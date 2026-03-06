//! End-to-end compiler pipeline orchestration for the Thagore CLI.

use std::boxed::Box;
use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::path::{Path, PathBuf};
use std::time::Instant;

use bumpalo::Bump;
use tempfile::NamedTempFile;
use thagore_ast::visitor::{walk_decl, Visitor};
use thagore_ast::{
    AssignExpr, BinaryExpr, Block, BreakStmt, CallExpr, ContinueStmt, Decl, Expr, ExprStmt,
    ExternDecl, FieldAccessExpr, FieldDef, FlowDecl, FlowStage, ForStmt, FuncDecl, GenericTypeExpr,
    IdentExpr, IfStmt, ImplBlock, IntentDecl, InternedStr, LetDecl, LitExpr, Literal,
    NamedTypeExpr, NodeId, Param, ReturnStmt, Stmt, StructDecl, TypeExpr, UnaryExpr, WhileStmt,
    Span,
};
use thagore_codegen::{
    link_binary, Codegen, CodegenOptions, DebugOptions, OptimizationLevel, OutputArtifacts,
    OutputConfig, TargetMachineConfig,
};
use thagore_ir::{IrLowerer, LoweringError};
use thagore_lexer::Lexer;
use thagore_parser::{ConditionDelimiter, ErrorKind, ParseError, Parser};
use thagore_typeck::{TypeArena, TypeChecker, TypeError, TypeId, TypeKind};

use crate::cli::{BuildOptions, EmitKind, OptLevel, RunOptions};
use crate::error::CompilerDiagnostic;
use crate::timer::TimingReport;

const MODULE_SYMBOL: InternedStr = InternedStr::new(u32::MAX - 32);

/// Successful `build` pipeline result.
#[derive(Debug)]
pub(crate) struct BuildResult {
    /// Emitted artifact paths.
    pub artifacts: OutputArtifacts,
    /// Per-stage timing report.
    pub timings: TimingReport,
}

/// Failed compiler pipeline result.
#[derive(Debug)]
pub(crate) struct PipelineFailure {
    /// Collected user-facing diagnostics.
    pub diagnostics: Vec<CompilerDiagnostic>,
    /// Source text used to compute line and column numbers.
    pub source: String,
    /// Per-stage timing report up to the point of failure.
    pub timings: TimingReport,
}

/// Runs lexing, parsing, and type checking without code generation.
pub(crate) fn check_file(
    path: &Path,
    include_dirs: &[PathBuf],
    defines: &[String],
    features: &[String],
) -> Result<(), PipelineFailure> {
    let mut timings = TimingReport::new();
    let source = read_source(path, &timings)?;
    let arena = Bump::new();

    let mut lexer = Lexer::new(&source);
    let stage = Instant::now();
    let tokens = lexer.lex_all_in(&arena);
    timings.record("lexer", stage.elapsed());

    let mut parser = Parser::new(tokens.as_slice(), &arena, lexer.interner());
    let stage = Instant::now();
    let decls = parser.parse_program();
    let parse_errors = parser.take_errors();
    timings.record("parser", stage.elapsed());
    if !parse_errors.is_empty() {
        return Err(PipelineFailure {
            diagnostics: parse_errors
                .iter()
                .map(|error| convert_parse_error(error))
                .collect(),
            source: source.clone(),
            timings,
        });
    }

    let mut decls = load_program(path, include_dirs, &mut parser, &arena, decls, &source, &timings)?;
    inject_compile_time_bindings(
        &mut decls,
        &mut parser,
        &arena,
        defines,
        features,
        &source,
        &timings,
    )?;

    let mut checker = TypeChecker::new();
    register_symbols(&decls, &parser, &mut checker, None);
    let stage = Instant::now();
    let table = checker.check(&decls);
    timings.record("typeck", stage.elapsed());
    if let Err(errors) = table {
        let types = checker.types().clone();
        return Err(PipelineFailure {
            diagnostics: errors
                .iter()
                .map(|error| convert_type_error(error, &types, &parser))
                .collect(),
            source: source.clone(),
            timings,
        });
    }

    Ok(())
}

/// Runs the full compiler pipeline and emits requested output artifacts.
pub(crate) fn build_file(
    path: &Path,
    options: &BuildOptions,
) -> Result<BuildResult, PipelineFailure> {
    let mut timings = TimingReport::new();
    let source = read_source(path, &timings)?;
    let arena = Bump::new();

    let mut lexer = Lexer::new(&source);
    let stage = Instant::now();
    let tokens = lexer.lex_all_in(&arena);
    timings.record("lexer", stage.elapsed());

    let mut parser = Parser::new(tokens.as_slice(), &arena, lexer.interner());
    let stage = Instant::now();
    let decls = parser.parse_program();
    let parse_errors = parser.take_errors();
    timings.record("parser", stage.elapsed());
    if !parse_errors.is_empty() {
        return Err(PipelineFailure {
            diagnostics: parse_errors
                .iter()
                .map(|error| convert_parse_error(error))
                .collect(),
            source: source.clone(),
            timings,
        });
    }

    let mut decls = load_program(
        path,
        &options.include_dirs,
        &mut parser,
        &arena,
        decls,
        &source,
        &timings,
    )?;
    inject_compile_time_bindings(
        &mut decls,
        &mut parser,
        &arena,
        &options.defines,
        &options.features,
        &source,
        &timings,
    )?;

    let mut checker = TypeChecker::new();
    let mut codegen = Codegen::new();
    let module_name = module_name_from_path(path);
    codegen.register_symbol_name(MODULE_SYMBOL, &module_name);
    register_symbols(&decls, &parser, &mut checker, Some(&mut codegen));

    let stage = Instant::now();
    let table = checker.check(&decls);
    timings.record("typeck", stage.elapsed());
    let table = match table {
        Ok(table) => table,
        Err(errors) => {
            let types = checker.types().clone();
            return Err(PipelineFailure {
                diagnostics: errors
                    .iter()
                    .map(|error| convert_type_error(error, &types, &parser))
                    .collect(),
                source: source.clone(),
                timings,
            });
        }
    };

    let types = checker.types().clone();
    let stage = Instant::now();
    let mut lowerer = IrLowerer::new(MODULE_SYMBOL, &types, &table);
    let ir_module = lowerer.lower_module(&decls);
    timings.record("ir lower", stage.elapsed());
    let ir_module = match ir_module {
        Ok(module) => module,
        Err(errors) => {
            return Err(PipelineFailure {
                diagnostics: errors
                    .iter()
                    .map(|error| convert_lowering_error(error, &types, &parser))
                    .collect(),
                source: source.clone(),
                timings,
            });
        }
    };

    let emit = RequestedOutputs::new(&options.emit, true);
    let binary_path = options
        .output
        .clone()
        .unwrap_or_else(|| default_binary_output(path));
    let llvm_ir_path = emit.ll.then(|| binary_path.with_extension("ll"));
    let bitcode_path = emit.bc.then(|| binary_path.with_extension("bc"));
    let object_path = if emit.obj || emit.bin {
        Some(binary_path.with_extension("o"))
    } else {
        None
    };

    let transient_object = if emit.bin && !emit.obj {
        Some(NamedTempFile::new().map_err(|error| PipelineFailure {
            diagnostics: vec![CompilerDiagnostic::new(
                "CLI004",
                "failed to create temporary object file",
                error.to_string(),
                None,
            )],
            source: source.clone(),
            timings: timings.clone(),
        })?)
    } else {
        None
    };
    let object_emit_path = transient_object
        .as_ref()
        .map(|file| file.path().to_path_buf())
        .or(object_path.clone());

    let stage = Instant::now();
    let mut codegen_options = CodegenOptions::default();
    codegen_options.optimization = map_opt_level(options.opt);
    codegen_options.target = TargetMachineConfig {
        triple: options.target.clone(),
        ..TargetMachineConfig::default()
    };
    codegen_options.debug = DebugOptions {
        enabled: options.debug,
        source_name: path.display().to_string(),
    };
    codegen_options.output = OutputConfig {
        llvm_ir: llvm_ir_path.clone(),
        bitcode: bitcode_path.clone(),
        object: object_emit_path.clone(),
        binary: None,
    };
    codegen.set_options(codegen_options);
    let emitted = codegen.emit(&ir_module, &types);
    timings.record("codegen", stage.elapsed());

    let mut emitted = match emitted {
        Ok(result) => result.artifacts,
        Err(errors) => {
            return Err(PipelineFailure {
                diagnostics: errors
                    .iter()
                    .map(|error| convert_codegen_error(error, &types, &parser))
                    .collect(),
                source: source.clone(),
                timings,
            });
        }
    };

    if !emit.obj {
        emitted.object = None;
    }

    if emit.bin {
        let object_for_link = object_emit_path.expect("object path must exist for binary emission");
        let stage = Instant::now();
        if let Err(error) = link_binary(&object_for_link, &binary_path) {
            timings.record("link", stage.elapsed());
            return Err(PipelineFailure {
                diagnostics: vec![convert_codegen_error(&error, &types, &parser)],
                source: source.clone(),
                timings,
            });
        }
        timings.record("link", stage.elapsed());
        emitted.binary = Some(binary_path);
    }

    Ok(BuildResult {
        artifacts: emitted,
        timings,
    })
}

/// Converts `run` options into build options and forces binary emission.
#[must_use]
pub(crate) fn build_options_for_run(options: &RunOptions) -> BuildOptions {
    let mut emit = options.emit.clone();
    if !emit.contains(&EmitKind::Bin) {
        emit.push(EmitKind::Bin);
    }

    BuildOptions {
        output: None,
        opt: options.opt,
        emit,
        debug: options.debug,
        target: options.target.clone(),
        include_dirs: options.include_dirs.clone(),
        defines: options.defines.clone(),
        features: options.features.clone(),
        json_errors: options.json_errors,
        time: options.time,
    }
}

fn read_source(path: &Path, timings: &TimingReport) -> Result<String, PipelineFailure> {
    fs::read_to_string(path).map_err(|error| PipelineFailure {
        diagnostics: vec![CompilerDiagnostic::new(
            "CLI001",
            "failed to read source file",
            error.to_string(),
            None,
        )],
        source: String::new(),
        timings: timings.clone(),
    })
}

fn module_name_from_path(path: &Path) -> String {
    path.file_stem()
        .and_then(|stem| stem.to_str())
        .filter(|stem| !stem.is_empty())
        .unwrap_or("module")
        .to_string()
}

fn default_binary_output(path: &Path) -> PathBuf {
    let stem = path
        .file_stem()
        .and_then(|stem| stem.to_str())
        .filter(|stem| !stem.is_empty())
        .unwrap_or("a.out");
    path.parent()
        .unwrap_or_else(|| Path::new("."))
        .join(stem)
}

fn inject_compile_time_bindings<'src, 'tok, 'ast>(
    decls: &mut Vec<Decl<'ast>>,
    parser: &mut Parser<'src, 'tok, 'ast>,
    arena: &'ast Bump,
    defines: &[String],
    features: &[String],
    source: &str,
    timings: &TimingReport,
) -> Result<(), PipelineFailure> {
    if defines.is_empty() && features.is_empty() {
        return Ok(());
    }

    let mut synthetic = Vec::new();
    let mut next_id = max_existing_node_id(decls).saturating_add(1);

    for feature in features {
        if feature.trim().is_empty() {
            continue;
        }
        let binding = format!("FEATURE_{}", sanitize_binding_name(feature));
        synthetic.push(make_boolean_binding(
            parser,
            arena,
            &binding,
            true,
            &mut next_id,
        ));
    }

    for define in defines {
        let Some((name, value)) = define.split_once('=') else {
            return Err(PipelineFailure {
                diagnostics: vec![CompilerDiagnostic::new(
                    "CLI002",
                    "invalid --define value",
                    format!("expected KEY=VALUE, found `{define}`"),
                    None,
                )],
                source: source.to_string(),
                timings: timings.clone(),
            });
        };
        if name.trim().is_empty() {
            return Err(PipelineFailure {
                diagnostics: vec![CompilerDiagnostic::new(
                    "CLI002",
                    "invalid --define name",
                    format!("expected KEY=VALUE, found `{define}`"),
                    None,
                )],
                source: source.to_string(),
                timings: timings.clone(),
            });
        }
        synthetic.push(make_define_binding(
            parser,
            arena,
            name.trim(),
            value.trim(),
            &mut next_id,
        ));
    }

    if !synthetic.is_empty() {
        synthetic.append(decls);
        *decls = synthetic;
    }
    Ok(())
}

fn load_program<'src, 'tok, 'ast>(
    entry: &Path,
    include_dirs: &[PathBuf],
    parser: &mut Parser<'src, 'tok, 'ast>,
    arena: &'ast Bump,
    decls: Vec<Decl<'ast>>,
    source: &str,
    timings: &TimingReport,
) -> Result<Vec<Decl<'ast>>, PipelineFailure> {
    let mut loader = ModuleLoader::new(entry, include_dirs, parser, arena);
    loader.load_entry_module(decls);
    if loader.diagnostics.is_empty() {
        Ok(loader.program)
    } else {
        Err(PipelineFailure {
            diagnostics: loader.diagnostics,
            source: source.to_string(),
            timings: timings.clone(),
        })
    }
}

#[derive(Debug, Clone)]
struct ImportBinding {
    alias: InternedStr,
    namespace: String,
    resolved_path: PathBuf,
}

struct ModuleLoader<'a, 'src, 'tok, 'ast> {
    entry: &'a Path,
    include_dirs: &'a [PathBuf],
    parser: &'a mut Parser<'src, 'tok, 'ast>,
    arena: &'ast Bump,
    diagnostics: Vec<CompilerDiagnostic>,
    intern_cache: BTreeMap<String, InternedStr>,
    module_namespaces: BTreeMap<PathBuf, String>,
    loaded: BTreeSet<PathBuf>,
    loading: BTreeSet<PathBuf>,
    next_node_id: u32,
    program: Vec<Decl<'ast>>,
}

impl<'a, 'src, 'tok, 'ast> ModuleLoader<'a, 'src, 'tok, 'ast> {
    fn new(
        entry: &'a Path,
        include_dirs: &'a [PathBuf],
        parser: &'a mut Parser<'src, 'tok, 'ast>,
        arena: &'ast Bump,
    ) -> Self {
        Self {
            entry,
            include_dirs,
            parser,
            arena,
            diagnostics: Vec::new(),
            intern_cache: BTreeMap::new(),
            module_namespaces: BTreeMap::new(),
            loaded: BTreeSet::new(),
            loading: BTreeSet::new(),
            next_node_id: 0,
            program: Vec::new(),
        }
    }

    fn load_entry_module(&mut self, decls: Vec<Decl<'ast>>) {
        let entry_path = normalize_path(self.entry);
        self.module_namespaces
            .insert(entry_path.clone(), String::new());
        let symbols = collect_symbol_texts(&decls, &*self.parser);
        self.load_parsed_module(entry_path, None, decls, symbols);
    }

    fn load_imported_module(&mut self, module_path: PathBuf, namespace: String) {
        if self.loaded.contains(&module_path) {
            return;
        }

        let source = match fs::read_to_string(&module_path) {
            Ok(source) => Box::leak(source.into_boxed_str()) as &'static str,
            Err(error) => {
                self.diagnostics.push(CompilerDiagnostic::new(
                    "CLI001",
                    "failed to read imported module",
                    error.to_string(),
                    None,
                ));
                self.module_namespaces.insert(module_path, namespace);
                return;
            }
        };

        let lexer = Box::leak(Box::new(Lexer::new(source)));
        let tokens = lexer.lex_all_in(self.arena);
        let mut parser = Parser::new(tokens.as_slice(), self.arena, lexer.interner());
        let decls = parser.parse_program();
        let parse_errors = parser.take_errors();
        if !parse_errors.is_empty() {
            self.diagnostics
                .extend(parse_errors.iter().map(convert_parse_error));
            self.module_namespaces.insert(module_path, namespace);
            return;
        }

        let symbols = collect_symbol_texts(&decls, &parser);
        self.load_parsed_module(module_path, Some(namespace), decls, symbols);
    }

    fn load_parsed_module(
        &mut self,
        module_path: PathBuf,
        namespace: Option<String>,
        decls: Vec<Decl<'ast>>,
        source_symbols: BTreeMap<InternedStr, String>,
    ) {
        if self.loading.contains(&module_path) {
            self.diagnostics.push(
                CompilerDiagnostic::new(
                    "CLI003",
                    "cyclic import",
                    format!("cyclic module load detected for `{}`", module_path.display()),
                    None,
                )
                .with_hint("break the cycle by moving shared definitions into a lower-level module"),
            );
            return;
        }

        self.loading.insert(module_path.clone());
        if self.loaded.contains(&module_path) {
            self.loading.remove(&module_path);
            return;
        }
        if let Some(namespace) = namespace.as_ref() {
            self.module_namespaces
                .entry(module_path.clone())
                .or_insert_with(|| namespace.clone());
        }

        let import_bindings = self.resolve_imports(&module_path, &decls, &source_symbols);
        for binding in &import_bindings {
            if !self.loading.contains(&binding.resolved_path)
                && !self.loaded.contains(&binding.resolved_path)
            {
                self.load_imported_module(binding.resolved_path.clone(), binding.namespace.clone());
            }
        }

        let mut rewriter = ModuleRewriter::new(
            self.arena,
            self.parser,
            &mut self.next_node_id,
            &mut self.intern_cache,
            &source_symbols,
            &decls,
            namespace.as_deref(),
            &import_bindings,
        );
        self.program.extend(rewriter.rewrite_program(&decls));
        self.loading.remove(&module_path);
        self.loaded.insert(module_path.clone());
        self.module_namespaces
            .entry(module_path)
            .or_insert_with(String::new);
    }

    fn resolve_imports(
        &mut self,
        current_path: &Path,
        decls: &[Decl<'ast>],
        source_symbols: &BTreeMap<InternedStr, String>,
    ) -> Vec<ImportBinding> {
        let mut bindings = Vec::new();
        for decl in decls {
            let Decl::Import(import_decl) = decl else {
                continue;
            };

            let module_segments = import_decl
                .path_segments
                .iter()
                .map(|segment| {
                    source_symbols
                        .get(segment)
                        .cloned()
                        .unwrap_or_else(|| format!("sym_{}", segment.as_u32()))
                })
                .collect::<Vec<_>>();
            let module_refs = module_segments.iter().map(String::as_str).collect::<Vec<_>>();
            let Some(resolved_path) =
                resolve_import_path(current_path, self.include_dirs, &module_refs)
            else {
                if module_segments.first().map(String::as_str) == Some("std") {
                    continue;
                }
                self.diagnostics.push(
                    CompilerDiagnostic::new(
                        "CLI003",
                        "unresolved import",
                        format!("could not resolve import `{}`", module_segments.join(".")),
                        Some(import_decl.span),
                    )
                    .with_hint(
                        "pass --include-dir for dependency roots or add the module to stdlib",
                    ),
                );
                continue;
            };

            let normalized = normalize_path(&resolved_path);
            let namespace = self
                .module_namespaces
                .get(&normalized)
                .cloned()
                .unwrap_or_else(|| module_namespace(&module_segments));
            self.module_namespaces
                .entry(normalized.clone())
                .or_insert_with(|| namespace.clone());
            let alias = import_decl
                .alias
                .or_else(|| import_decl.path_segments.last().copied())
                .unwrap_or_else(|| InternedStr::new(u32::MAX));
            bindings.push(ImportBinding {
                alias,
                namespace,
                resolved_path: normalized,
            });
        }
        bindings
    }
}

fn resolve_import_path(entry: &Path, include_dirs: &[PathBuf], segments: &[&str]) -> Option<PathBuf> {
    let mut roots = Vec::new();
    if let Some(parent) = entry.parent() {
        roots.push(parent.to_path_buf());
    }
    roots.extend(include_dirs.iter().cloned());
    roots.push(stdlib_root());

    for root in roots {
        let candidate = segments.iter().fold(root.clone(), |path, segment| path.join(segment));
        let file = candidate.with_extension("tg");
        if file.is_file() {
            return Some(file);
        }
    }
    None
}

fn stdlib_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../stdlib")
        .to_path_buf()
}

fn normalize_path(path: &Path) -> PathBuf {
    path.canonicalize().unwrap_or_else(|_| path.to_path_buf())
}

fn module_namespace(segments: &[String]) -> String {
    segments.join("__")
}

struct ModuleRewriter<'a, 'src, 'tok, 'ast> {
    arena: &'ast Bump,
    parser: &'a mut Parser<'src, 'tok, 'ast>,
    next_node_id: &'a mut u32,
    intern_cache: &'a mut BTreeMap<String, InternedStr>,
    source_symbols: &'a BTreeMap<InternedStr, String>,
    import_namespaces: BTreeMap<InternedStr, String>,
    top_level_renames: BTreeMap<InternedStr, InternedStr>,
    scopes: Vec<BTreeSet<InternedStr>>,
}

impl<'a, 'src, 'tok, 'ast> ModuleRewriter<'a, 'src, 'tok, 'ast> {
    fn new(
        arena: &'ast Bump,
        parser: &'a mut Parser<'src, 'tok, 'ast>,
        next_node_id: &'a mut u32,
        intern_cache: &'a mut BTreeMap<String, InternedStr>,
        source_symbols: &'a BTreeMap<InternedStr, String>,
        decls: &[Decl<'ast>],
        namespace: Option<&str>,
        imports: &[ImportBinding],
    ) -> Self {
        let mut import_namespaces = BTreeMap::new();
        for import in imports {
            import_namespaces.insert(import.alias, import.namespace.clone());
        }

        let mut top_level_renames = BTreeMap::new();
        if let Some(namespace) = namespace {
            for symbol in top_level_symbols(decls) {
                let text = source_symbols
                    .get(&symbol)
                    .map(String::as_str)
                    .unwrap_or("__error__");
                top_level_renames.insert(
                    symbol,
                    intern_owned_cached(parser, intern_cache, &format!("{namespace}__{text}")),
                );
            }
        }

        Self {
            arena,
            parser,
            next_node_id,
            intern_cache,
            source_symbols,
            import_namespaces,
            top_level_renames,
            scopes: Vec::new(),
        }
    }

    fn rewrite_program(&mut self, decls: &[Decl<'ast>]) -> Vec<Decl<'ast>> {
        decls
            .iter()
            .filter_map(|decl| self.rewrite_decl(decl))
            .collect()
    }

    fn rewrite_decl(&mut self, decl: &Decl<'ast>) -> Option<Decl<'ast>> {
        match decl {
            Decl::Import(_) => None,
            Decl::Func(node) => Some(Decl::Func(self.rewrite_func_decl(node))),
            Decl::Let(node) => Some(Decl::Let(self.rewrite_let_decl(node, true))),
            Decl::Struct(node) => Some(Decl::Struct(self.rewrite_struct_decl(node))),
            Decl::Impl(node) => Some(Decl::Impl(self.rewrite_impl_block(node))),
            Decl::Extern(node) => Some(Decl::Extern(self.rewrite_extern_decl(node))),
            Decl::Intent(node) => Some(Decl::Intent(self.rewrite_intent_decl(node))),
            Decl::Flow(node) => Some(Decl::Flow(self.rewrite_flow_decl(node))),
        }
    }

    fn rewrite_func_decl(&mut self, decl: &FuncDecl<'ast>) -> FuncDecl<'ast> {
        self.push_scope();
        let params = decl
            .params
            .iter()
            .map(|param| {
                self.define_local(param.name);
                self.rewrite_param(param)
            })
            .collect::<Vec<_>>();
        let return_type = decl.return_type.map(|ty| self.rewrite_type_expr(ty));
        let body = self.rewrite_block(decl.body);
        self.pop_scope();
        FuncDecl {
            id: self.new_node_id(),
            span: decl.span,
            name: self.rename_top_level_symbol(decl.name),
            params: self.arena.alloc_slice_fill_iter(params),
            return_type,
            body,
        }
    }

    fn rewrite_let_decl(&mut self, decl: &LetDecl<'ast>, is_top_level: bool) -> LetDecl<'ast> {
        let initializer = self.rewrite_expr(decl.initializer);
        let ty = decl.ty.map(|ty| self.rewrite_type_expr(ty));
        let name = if is_top_level {
            self.rename_top_level_symbol(decl.name)
        } else {
            self.canonical_symbol(decl.name)
        };
        if !is_top_level {
            self.define_local(decl.name);
        }
        LetDecl {
            id: self.new_node_id(),
            span: decl.span,
            name,
            ty,
            initializer,
        }
    }

    fn rewrite_struct_decl(&mut self, decl: &StructDecl<'ast>) -> StructDecl<'ast> {
        let fields = decl
            .fields
            .iter()
            .map(|field| self.rewrite_field_def(field))
            .collect::<Vec<_>>();
        StructDecl {
            id: self.new_node_id(),
            span: decl.span,
            name: self.rename_top_level_symbol(decl.name),
            fields: self.arena.alloc_slice_fill_iter(fields),
        }
    }

    fn rewrite_impl_block(&mut self, decl: &ImplBlock<'ast>) -> ImplBlock<'ast> {
        let methods = decl
            .methods
            .iter()
            .map(|method| self.rewrite_func_decl(method))
            .collect::<Vec<_>>();
        ImplBlock {
            id: self.new_node_id(),
            span: decl.span,
            target: self.rename_top_level_symbol(decl.target),
            methods: self.arena.alloc_slice_fill_iter(methods),
        }
    }

    fn rewrite_extern_decl(&mut self, decl: &ExternDecl<'ast>) -> ExternDecl<'ast> {
        let params = decl
            .params
            .iter()
            .map(|param| self.rewrite_param(param))
            .collect::<Vec<_>>();
        ExternDecl {
            id: self.new_node_id(),
            span: decl.span,
            name: self.canonical_symbol(decl.name),
            params: self.arena.alloc_slice_fill_iter(params),
            return_type: self.rewrite_type_expr(decl.return_type),
        }
    }

    fn rewrite_intent_decl(&mut self, decl: &IntentDecl<'ast>) -> IntentDecl<'ast> {
        self.push_scope();
        let constraints = decl
            .constraints
            .iter()
            .map(|expr| self.rewrite_expr(expr))
            .collect::<Vec<_>>();
        let body = self.rewrite_block(decl.body);
        self.pop_scope();
        IntentDecl {
            id: self.new_node_id(),
            span: decl.span,
            name: self.rename_top_level_symbol(decl.name),
            constraints: self.arena.alloc_slice_fill_iter(constraints),
            body,
        }
    }

    fn rewrite_flow_decl(&mut self, decl: &FlowDecl<'ast>) -> FlowDecl<'ast> {
        self.push_scope();
        let stages = decl
            .stages
            .iter()
            .map(|stage| self.rewrite_flow_stage(stage))
            .collect::<Vec<_>>();
        let compensation = decl.compensation.map(|block| self.rewrite_block(block));
        self.pop_scope();
        FlowDecl {
            id: self.new_node_id(),
            span: decl.span,
            name: self.rename_top_level_symbol(decl.name),
            stages: self.arena.alloc_slice_fill_iter(stages),
            compensation,
        }
    }

    fn rewrite_flow_stage(&mut self, stage: &FlowStage<'ast>) -> FlowStage<'ast> {
        FlowStage {
            id: self.new_node_id(),
            span: stage.span,
            name: self.canonical_symbol(stage.name),
            body: self.rewrite_block(stage.body),
        }
    }

    fn rewrite_param(&mut self, param: &Param<'ast>) -> Param<'ast> {
        Param {
            id: self.new_node_id(),
            span: param.span,
            name: self.canonical_symbol(param.name),
            ty: self.rewrite_type_expr(param.ty),
        }
    }

    fn rewrite_field_def(&mut self, field: &FieldDef<'ast>) -> FieldDef<'ast> {
        FieldDef {
            id: self.new_node_id(),
            span: field.span,
            name: self.canonical_symbol(field.name),
            ty: self.rewrite_type_expr(field.ty),
        }
    }

    fn rewrite_block(&mut self, block: &Block<'ast>) -> &'ast Block<'ast> {
        self.push_scope();
        let statements = block
            .statements
            .iter()
            .map(|stmt| self.rewrite_stmt(stmt))
            .collect::<Vec<_>>();
        self.pop_scope();
        self.arena.alloc(Block {
            id: self.new_node_id(),
            span: block.span,
            statements: self.arena.alloc_slice_fill_iter(statements),
        })
    }

    fn rewrite_stmt(&mut self, stmt: &Stmt<'ast>) -> Stmt<'ast> {
        match stmt {
            Stmt::Let(node) => Stmt::Let(self.rewrite_let_decl(node, false)),
            Stmt::Expr(node) => Stmt::Expr(ExprStmt {
                id: self.new_node_id(),
                span: node.span,
                expr: self.rewrite_expr(node.expr),
            }),
            Stmt::Return(node) => Stmt::Return(ReturnStmt {
                id: self.new_node_id(),
                span: node.span,
                value: node.value.map(|expr| self.rewrite_expr(expr)),
            }),
            Stmt::If(node) => Stmt::If(IfStmt {
                id: self.new_node_id(),
                span: node.span,
                condition: self.rewrite_expr(node.condition),
                then_block: self.rewrite_block(node.then_block),
                else_block: node.else_block.map(|block| self.rewrite_block(block)),
            }),
            Stmt::While(node) => Stmt::While(WhileStmt {
                id: self.new_node_id(),
                span: node.span,
                condition: self.rewrite_expr(node.condition),
                body: self.rewrite_block(node.body),
            }),
            Stmt::For(node) => {
                let iterator = self.rewrite_expr(node.iterator);
                self.push_scope();
                self.define_local(node.binding);
                let body = self.rewrite_block(node.body);
                self.pop_scope();
                Stmt::For(ForStmt {
                    id: self.new_node_id(),
                    span: node.span,
                    binding: self.canonical_symbol(node.binding),
                    iterator,
                    body,
                })
            }
            Stmt::Break(node) => Stmt::Break(BreakStmt {
                id: self.new_node_id(),
                span: node.span,
            }),
            Stmt::Continue(node) => Stmt::Continue(ContinueStmt {
                id: self.new_node_id(),
                span: node.span,
            }),
        }
    }

    fn rewrite_expr(&mut self, expr: &Expr<'ast>) -> &'ast Expr<'ast> {
        let rewritten = match expr {
            Expr::Binary(node) => Expr::Binary(BinaryExpr {
                id: self.new_node_id(),
                span: node.span,
                left: self.rewrite_expr(node.left),
                op: node.op,
                right: self.rewrite_expr(node.right),
            }),
            Expr::Unary(node) => Expr::Unary(UnaryExpr {
                id: self.new_node_id(),
                span: node.span,
                op: node.op,
                operand: self.rewrite_expr(node.operand),
            }),
            Expr::Call(node) => {
                let args = node
                    .args
                    .iter()
                    .map(|arg| self.rewrite_expr(arg))
                    .collect::<Vec<_>>();
                Expr::Call(CallExpr {
                    id: self.new_node_id(),
                    span: node.span,
                    callee: self.rewrite_expr(node.callee),
                    args: self.arena.alloc_slice_fill_iter(args),
                })
            }
            Expr::FieldAccess(node) => {
                if let Expr::Ident(base) = node.object {
                    if !self.is_local(base.name) {
                        if let Some(namespace) = self.import_namespaces.get(&base.name) {
                            let symbol =
                                intern_owned_cached(
                                    self.parser,
                                    self.intern_cache,
                                    &format!("{namespace}__{}", self.text_of(node.field)),
                                );
                            Expr::Ident(IdentExpr {
                                id: self.new_node_id(),
                                span: node.span,
                                name: symbol,
                            })
                        } else {
                            Expr::FieldAccess(FieldAccessExpr {
                                id: self.new_node_id(),
                                span: node.span,
                                object: self.rewrite_expr(node.object),
                                field: self.canonical_symbol(node.field),
                            })
                        }
                    } else {
                        Expr::FieldAccess(FieldAccessExpr {
                            id: self.new_node_id(),
                            span: node.span,
                            object: self.rewrite_expr(node.object),
                            field: self.canonical_symbol(node.field),
                        })
                    }
                } else {
                    Expr::FieldAccess(FieldAccessExpr {
                        id: self.new_node_id(),
                        span: node.span,
                        object: self.rewrite_expr(node.object),
                        field: self.canonical_symbol(node.field),
                    })
                }
            }
            Expr::Index(node) => Expr::Index(thagore_ast::IndexExpr {
                id: self.new_node_id(),
                span: node.span,
                object: self.rewrite_expr(node.object),
                index: self.rewrite_expr(node.index),
            }),
            Expr::Ident(node) => Expr::Ident(IdentExpr {
                id: self.new_node_id(),
                span: node.span,
                name: self.rewrite_identifier(node.name),
            }),
            Expr::Literal(node) => Expr::Literal(LitExpr {
                id: self.new_node_id(),
                span: node.span,
                literal: self.rewrite_literal(node.literal.clone()),
            }),
            Expr::Assign(node) => Expr::Assign(AssignExpr {
                id: self.new_node_id(),
                span: node.span,
                target: self.rewrite_expr(node.target),
                value: self.rewrite_expr(node.value),
            }),
        };
        self.arena.alloc(rewritten)
    }

    fn rewrite_type_expr(&mut self, ty: &TypeExpr<'ast>) -> &'ast TypeExpr<'ast> {
        let rewritten = match ty {
            TypeExpr::Named(node) => TypeExpr::Named(NamedTypeExpr {
                id: self.new_node_id(),
                span: node.span,
                name: self.rewrite_type_name(node.name),
            }),
            TypeExpr::Generic(node) => {
                let args = node
                    .args
                    .iter()
                    .map(|arg| self.rewrite_type_expr(arg))
                    .collect::<Vec<_>>();
                TypeExpr::Generic(GenericTypeExpr {
                    id: self.new_node_id(),
                    span: node.span,
                    name: self.rewrite_type_name(node.name),
                    args: self.arena.alloc_slice_fill_iter(args),
                })
            }
            TypeExpr::Infer(node) => TypeExpr::Infer(thagore_ast::InferTypeExpr {
                id: self.new_node_id(),
                span: node.span,
            }),
        };
        self.arena.alloc(rewritten)
    }

    fn rewrite_literal(&mut self, literal: Literal) -> Literal {
        match literal {
            Literal::Str(symbol) => Literal::Str(self.canonical_symbol(symbol)),
            other => other,
        }
    }

    fn rewrite_identifier(&mut self, symbol: InternedStr) -> InternedStr {
        if self.is_local(symbol) {
            self.canonical_symbol(symbol)
        } else if let Some(renamed) = self.top_level_renames.get(&symbol).copied() {
            renamed
        } else {
            self.canonical_symbol(symbol)
        }
    }

    fn rewrite_type_name(&mut self, symbol: InternedStr) -> InternedStr {
        self.top_level_renames
            .get(&symbol)
            .copied()
            .unwrap_or_else(|| self.canonical_symbol(symbol))
    }

    fn rename_top_level_symbol(&mut self, symbol: InternedStr) -> InternedStr {
        self.top_level_renames
            .get(&symbol)
            .copied()
            .unwrap_or_else(|| self.canonical_symbol(symbol))
    }

    fn canonical_symbol(&mut self, symbol: InternedStr) -> InternedStr {
        let text = self.text_of(symbol).to_string();
        intern_owned_cached(self.parser, self.intern_cache, &text)
    }

    fn text_of(&self, symbol: InternedStr) -> &str {
        self.source_symbols
            .get(&symbol)
            .map(String::as_str)
            .unwrap_or("__error__")
    }

    fn push_scope(&mut self) {
        self.scopes.push(BTreeSet::new());
    }

    fn pop_scope(&mut self) {
        self.scopes.pop();
    }

    fn define_local(&mut self, symbol: InternedStr) {
        if let Some(scope) = self.scopes.last_mut() {
            scope.insert(symbol);
        }
    }

    fn is_local(&self, symbol: InternedStr) -> bool {
        self.scopes.iter().rev().any(|scope| scope.contains(&symbol))
    }

    fn new_node_id(&mut self) -> NodeId {
        let id = NodeId::new(*self.next_node_id);
        *self.next_node_id = self.next_node_id.saturating_add(1);
        id
    }
}

fn sanitize_binding_name(name: &str) -> String {
    let mut sanitized = String::with_capacity(name.len());
    for ch in name.chars() {
        if ch.is_ascii_alphanumeric() {
            sanitized.push(ch.to_ascii_uppercase());
        } else {
            sanitized.push('_');
        }
    }
    if sanitized.is_empty() {
        "_".to_string()
    } else {
        sanitized
    }
}

fn make_boolean_binding<'src, 'tok, 'ast>(
    parser: &mut Parser<'src, 'tok, 'ast>,
    arena: &'ast Bump,
    name: &str,
    value: bool,
    next_id: &mut u32,
) -> Decl<'ast> {
    make_literal_binding(
        parser,
        arena,
        name,
        Literal::Bool(value),
        "bool",
        next_id,
    )
}

fn make_define_binding<'src, 'tok, 'ast>(
    parser: &mut Parser<'src, 'tok, 'ast>,
    arena: &'ast Bump,
    name: &str,
    value: &str,
    next_id: &mut u32,
) -> Decl<'ast> {
    if matches!(value, "true" | "false") {
        return make_boolean_binding(parser, arena, name, value == "true", next_id);
    }
    if let Ok(int_value) = value.parse::<i64>() {
        return make_literal_binding(
            parser,
            arena,
            name,
            Literal::Int(int_value),
            "i32",
            next_id,
        );
    }
    if let Ok(float_value) = value.parse::<f64>() {
        return make_literal_binding(
            parser,
            arena,
            name,
            Literal::Float(float_value),
            "f64",
            next_id,
        );
    }

    let text = value.trim_matches('"');
    let symbol = parser.intern_text(Box::leak(text.to_string().into_boxed_str()));
    make_literal_binding(parser, arena, name, Literal::Str(symbol), "str", next_id)
}

fn make_literal_binding<'src, 'tok, 'ast>(
    parser: &mut Parser<'src, 'tok, 'ast>,
    arena: &'ast Bump,
    name: &str,
    literal: Literal,
    type_name: &str,
    next_id: &mut u32,
) -> Decl<'ast> {
    let span = Span::empty();
    let name_symbol = parser.intern_text(Box::leak(name.to_string().into_boxed_str()));
    let type_symbol = parser.intern_text(Box::leak(type_name.to_string().into_boxed_str()));

    let ty = arena.alloc(TypeExpr::Named(NamedTypeExpr {
        id: next_synthetic_node(next_id),
        span,
        name: type_symbol,
    }));
    let initializer = arena.alloc(Expr::Literal(LitExpr {
        id: next_synthetic_node(next_id),
        span,
        literal,
    }));

    Decl::Let(LetDecl {
        id: next_synthetic_node(next_id),
        span,
        name: name_symbol,
        ty: Some(ty),
        initializer,
    })
}

fn next_synthetic_node(next_id: &mut u32) -> NodeId {
    let id = NodeId::new(*next_id);
    *next_id = next_id.saturating_add(1);
    id
}

fn max_existing_node_id<'ast>(decls: &[Decl<'ast>]) -> u32 {
    let mut collector = MaxNodeIdCollector::default();
    for decl in decls {
        walk_decl(&mut collector, decl);
    }
    collector.max
}

#[derive(Debug, Default)]
struct MaxNodeIdCollector {
    max: u32,
}

impl MaxNodeIdCollector {
    fn record(&mut self, id: NodeId) {
        self.max = self.max.max(id.as_u32());
    }
}

impl<'ast> Visitor<'ast> for MaxNodeIdCollector {
    fn visit_decl(&mut self, decl: &'ast thagore_ast::Decl<'ast>) {
        self.record(decl.id());
    }

    fn visit_param(&mut self, param: &'ast thagore_ast::Param<'ast>) {
        self.record(param.id);
    }

    fn visit_field_def(&mut self, field: &'ast thagore_ast::FieldDef<'ast>) {
        self.record(field.id);
    }

    fn visit_flow_stage(&mut self, stage: &'ast thagore_ast::FlowStage<'ast>) {
        self.record(stage.id);
    }

    fn visit_stmt(&mut self, stmt: &'ast thagore_ast::Stmt<'ast>) {
        self.record(stmt.id());
    }

    fn visit_block(&mut self, block: &'ast thagore_ast::Block<'ast>) {
        self.record(block.id);
    }

    fn visit_expr_stmt(&mut self, stmt: &'ast thagore_ast::ExprStmt<'ast>) {
        self.record(stmt.id);
    }

    fn visit_return_stmt(&mut self, stmt: &'ast thagore_ast::ReturnStmt<'ast>) {
        self.record(stmt.id);
    }

    fn visit_if_stmt(&mut self, stmt: &'ast thagore_ast::IfStmt<'ast>) {
        self.record(stmt.id);
    }

    fn visit_while_stmt(&mut self, stmt: &'ast thagore_ast::WhileStmt<'ast>) {
        self.record(stmt.id);
    }

    fn visit_for_stmt(&mut self, stmt: &'ast thagore_ast::ForStmt<'ast>) {
        self.record(stmt.id);
    }

    fn visit_expr(&mut self, expr: &'ast thagore_ast::Expr<'ast>) {
        self.record(expr.id());
    }

    fn visit_ident_expr(&mut self, expr: &'ast thagore_ast::IdentExpr) {
        self.record(expr.id);
    }

    fn visit_lit_expr(&mut self, expr: &'ast thagore_ast::LitExpr) {
        self.record(expr.id);
    }

    fn visit_type_expr(&mut self, ty: &'ast thagore_ast::TypeExpr<'ast>) {
        self.record(ty.id());
    }

    fn visit_named_type_expr(&mut self, ty: &'ast thagore_ast::NamedTypeExpr) {
        self.record(ty.id);
    }

    fn visit_generic_type_expr(&mut self, ty: &'ast thagore_ast::GenericTypeExpr<'ast>) {
        self.record(ty.id);
    }

    fn visit_infer_type_expr(&mut self, ty: &'ast thagore_ast::InferTypeExpr) {
        self.record(ty.id);
    }
}

fn map_opt_level(level: OptLevel) -> OptimizationLevel {
    match level {
        OptLevel::O0 => OptimizationLevel::O0,
        OptLevel::O1 => OptimizationLevel::O1,
        OptLevel::O2 => OptimizationLevel::O2,
        OptLevel::O3 => OptimizationLevel::O3,
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct RequestedOutputs {
    ll: bool,
    bc: bool,
    obj: bool,
    bin: bool,
}

impl RequestedOutputs {
    fn new(requested: &[EmitKind], force_bin: bool) -> Self {
        let mut outputs = Self {
            ll: false,
            bc: false,
            obj: false,
            bin: force_bin,
        };
        for emit in requested {
            match emit {
                EmitKind::Ll => outputs.ll = true,
                EmitKind::Bc => outputs.bc = true,
                EmitKind::Obj => outputs.obj = true,
                EmitKind::Bin => outputs.bin = true,
            }
        }
        outputs
    }
}

fn convert_parse_error(error: &ParseError) -> CompilerDiagnostic {
    match &error.kind {
        ErrorKind::UnexpectedToken { .. } => CompilerDiagnostic::new(
            "P001",
            error.message(),
            error.kind.to_string(),
            Some(error.span),
        ),
        ErrorKind::MissingToken { .. } => CompilerDiagnostic::new(
            "P002",
            "missing token",
            error.kind.to_string(),
            Some(error.span),
        ),
        ErrorKind::MissingBlockColon => CompilerDiagnostic::new(
            "P003",
            "missing ':'",
            error.kind.to_string(),
            Some(error.span),
        )
        .with_hint("add ':' before the indented block"),
        ErrorKind::MissingConditionDelimiter {
            expected: ConditionDelimiter::OpenParen,
        } => CompilerDiagnostic::new(
            "P004",
            "missing '(' before condition",
            error.kind.to_string(),
            Some(error.span),
        )
        .with_hint("wrap the condition in parentheses"),
        ErrorKind::MissingConditionDelimiter {
            expected: ConditionDelimiter::CloseParen,
        } => CompilerDiagnostic::new(
            "P005",
            "missing ')' after condition",
            error.kind.to_string(),
            Some(error.span),
        )
        .with_hint("wrap the condition in parentheses"),
        ErrorKind::MissingDedent => CompilerDiagnostic::new(
            "P006",
            "missing dedent",
            error.kind.to_string(),
            Some(error.span),
        ),
        ErrorKind::LexerError { .. } => CompilerDiagnostic::new(
            "L001",
            "lexical error",
            error.kind.to_string(),
            Some(error.span),
        ),
    }
}

fn convert_type_error(
    error: &TypeError,
    types: &TypeArena,
    parser: &Parser<'_, '_, '_>,
) -> CompilerDiagnostic {
    match error {
        TypeError::TypeMismatch {
            expected,
            found,
            span,
        } => CompilerDiagnostic::new(
            "E001",
            "type mismatch",
            format!(
                "expected {}, found {}",
                render_type(*expected, types, parser),
                render_type(*found, types, parser)
            ),
            Some(*span),
        )
        .with_hint("make both sides use the same type"),
        TypeError::UnknownIdentifier { name, span } => CompilerDiagnostic::new(
            "E002",
            "unknown identifier",
            format!("`{}` is not in scope", render_symbol(*name, parser)),
            Some(*span),
        ),
        TypeError::UnknownField {
            struct_name,
            field,
            span,
        } => CompilerDiagnostic::new(
            "E003",
            "unknown field",
            format!(
                "struct `{}` has no field `{}`",
                render_symbol(*struct_name, parser),
                render_symbol(*field, parser)
            ),
            Some(*span),
        ),
        TypeError::NotCallable { found, span } => CompilerDiagnostic::new(
            "E004",
            "value is not callable",
            format!("type {} cannot be called", render_type(*found, types, parser)),
            Some(*span),
        ),
        TypeError::NotIndexable { found, span } => CompilerDiagnostic::new(
            "E005",
            "value is not indexable",
            format!("type {} cannot be indexed", render_type(*found, types, parser)),
            Some(*span),
        ),
        TypeError::ArgumentCountMismatch {
            expected,
            found,
            span,
        } => CompilerDiagnostic::new(
            "E006",
            "argument count mismatch",
            format!("expected {expected} arguments, found {found}"),
            Some(*span),
        ),
        TypeError::ReturnTypeMismatch {
            expected,
            found,
            span,
        } => CompilerDiagnostic::new(
            "E007",
            "return type mismatch",
            format!(
                "expected {}, found {}",
                render_type(*expected, types, parser),
                render_type(*found, types, parser)
            ),
            Some(*span),
        ),
        TypeError::ConditionNotBool { found, span } => CompilerDiagnostic::new(
            "E008",
            "condition must be bool",
            format!("found {}", render_type(*found, types, parser)),
            Some(*span),
        ),
        TypeError::InferenceFailure { span } => CompilerDiagnostic::new(
            "E009",
            "type inference failed",
            "the compiler could not infer a concrete type".to_string(),
            Some(*span),
        ),
        TypeError::Unknown { span } => CompilerDiagnostic::new(
            "E999",
            "type checking failed",
            error.to_string(),
            Some(*span),
        ),
    }
}

fn convert_lowering_error(
    error: &LoweringError,
    types: &TypeArena,
    parser: &Parser<'_, '_, '_>,
) -> CompilerDiagnostic {
    match error {
        LoweringError::UnknownIdentifier { name, span } => CompilerDiagnostic::new(
            "I001",
            "unknown identifier during IR lowering",
            format!("`{}` could not be resolved", render_symbol(*name, parser)),
            Some(*span),
        ),
        LoweringError::MissingType { node, span } => CompilerDiagnostic::new(
            "I002",
            "missing type information",
            format!("node {} has no type entry", node.as_u32()),
            Some(*span),
        ),
        LoweringError::UnknownStruct { name, span } => CompilerDiagnostic::new(
            "I003",
            "unknown struct during IR lowering",
            format!("`{}` could not be resolved", render_symbol(*name, parser)),
            Some(*span),
        ),
        LoweringError::UnknownField {
            struct_name,
            field,
            span,
        } => CompilerDiagnostic::new(
            "I004",
            "unknown field during IR lowering",
            format!(
                "struct `{}` has no field `{}`",
                render_symbol(*struct_name, parser),
                render_symbol(*field, parser)
            ),
            Some(*span),
        ),
        LoweringError::NotCallable { found, span } => CompilerDiagnostic::new(
            "I005",
            "value is not callable during IR lowering",
            format!("found {}", render_type(*found, types, parser)),
            Some(*span),
        ),
        LoweringError::NotIndexable { found, span } => CompilerDiagnostic::new(
            "I006",
            "value is not indexable during IR lowering",
            format!("found {}", render_type(*found, types, parser)),
            Some(*span),
        ),
        LoweringError::InvalidAssignmentTarget { span } => CompilerDiagnostic::new(
            "I007",
            "invalid assignment target",
            error.to_string(),
            Some(*span),
        ),
        LoweringError::InvalidLoweringState { span, .. } => CompilerDiagnostic::new(
            "I008",
            "invalid lowering state",
            error.to_string(),
            Some(*span),
        ),
        _ => CompilerDiagnostic::new(
            "I999",
            "IR lowering failed",
            error.to_string(),
            error.span(),
        ),
    }
}

fn convert_codegen_error(
    error: &thagore_codegen::CodegenError,
    types: &TypeArena,
    parser: &Parser<'_, '_, '_>,
) -> CompilerDiagnostic {
    use thagore_codegen::CodegenError;

    match error {
        CodegenError::MissingType { ty, span } => CompilerDiagnostic::new(
            "C001",
            "missing LLVM type mapping",
            format!("no LLVM type mapping for {}", render_type(*ty, types, parser)),
            *span,
        ),
        CodegenError::UnknownFunction { name, span } => CompilerDiagnostic::new(
            "C002",
            "unknown function during codegen",
            format!("`{}` was not declared", render_symbol(*name, parser)),
            *span,
        ),
        CodegenError::UnknownStruct { name, span } => CompilerDiagnostic::new(
            "C003",
            "unknown struct during codegen",
            format!("`{}` was not declared", render_symbol(*name, parser)),
            *span,
        ),
        CodegenError::UnknownField {
            struct_name,
            field,
            span,
        } => CompilerDiagnostic::new(
            "C004",
            "unknown field during codegen",
            format!(
                "struct `{}` has no field `{}`",
                render_symbol(*struct_name, parser),
                render_symbol(*field, parser)
            ),
            *span,
        ),
        CodegenError::InvalidOperandType {
            expected,
            found,
            span,
            ..
        } => CompilerDiagnostic::new(
            "C005",
            "invalid codegen operand type",
            format!(
                "expected {expected}, found {}",
                render_type(*found, types, parser)
            ),
            *span,
        ),
        CodegenError::InvalidBranchCondition { found, span, .. } => CompilerDiagnostic::new(
            "C006",
            "invalid branch condition",
            format!("expected bool, found {}", render_type(*found, types, parser)),
            *span,
        ),
        CodegenError::OutputFailed { .. } | CodegenError::LinkFailed { .. } => {
            CompilerDiagnostic::new("C007", "failed to emit output artifact", error.to_string(), None)
        }
        _ => CompilerDiagnostic::new("C999", "code generation failed", error.to_string(), error.span()),
    }
}

fn render_type(ty: TypeId, arena: &TypeArena, parser: &Parser<'_, '_, '_>) -> String {
    match arena.kind(ty) {
        TypeKind::Unit => "()".to_string(),
        TypeKind::Unknown => "<unknown>".to_string(),
        TypeKind::I32 => "i32".to_string(),
        TypeKind::I64 => "i64".to_string(),
        TypeKind::F64 => "f64".to_string(),
        TypeKind::Bool => "bool".to_string(),
        TypeKind::Str => "str".to_string(),
        TypeKind::Struct(struct_ty) => render_symbol(struct_ty.name, parser),
        TypeKind::Array(element) => format!("[{}]", render_type(*element, arena, parser)),
        TypeKind::Function(function) => {
            let params = function
                .params
                .iter()
                .map(|param| render_type(*param, arena, parser))
                .collect::<Vec<_>>()
                .join(", ");
            format!(
                "func({params}) -> {}",
                render_type(function.return_type, arena, parser)
            )
        }
        TypeKind::Infer(index) => format!("_t{index}"),
        TypeKind::IntInfer(index) => format!("_i{index}"),
    }
}

fn render_symbol(symbol: InternedStr, parser: &Parser<'_, '_, '_>) -> String {
    parser
        .resolve_symbol(symbol)
        .map(ToString::to_string)
        .unwrap_or_else(|| format!("sym_{}", symbol.as_u32()))
}

fn intern_owned_cached(
    parser: &mut Parser<'_, '_, '_>,
    cache: &mut BTreeMap<String, InternedStr>,
    text: &str,
) -> InternedStr {
    if let Some(symbol) = cache.get(text).copied() {
        return symbol;
    }

    let owned = text.to_string();
    let symbol = parser.intern_text(Box::leak(owned.clone().into_boxed_str()));
    cache.insert(owned, symbol);
    symbol
}

fn collect_symbol_texts(
    decls: &[Decl<'_>],
    parser: &Parser<'_, '_, '_>,
) -> BTreeMap<InternedStr, String> {
    let mut collector = SymbolCollector::default();
    for decl in decls {
        walk_decl(&mut collector, decl);
    }

    collector
        .symbols
        .into_iter()
        .filter_map(|symbol| parser.resolve_symbol(symbol).map(|text| (symbol, text.to_string())))
        .collect()
}

fn top_level_symbols<'ast>(decls: &[Decl<'ast>]) -> BTreeSet<InternedStr> {
    let mut symbols = BTreeSet::new();
    for decl in decls {
        match decl {
            Decl::Func(node) => {
                symbols.insert(node.name);
            }
            Decl::Let(node) => {
                symbols.insert(node.name);
            }
            Decl::Struct(node) => {
                symbols.insert(node.name);
            }
            Decl::Intent(node) => {
                symbols.insert(node.name);
            }
            Decl::Flow(node) => {
                symbols.insert(node.name);
            }
            Decl::Impl(_) | Decl::Import(_) | Decl::Extern(_) => {}
        }
    }
    symbols
}

fn register_symbols(
    decls: &[Decl<'_>],
    parser: &Parser<'_, '_, '_>,
    checker: &mut TypeChecker,
    mut codegen: Option<&mut Codegen>,
) {
    let mut collector = SymbolCollector::default();
    for decl in decls {
        walk_decl(&mut collector, decl);
    }

    for symbol in collector.symbols {
        let Some(name) = parser.resolve_symbol(symbol) else {
            continue;
        };
        checker.register_symbol_name(symbol, name);
        if let Some(codegen) = codegen.as_deref_mut() {
            codegen.register_symbol_name(symbol, name);
        }
    }
}

#[derive(Debug, Default)]
struct SymbolCollector {
    symbols: BTreeSet<InternedStr>,
}

impl SymbolCollector {
    fn record(&mut self, symbol: InternedStr) {
        self.symbols.insert(symbol);
    }
}

impl<'ast> Visitor<'ast> for SymbolCollector {
    fn visit_func_decl(&mut self, decl: &'ast thagore_ast::FuncDecl<'ast>) {
        self.record(decl.name);
    }

    fn visit_let_decl(&mut self, decl: &'ast thagore_ast::LetDecl<'ast>) {
        self.record(decl.name);
    }

    fn visit_struct_decl(&mut self, decl: &'ast thagore_ast::StructDecl<'ast>) {
        self.record(decl.name);
    }

    fn visit_impl_block(&mut self, decl: &'ast thagore_ast::ImplBlock<'ast>) {
        self.record(decl.target);
    }

    fn visit_import_decl(&mut self, decl: &'ast thagore_ast::ImportDecl<'ast>) {
        for segment in decl.path_segments {
            self.record(*segment);
        }
        if let Some(alias) = decl.alias {
            self.record(alias);
        }
    }

    fn visit_extern_decl(&mut self, decl: &'ast thagore_ast::ExternDecl<'ast>) {
        self.record(decl.name);
    }

    fn visit_intent_decl(&mut self, decl: &'ast thagore_ast::IntentDecl<'ast>) {
        self.record(decl.name);
    }

    fn visit_flow_decl(&mut self, decl: &'ast thagore_ast::FlowDecl<'ast>) {
        self.record(decl.name);
    }

    fn visit_param(&mut self, param: &'ast thagore_ast::Param<'ast>) {
        self.record(param.name);
    }

    fn visit_field_def(&mut self, field: &'ast thagore_ast::FieldDef<'ast>) {
        self.record(field.name);
    }

    fn visit_flow_stage(&mut self, stage: &'ast thagore_ast::FlowStage<'ast>) {
        self.record(stage.name);
    }

    fn visit_ident_expr(&mut self, expr: &'ast IdentExpr) {
        self.record(expr.name);
    }

    fn visit_field_access_expr(&mut self, expr: &'ast FieldAccessExpr<'ast>) {
        self.record(expr.field);
    }

    fn visit_lit_expr(&mut self, expr: &'ast LitExpr) {
        if let Literal::Str(symbol) = expr.literal {
            self.record(symbol);
        }
    }

    fn visit_named_type_expr(&mut self, ty: &'ast NamedTypeExpr) {
        self.record(ty.name);
    }

    fn visit_generic_type_expr(&mut self, ty: &'ast GenericTypeExpr<'ast>) {
        self.record(ty.name);
    }
}
