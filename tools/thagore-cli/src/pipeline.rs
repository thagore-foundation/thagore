//! End-to-end compiler pipeline orchestration for the Thagore CLI.

use std::boxed::Box;
use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet};
use std::fs;
use std::path::{Path, PathBuf};
use std::time::Instant;

use bumpalo::Bump;
use tempfile::NamedTempFile;
use thagore_ast::visitor::{Visitor, walk_decl};
use thagore_ast::{
    AssignExpr, BinaryExpr, Block, BreakStmt, CallExpr, ConstDecl, Constraint, ContinueStmt, Decl,
    Expr, ExprStmt, ExternDecl, FieldAccessExpr, FieldDef, FlowDecl, FlowStage, ForStmt, FuncDecl,
    GenericFuncDecl, GenericImplBlock, GenericStructDecl, GenericTypeExpr, IdentExpr, IfStmt,
    ImplBlock, IntentDecl, InternedStr, LetDecl, LitExpr, Literal, NamedTypeExpr, NodeId, Param,
    ReturnStmt, Span, Stmt, StructDecl, TypeExpr, TypeParam, UnaryExpr, WhileStmt,
};
use thagore_codegen::{
    Codegen, CodegenOptions, DebugOptions, OptimizationLevel, OutputArtifacts, OutputConfig,
    TargetMachineConfig, link_binary,
};
use thagore_ir::{IrLowerer, LoweringError};
use thagore_lexer::Lexer;
use thagore_parser::{ConditionDelimiter, ErrorKind, ParseError, Parser};
use thagore_typeck::{TypeArena, TypeChecker, TypeError, TypeId, TypeKind};

use crate::builtins::{
    builtin_suggestion, prepend_builtin_externs, register_builtin_runtime_symbols,
};
use crate::cli::{BuildOptions, EmitKind, OptLevel, RunOptions};
use crate::error::CompilerDiagnostic;
use crate::timer::TimingReport;

pub(crate) const MODULE_SYMBOL: InternedStr = InternedStr::new(u32::MAX - 32);

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
pub(crate) fn legacy_check_file(
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

    let mut decls = load_program(
        path,
        include_dirs,
        &mut parser,
        &arena,
        decls,
        &source,
        &timings,
    )?;
    let mut intern_cache = HashMap::new();
    let mut next_id = max_existing_node_id(&decls).saturating_add(1);
    let _builtin_bindings = prepend_builtin_externs(
        &arena,
        &mut parser,
        &mut intern_cache,
        &mut next_id,
        &mut decls,
    );
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
pub(crate) fn legacy_build_file(
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
    let mut intern_cache = HashMap::new();
    let mut next_id = max_existing_node_id(&decls).saturating_add(1);
    let builtin_bindings = prepend_builtin_externs(
        &arena,
        &mut parser,
        &mut intern_cache,
        &mut next_id,
        &mut decls,
    );
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
    register_builtin_runtime_symbols(&mut codegen, &builtin_bindings);

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
    for instance in checker.monomorph_instances() {
        if let Some(name) = checker.resolve_symbol_name(instance.result.mangled_name) {
            codegen.register_symbol_name(instance.result.mangled_name, name);
        }
    }
    let stage = Instant::now();
    let mut lowerer = IrLowerer::new(MODULE_SYMBOL, &types, &table, checker.monomorph_instances());
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
        legacy_flatten: options.legacy_flatten,
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

pub(crate) fn module_name_from_path(path: &Path) -> String {
    path.file_stem()
        .and_then(|stem| stem.to_str())
        .filter(|stem| !stem.is_empty())
        .unwrap_or("module")
        .to_string()
}

pub(crate) fn default_binary_output(path: &Path) -> PathBuf {
    let stem = path
        .file_stem()
        .and_then(|stem| stem.to_str())
        .filter(|stem| !stem.is_empty())
        .unwrap_or("a.out");
    let mut output = path.parent().unwrap_or_else(|| Path::new(".")).join(stem);
    if cfg!(windows) && output.extension().is_none() {
        output.set_extension("exe");
    }
    output
}

#[cfg(test)]
mod tests {
    use super::default_binary_output;
    use std::path::Path;

    #[test]
    fn derives_output_name_from_source_stem() {
        let output = default_binary_output(Path::new("examples/hello.tg"));
        if cfg!(windows) {
            assert_eq!(output, Path::new("examples/hello.exe"));
        } else {
            assert_eq!(output, Path::new("examples/hello"));
        }
    }

    #[test]
    fn falls_back_to_current_directory_for_bare_file_names() {
        let output = default_binary_output(Path::new("hello.tg"));
        if cfg!(windows) {
            assert_eq!(output, Path::new("hello.exe"));
        } else {
            assert_eq!(output, Path::new("hello"));
        }
    }
}

pub(crate) fn inject_compile_time_bindings<'src, 'tok, 'ast>(
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
pub(crate) struct ModuleAliasBinding {
    pub(crate) alias: InternedStr,
    pub(crate) namespace: String,
}

#[derive(Debug, Clone)]
pub(crate) struct DirectImportBinding {
    pub(crate) local_name: InternedStr,
    pub(crate) qualified_name: InternedStr,
}

#[derive(Debug, Clone)]
pub(crate) struct IncludeAllBinding {
    pub(crate) module_label: String,
    pub(crate) symbols: Vec<(String, InternedStr)>,
}

#[derive(Debug, Clone)]
pub(crate) struct ImportBindings {
    pub(crate) module_aliases: Vec<ModuleAliasBinding>,
    pub(crate) direct_symbols: Vec<DirectImportBinding>,
    pub(crate) include_all: Vec<IncludeAllBinding>,
}

#[derive(Debug, Clone)]
struct ResolvedModuleTarget {
    resolved_path: PathBuf,
    namespace: String,
    module_label: String,
}

#[derive(Debug, Clone)]
pub(crate) struct ExportedSymbol {
    pub(crate) local_name: String,
    pub(crate) qualified_name: String,
}

struct ModuleLoader<'a, 'src, 'tok, 'ast> {
    entry: &'a Path,
    include_dirs: &'a [PathBuf],
    parser: &'a mut Parser<'src, 'tok, 'ast>,
    arena: &'ast Bump,
    diagnostics: Vec<CompilerDiagnostic>,
    intern_cache: HashMap<String, InternedStr>,
    module_namespaces: HashMap<PathBuf, String>,
    module_exports: HashMap<PathBuf, Vec<ExportedSymbol>>,
    loaded: HashSet<PathBuf>,
    loading: HashSet<PathBuf>,
    import_resolution_cache: HashMap<String, Option<PathBuf>>,
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
            intern_cache: HashMap::new(),
            module_namespaces: HashMap::new(),
            module_exports: HashMap::new(),
            loaded: HashSet::new(),
            loading: HashSet::new(),
            import_resolution_cache: HashMap::new(),
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
                    format!(
                        "cyclic module load detected for `{}`",
                        module_path.display()
                    ),
                    None,
                )
                .with_hint(
                    "break the cycle by moving shared definitions into a lower-level module",
                ),
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

        let effective_namespace = self
            .module_namespaces
            .get(&module_path)
            .cloned()
            .or(namespace.clone())
            .unwrap_or_default();
        self.module_exports.insert(
            module_path.clone(),
            collect_module_exports(&decls, &source_symbols, &effective_namespace),
        );

        let import_bindings = self.resolve_imports(&module_path, &decls, &source_symbols);

        let mut rewriter = ModuleRewriter::new(
            self.arena,
            self.parser,
            &mut self.next_node_id,
            &mut self.intern_cache,
            &mut self.diagnostics,
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
    ) -> ImportBindings {
        let mut bindings = ImportBindings {
            module_aliases: Vec::new(),
            direct_symbols: Vec::new(),
            include_all: Vec::new(),
        };
        for decl in decls {
            let Decl::Import(import_decl) = decl else {
                continue;
            };

            let targets = self.resolve_import_targets(current_path, import_decl, source_symbols);
            for target in targets {
                if !self.loading.contains(&target.resolved_path)
                    && !self.loaded.contains(&target.resolved_path)
                {
                    self.load_imported_module(
                        target.resolved_path.clone(),
                        target.namespace.clone(),
                    );
                }

                let Some(exports) = self.module_exports.get(&target.resolved_path) else {
                    continue;
                };

                if import_decl.is_from {
                    if is_relative_module_import(import_decl) {
                        let local_alias = import_decl
                            .symbols
                            .first()
                            .and_then(|symbol| symbol.alias.or(Some(symbol.name)));
                        if let Some(alias) = local_alias {
                            bindings.module_aliases.push(ModuleAliasBinding {
                                alias,
                                namespace: target.namespace.clone(),
                            });
                        }
                        if import_decl.include_all {
                            bindings.include_all.push(include_all_binding(
                                exports,
                                target.module_label.clone(),
                                self.parser,
                                &mut self.intern_cache,
                            ));
                        }
                        continue;
                    }

                    for symbol in import_decl.symbols {
                        let local_name = symbol.alias.unwrap_or(symbol.name);
                        let symbol_name = source_symbols
                            .get(&symbol.name)
                            .cloned()
                            .unwrap_or_else(|| format!("sym_{}", symbol.name.as_u32()));
                        if let Some(export) =
                            exports.iter().find(|item| item.local_name == symbol_name)
                        {
                            bindings.direct_symbols.push(DirectImportBinding {
                                local_name,
                                qualified_name: intern_owned_cached(
                                    self.parser,
                                    &mut self.intern_cache,
                                    &export.qualified_name,
                                ),
                            });
                        } else {
                            self.diagnostics.push(
                                CompilerDiagnostic::new(
                                    "CLI003",
                                    "unresolved imported symbol",
                                    format!(
                                        "module `{}` has no symbol `{}`",
                                        target.module_label, symbol_name
                                    ),
                                    Some(import_decl.span),
                                )
                                .with_hint("check the exported symbol name or import the module namespace instead"),
                            );
                        }
                    }
                    if import_decl.include_all {
                        bindings.include_all.push(include_all_binding(
                            exports,
                            target.module_label.clone(),
                            self.parser,
                            &mut self.intern_cache,
                        ));
                    }
                    continue;
                }

                let alias = import_decl
                    .alias
                    .or_else(|| import_decl.path_segments.last().copied())
                    .unwrap_or_else(|| InternedStr::new(u32::MAX));
                bindings.module_aliases.push(ModuleAliasBinding {
                    alias,
                    namespace: target.namespace.clone(),
                });
                if import_decl.include_all {
                    bindings.include_all.push(include_all_binding(
                        exports,
                        target.module_label.clone(),
                        self.parser,
                        &mut self.intern_cache,
                    ));
                }
            }
        }
        bindings
    }

    fn resolve_import_path_cached(
        &mut self,
        current_path: &Path,
        relative_level: u8,
        segments: &[String],
    ) -> Option<PathBuf> {
        let key = format!(
            "{}::{}::{}",
            current_path.display(),
            relative_level,
            segments.join(".")
        );
        if let Some(cached) = self.import_resolution_cache.get(&key) {
            return cached.clone();
        }

        let resolved = resolve_import_path(
            self.entry,
            current_path,
            self.include_dirs,
            relative_level,
            segments,
        )
        .map(|path| normalize_path(&path));
        self.import_resolution_cache.insert(key, resolved.clone());
        resolved
    }

    fn resolve_import_targets(
        &mut self,
        current_path: &Path,
        import_decl: &thagore_ast::ImportDecl<'ast>,
        source_symbols: &BTreeMap<InternedStr, String>,
    ) -> Vec<ResolvedModuleTarget> {
        let mut targets = Vec::new();

        if is_relative_module_import(import_decl) {
            for symbol in import_decl.symbols {
                let segment = source_symbols
                    .get(&symbol.name)
                    .cloned()
                    .unwrap_or_else(|| format!("sym_{}", symbol.name.as_u32()));
                let path_segments = vec![segment.clone()];
                if let Some(resolved_path) = self.resolve_import_path_cached(
                    current_path,
                    import_decl.relative_level,
                    &path_segments,
                ) {
                    let namespace = self
                        .module_namespaces
                        .get(&resolved_path)
                        .cloned()
                        .unwrap_or_else(|| module_namespace(&path_segments));
                    self.module_namespaces
                        .entry(resolved_path.clone())
                        .or_insert_with(|| namespace.clone());
                    targets.push(ResolvedModuleTarget {
                        resolved_path,
                        namespace,
                        module_label: segment,
                    });
                } else {
                    self.diagnostics.push(
                        CompilerDiagnostic::new(
                            "CLI003",
                            "unresolved import",
                            format!("could not resolve relative module `{segment}`"),
                            Some(import_decl.span),
                        )
                        .with_hint("check the relative module path and filename"),
                    );
                }
            }
            return targets;
        }

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
        let Some(resolved_path) = self.resolve_import_path_cached(
            current_path,
            import_decl.relative_level,
            &module_segments,
        ) else {
            self.diagnostics.push(
                CompilerDiagnostic::new(
                    "CLI003",
                    "unresolved import",
                    format!(
                        "could not resolve import `{}`",
                        import_path_label(import_decl, &module_segments)
                    ),
                    Some(import_decl.span),
                )
                .with_hint("check stdlib, dependency include dirs, and project module paths"),
            );
            return targets;
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
        targets.push(ResolvedModuleTarget {
            resolved_path: normalized,
            namespace,
            module_label: module_label_from_segments(&module_segments),
        });
        targets
    }
}

fn resolve_import_path(
    entry: &Path,
    current_path: &Path,
    include_dirs: &[PathBuf],
    relative_level: u8,
    segments: &[String],
) -> Option<PathBuf> {
    if relative_level > 0 {
        return resolve_relative_import(current_path, relative_level, segments);
    }

    let stdlib = stdlib_root();
    if segments.first().map(String::as_str) == Some("std") && segments.len() > 1 {
        if let Some(path) = find_module_candidates(&stdlib, &segments[1..]) {
            return Some(path);
        }
    }
    if let Some(path) = find_module_candidates(&stdlib, segments) {
        return Some(path);
    }

    for root in include_dirs {
        if let Some(path) = find_module_candidates(root, segments) {
            return Some(path);
        }
        let src_root = root.join("src");
        if let Some(path) = find_module_candidates(&src_root, segments) {
            return Some(path);
        }
    }

    let project_root = project_root(entry);
    if segments.len() == 1 {
        let src_root = project_root.join("src");
        if let Some(path) = find_module_candidates(&src_root, segments) {
            return Some(path);
        }
    }

    if let Some(path) = find_module_candidates(&project_root, segments) {
        return Some(path);
    }

    if segments.first().map(String::as_str) != Some("src") {
        let src_root = project_root.join("src");
        if let Some(path) = find_module_candidates(&src_root, segments) {
            return Some(path);
        }
    }

    None
}

fn resolve_relative_import(
    current_path: &Path,
    relative_level: u8,
    segments: &[String],
) -> Option<PathBuf> {
    if segments.is_empty() {
        return None;
    }

    let mut base = current_path.parent()?.to_path_buf();
    for _ in 1..relative_level {
        base = base.parent()?.to_path_buf();
    }
    find_module_candidates(&base, segments)
}

fn find_module_candidates(root: &Path, segments: &[String]) -> Option<PathBuf> {
    if segments.is_empty() {
        return None;
    }

    let candidate = segments
        .iter()
        .fold(root.to_path_buf(), |path, segment| path.join(segment));
    let file = candidate.with_extension("tg");
    if file.is_file() {
        return Some(file);
    }

    let nested_main = candidate.join("main.tg");
    if nested_main.is_file() {
        return Some(nested_main);
    }

    None
}

pub(crate) fn stdlib_root() -> PathBuf {
    if let Some(configured) = std::env::var_os("THAGORE_STDLIB") {
        return PathBuf::from(configured);
    }
    if let Some(home) = std::env::var_os("THAGORE_HOME") {
        let candidate = PathBuf::from(home).join("share/thagore/stdlib");
        if candidate.is_dir() {
            return candidate;
        }
    }
    if let Ok(executable) = std::env::current_exe() {
        if let Some(bin_dir) = executable.parent() {
            let installed = bin_dir
                .parent()
                .map(|prefix| prefix.join("share/thagore/stdlib"));
            if let Some(candidate) = installed.filter(|candidate| candidate.is_dir()) {
                return candidate;
            }
            let sibling = bin_dir.join("../share/thagore/stdlib");
            if sibling.is_dir() {
                return sibling;
            }
        }
    }
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../stdlib")
}

pub(crate) fn project_root(entry: &Path) -> PathBuf {
    let normalized = normalize_path(entry);
    let mut ancestors = normalized.ancestors().peekable();
    while let Some(ancestor) = ancestors.next() {
        if ancestor.file_name().and_then(|name| name.to_str()) == Some("src") {
            if let Some(parent) = ancestor.parent() {
                return parent.to_path_buf();
            }
        }
    }
    normalized
        .parent()
        .and_then(Path::parent)
        .unwrap_or_else(|| Path::new("."))
        .to_path_buf()
}

pub(crate) fn normalize_path(path: &Path) -> PathBuf {
    path.canonicalize().unwrap_or_else(|_| path.to_path_buf())
}

pub(crate) fn module_namespace(segments: &[String]) -> String {
    segments.join("__")
}

pub(crate) fn module_label_from_segments(segments: &[String]) -> String {
    segments
        .last()
        .cloned()
        .unwrap_or_else(|| "__module__".to_string())
}

pub(crate) fn import_path_label(
    import_decl: &thagore_ast::ImportDecl<'_>,
    segments: &[String],
) -> String {
    let mut label = String::new();
    for _ in 0..import_decl.relative_level {
        label.push('.');
    }
    if !segments.is_empty() {
        if !label.is_empty() && !label.ends_with('.') {
            label.push('.');
        }
        label.push_str(&segments.join("."));
    }
    if label.is_empty() {
        "<empty>".to_string()
    } else {
        label
    }
}

pub(crate) fn is_relative_module_import(import_decl: &thagore_ast::ImportDecl<'_>) -> bool {
    import_decl.is_from && import_decl.relative_level > 0 && import_decl.path_segments.is_empty()
}

pub(crate) fn collect_module_exports(
    decls: &[Decl<'_>],
    source_symbols: &BTreeMap<InternedStr, String>,
    namespace: &str,
) -> Vec<ExportedSymbol> {
    let mut exports = Vec::new();
    for symbol in top_level_symbols(decls) {
        let local_name = source_symbols
            .get(&symbol)
            .cloned()
            .unwrap_or_else(|| format!("sym_{}", symbol.as_u32()));
        let qualified_name = if namespace.is_empty() {
            local_name.clone()
        } else {
            format!("{namespace}__{local_name}")
        };
        exports.push(ExportedSymbol {
            local_name,
            qualified_name,
        });
    }
    exports
}

pub(crate) fn include_all_binding(
    exports: &[ExportedSymbol],
    module_label: String,
    parser: &mut Parser<'_, '_, '_>,
    intern_cache: &mut HashMap<String, InternedStr>,
) -> IncludeAllBinding {
    IncludeAllBinding {
        module_label,
        symbols: exports
            .iter()
            .map(|export| {
                (
                    export.local_name.clone(),
                    intern_owned_cached(parser, intern_cache, &export.qualified_name),
                )
            })
            .collect(),
    }
}

pub(crate) struct ModuleRewriter<'a, 'src, 'tok, 'ast> {
    arena: &'ast Bump,
    parser: &'a mut Parser<'src, 'tok, 'ast>,
    next_node_id: &'a mut u32,
    intern_cache: &'a mut HashMap<String, InternedStr>,
    diagnostics: &'a mut Vec<CompilerDiagnostic>,
    source_symbols: &'a BTreeMap<InternedStr, String>,
    import_namespaces: HashMap<InternedStr, String>,
    direct_imports: HashMap<InternedStr, InternedStr>,
    include_all_symbols: HashMap<String, Vec<(String, InternedStr)>>,
    top_level_renames: HashMap<InternedStr, InternedStr>,
    canonical_symbols: HashMap<InternedStr, InternedStr>,
    scope_bindings: Vec<Vec<InternedStr>>,
    local_bindings: HashMap<InternedStr, usize>,
}

impl<'a, 'src, 'tok, 'ast> ModuleRewriter<'a, 'src, 'tok, 'ast> {
    pub(crate) fn new(
        arena: &'ast Bump,
        parser: &'a mut Parser<'src, 'tok, 'ast>,
        next_node_id: &'a mut u32,
        intern_cache: &'a mut HashMap<String, InternedStr>,
        diagnostics: &'a mut Vec<CompilerDiagnostic>,
        source_symbols: &'a BTreeMap<InternedStr, String>,
        decls: &[Decl<'ast>],
        namespace: Option<&str>,
        imports: &ImportBindings,
    ) -> Self {
        let mut import_namespaces = HashMap::new();
        for import in &imports.module_aliases {
            import_namespaces.insert(import.alias, import.namespace.clone());
        }
        let direct_imports = imports
            .direct_symbols
            .iter()
            .map(|binding| (binding.local_name, binding.qualified_name))
            .collect::<HashMap<_, _>>();
        let mut include_all_symbols: HashMap<String, Vec<(String, InternedStr)>> = HashMap::new();
        for import in &imports.include_all {
            for (name, qualified) in &import.symbols {
                include_all_symbols
                    .entry(name.clone())
                    .or_default()
                    .push((import.module_label.clone(), *qualified));
            }
        }

        let mut top_level_renames = HashMap::new();
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
            diagnostics,
            source_symbols,
            import_namespaces,
            direct_imports,
            include_all_symbols,
            top_level_renames,
            canonical_symbols: HashMap::new(),
            scope_bindings: Vec::new(),
            local_bindings: HashMap::new(),
        }
    }

    pub(crate) fn rewrite_program(&mut self, decls: &[Decl<'ast>]) -> Vec<Decl<'ast>> {
        decls
            .iter()
            .filter_map(|decl| self.rewrite_decl(decl))
            .collect()
    }

    fn rewrite_decl(&mut self, decl: &Decl<'ast>) -> Option<Decl<'ast>> {
        match decl {
            Decl::Import(_) => None,
            Decl::Func(node) => Some(Decl::Func(self.rewrite_func_decl(node))),
            Decl::GenericFunc(node) => {
                Some(Decl::GenericFunc(self.rewrite_generic_func_decl(node)))
            }
            Decl::Let(node) => Some(Decl::Let(self.rewrite_let_decl(node, true))),
            Decl::Const(node) => Some(Decl::Const(self.rewrite_const_decl(node, true))),
            Decl::Struct(node) => Some(Decl::Struct(self.rewrite_struct_decl(node))),
            Decl::GenericStruct(node) => {
                Some(Decl::GenericStruct(self.rewrite_generic_struct_decl(node)))
            }
            Decl::Impl(node) => Some(Decl::Impl(self.rewrite_impl_block(node))),
            Decl::GenericImpl(node) => {
                Some(Decl::GenericImpl(self.rewrite_generic_impl_block(node)))
            }
            Decl::Extern(node) => Some(Decl::Extern(self.rewrite_extern_decl(node))),
            Decl::Intent(node) => Some(Decl::Intent(self.rewrite_intent_decl(node))),
            Decl::Flow(node) => Some(Decl::Flow(self.rewrite_flow_decl(node))),
        }
    }

    fn builtin_io_symbol(&mut self, field: InternedStr) -> Option<InternedStr> {
        match self.text_of(field) {
            "print" | "println" | "eprint" | "eprintln" | "flush" => Some(field),
            _ => None,
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

    fn rewrite_const_decl(
        &mut self,
        decl: &ConstDecl<'ast>,
        is_top_level: bool,
    ) -> ConstDecl<'ast> {
        let value = self.rewrite_expr(decl.value);
        let type_ann = self.rewrite_type_expr(decl.type_ann);
        let name = if is_top_level {
            self.rename_top_level_symbol(decl.name)
        } else {
            self.canonical_symbol(decl.name)
        };
        ConstDecl {
            id: self.new_node_id(),
            span: decl.span,
            name,
            type_ann,
            value,
        }
    }

    fn rewrite_generic_func_decl(&mut self, decl: &GenericFuncDecl<'ast>) -> GenericFuncDecl<'ast> {
        self.push_scope();
        let type_params = decl
            .type_params
            .iter()
            .map(|param| self.rewrite_type_param(param))
            .collect::<Vec<_>>();
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
        GenericFuncDecl {
            id: self.new_node_id(),
            span: decl.span,
            name: self.rename_top_level_symbol(decl.name),
            type_params: self.arena.alloc_slice_fill_iter(type_params),
            params: self.arena.alloc_slice_fill_iter(params),
            return_type,
            body,
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

    fn rewrite_generic_struct_decl(
        &mut self,
        decl: &GenericStructDecl<'ast>,
    ) -> GenericStructDecl<'ast> {
        let type_params = decl
            .type_params
            .iter()
            .map(|param| self.rewrite_type_param(param))
            .collect::<Vec<_>>();
        let fields = decl
            .fields
            .iter()
            .map(|field| self.rewrite_field_def(field))
            .collect::<Vec<_>>();
        GenericStructDecl {
            id: self.new_node_id(),
            span: decl.span,
            name: self.rename_top_level_symbol(decl.name),
            type_params: self.arena.alloc_slice_fill_iter(type_params),
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

    fn rewrite_generic_impl_block(
        &mut self,
        decl: &GenericImplBlock<'ast>,
    ) -> GenericImplBlock<'ast> {
        let type_params = decl
            .type_params
            .iter()
            .map(|param| self.rewrite_type_param(param))
            .collect::<Vec<_>>();
        let methods = decl
            .methods
            .iter()
            .map(|method| self.rewrite_func_decl(method))
            .collect::<Vec<_>>();
        GenericImplBlock {
            id: self.new_node_id(),
            span: decl.span,
            target: self.rename_top_level_symbol(decl.target),
            type_params: self.arena.alloc_slice_fill_iter(type_params),
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

    fn rewrite_type_param(&mut self, param: &TypeParam<'ast>) -> TypeParam<'ast> {
        let constraints = param
            .constraints
            .iter()
            .map(|constraint| Constraint {
                id: self.new_node_id(),
                span: constraint.span,
                kind: constraint.kind,
            })
            .collect::<Vec<_>>();
        TypeParam {
            id: self.new_node_id(),
            span: param.span,
            name: self.canonical_symbol(param.name),
            constraints: self.arena.alloc_slice_fill_iter(constraints),
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
                        if let Some(symbol) = self.builtin_io_symbol(node.field) {
                            Expr::Ident(IdentExpr {
                                id: self.new_node_id(),
                                span: node.span,
                                name: symbol,
                            })
                        } else if let Some(namespace) =
                            self.import_namespaces.get(&base.name).cloned()
                        {
                            let symbol = intern_owned_cached(
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
                name: self.rewrite_identifier(node.name, Some(node.span)),
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

    fn rewrite_identifier(&mut self, symbol: InternedStr, span: Option<Span>) -> InternedStr {
        if self.is_local(symbol) {
            self.canonical_symbol(symbol)
        } else if let Some(renamed) = self.top_level_renames.get(&symbol).copied() {
            renamed
        } else if let Some(imported) = self.direct_imports.get(&symbol).copied() {
            imported
        } else if let Some(include_all) = self.include_all_symbols.get(self.text_of(symbol)) {
            if include_all.len() == 1 {
                include_all[0].1
            } else {
                self.diagnostics.push(
                    CompilerDiagnostic::new(
                        "CLI009",
                        "ambiguous symbol",
                        format!(
                            "`{}` is imported from: {}",
                            self.text_of(symbol),
                            include_all
                                .iter()
                                .map(|(module, _)| module.as_str())
                                .collect::<Vec<_>>()
                                .join(", ")
                        ),
                        span,
                    )
                    .with_hint(format!(
                        "use {}",
                        include_all
                            .iter()
                            .map(|(module, _)| format!("{module}.{}", self.text_of(symbol)))
                            .collect::<Vec<_>>()
                            .join(" or ")
                    )),
                );
                self.canonical_symbol(symbol)
            }
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
        if let Some(canonical) = self.canonical_symbols.get(&symbol).copied() {
            return canonical;
        }

        let text = self.text_of(symbol).to_string();
        let canonical = intern_owned_cached(self.parser, self.intern_cache, &text);
        self.canonical_symbols.insert(symbol, canonical);
        canonical
    }

    fn text_of(&self, symbol: InternedStr) -> &str {
        self.source_symbols
            .get(&symbol)
            .map(String::as_str)
            .unwrap_or("__error__")
    }

    fn push_scope(&mut self) {
        self.scope_bindings.push(Vec::new());
    }

    fn pop_scope(&mut self) {
        if let Some(bindings) = self.scope_bindings.pop() {
            for symbol in bindings {
                if let Some(count) = self.local_bindings.get_mut(&symbol) {
                    *count = count.saturating_sub(1);
                    if *count == 0 {
                        self.local_bindings.remove(&symbol);
                    }
                }
            }
        }
    }

    fn define_local(&mut self, symbol: InternedStr) {
        if let Some(scope) = self.scope_bindings.last_mut() {
            scope.push(symbol);
            *self.local_bindings.entry(symbol).or_insert(0) += 1;
        }
    }

    fn is_local(&self, symbol: InternedStr) -> bool {
        self.local_bindings.contains_key(&symbol)
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
    make_literal_binding(parser, arena, name, Literal::Bool(value), "bool", next_id)
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
        return make_literal_binding(parser, arena, name, Literal::Int(int_value), "i32", next_id);
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

pub(crate) fn next_synthetic_node(next_id: &mut u32) -> NodeId {
    let id = NodeId::new(*next_id);
    *next_id = next_id.saturating_add(1);
    id
}

pub(crate) fn max_existing_node_id<'ast>(decls: &[Decl<'ast>]) -> u32 {
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
pub(crate) struct RequestedOutputs {
    pub(crate) ll: bool,
    pub(crate) bc: bool,
    pub(crate) obj: bool,
    pub(crate) bin: bool,
}

impl RequestedOutputs {
    pub(crate) fn new(requested: &[EmitKind], force_bin: bool) -> Self {
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

pub(crate) fn convert_parse_error(error: &ParseError) -> CompilerDiagnostic {
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

pub(crate) fn convert_type_error(
    error: &TypeError,
    types: &TypeArena,
    parser: &Parser<'_, '_, '_>,
) -> CompilerDiagnostic {
    match error {
        TypeError::UnsupportedFeature { feature, span } => CompilerDiagnostic::new(
            "E000",
            "unsupported language feature",
            format!("`{feature}` is parsed but not implemented end to end yet"),
            Some(*span),
        ),
        TypeError::InvalidControlFlow { message, span } => CompilerDiagnostic::new(
            "E003",
            "invalid control flow",
            (*message).to_string(),
            Some(*span),
        ),
        TypeError::InvalidAssignmentTarget { span } => CompilerDiagnostic::new(
            "E004",
            "invalid assignment target",
            "only variables and direct struct fields can be assigned to".to_string(),
            Some(*span),
        ),
        TypeError::InvalidConstInitializer { span } => CompilerDiagnostic::new(
            "E005",
            "invalid const initializer",
            "top-level const initializers must be compile-time constant expressions".to_string(),
            Some(*span),
        ),
        TypeError::UnknownType { name, span } => CompilerDiagnostic::new(
            "E006",
            "unknown type",
            format!(
                "`{}` is not a builtin type or declared struct",
                render_symbol(*name, parser)
            ),
            Some(*span),
        ),
        TypeError::InvalidImplTarget { target, span } => CompilerDiagnostic::new(
            "E007",
            "invalid impl target",
            format!(
                "`{}` is not a declared struct, so methods cannot be attached to it",
                render_symbol(*target, parser)
            ),
            Some(*span),
        ),
        TypeError::InvalidMethodReceiver {
            method,
            expected,
            found,
            span,
        } => CompilerDiagnostic::new(
            "E008",
            "invalid impl method receiver",
            match found {
                Some(found) => format!(
                    "method `{}` must take `{}` as its first parameter, found {}",
                    render_symbol(*method, parser),
                    render_type(*expected, types, parser),
                    render_type(*found, types, parser)
                ),
                None => format!(
                    "method `{}` must declare `{}` as its first parameter",
                    render_symbol(*method, parser),
                    render_type(*expected, types, parser)
                ),
            },
            Some(*span),
        ),
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
        TypeError::UnknownIdentifier { name, span } => {
            let rendered = render_symbol(*name, parser);
            let mut diagnostic = CompilerDiagnostic::new(
                "E002",
                "unknown identifier",
                format!("`{}` is not in scope", rendered),
                Some(*span),
            );
            if let Some(suggestion) = builtin_suggestion(&rendered) {
                diagnostic = diagnostic.with_hint(format!("did you mean `{suggestion}`?"));
            }
            diagnostic
        }
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
        TypeError::NotFieldAccessible { found, span } => CompilerDiagnostic::new(
            "E010",
            "value has no fields",
            format!(
                "type {} does not support field access",
                render_type(*found, types, parser)
            ),
            Some(*span),
        ),
        TypeError::NotCallable { found, span } => CompilerDiagnostic::new(
            "E011",
            "value is not callable",
            format!(
                "type {} cannot be called",
                render_type(*found, types, parser)
            ),
            Some(*span),
        ),
        TypeError::NotIndexable { found, span } => CompilerDiagnostic::new(
            "E012",
            "value is not indexable",
            format!(
                "type {} cannot be indexed",
                render_type(*found, types, parser)
            ),
            Some(*span),
        ),
        TypeError::ArgumentCountMismatch {
            expected,
            found,
            span,
        } => CompilerDiagnostic::new(
            "E013",
            "argument count mismatch",
            format!("expected {expected} arguments, found {found}"),
            Some(*span),
        ),
        TypeError::ReturnTypeMismatch {
            expected,
            found,
            span,
        } => CompilerDiagnostic::new(
            "E014",
            "return type mismatch",
            format!(
                "expected {}, found {}",
                render_type(*expected, types, parser),
                render_type(*found, types, parser)
            ),
            Some(*span),
        ),
        TypeError::ConditionNotBool { found, span } => CompilerDiagnostic::new(
            "E015",
            "condition must be bool",
            format!("found {}", render_type(*found, types, parser)),
            Some(*span),
        ),
        TypeError::InferenceFailure { span } => CompilerDiagnostic::new(
            "E016",
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

pub(crate) fn convert_lowering_error(
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

pub(crate) fn convert_codegen_error(
    error: &thagore_codegen::CodegenError,
    types: &TypeArena,
    parser: &Parser<'_, '_, '_>,
) -> CompilerDiagnostic {
    use thagore_codegen::CodegenError;

    match error {
        CodegenError::MissingType { ty, span } => CompilerDiagnostic::new(
            "C001",
            "missing LLVM type mapping",
            format!(
                "no LLVM type mapping for {}",
                render_type(*ty, types, parser)
            ),
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
            format!(
                "expected bool, found {}",
                render_type(*found, types, parser)
            ),
            *span,
        ),
        CodegenError::OutputFailed { .. } | CodegenError::LinkFailed { .. } => {
            CompilerDiagnostic::new(
                "C007",
                "failed to emit output artifact",
                error.to_string(),
                None,
            )
        }
        _ => CompilerDiagnostic::new(
            "C999",
            "code generation failed",
            error.to_string(),
            error.span(),
        ),
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

pub(crate) fn render_symbol(symbol: InternedStr, parser: &Parser<'_, '_, '_>) -> String {
    parser
        .resolve_symbol(symbol)
        .map(ToString::to_string)
        .unwrap_or_else(|| format!("sym_{}", symbol.as_u32()))
}

pub(crate) fn intern_owned_cached(
    parser: &mut Parser<'_, '_, '_>,
    cache: &mut HashMap<String, InternedStr>,
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

pub(crate) fn collect_symbol_texts(
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
        .filter_map(|symbol| {
            parser
                .resolve_symbol(symbol)
                .map(|text| (symbol, text.to_string()))
        })
        .collect()
}

pub(crate) fn top_level_symbols<'ast>(decls: &[Decl<'ast>]) -> BTreeSet<InternedStr> {
    let mut symbols = BTreeSet::new();
    for decl in decls {
        match decl {
            Decl::Func(node) => {
                symbols.insert(node.name);
            }
            Decl::GenericFunc(node) => {
                symbols.insert(node.name);
            }
            Decl::Let(node) => {
                symbols.insert(node.name);
            }
            Decl::Const(node) => {
                symbols.insert(node.name);
            }
            Decl::Struct(node) => {
                symbols.insert(node.name);
            }
            Decl::GenericStruct(node) => {
                symbols.insert(node.name);
            }
            Decl::Intent(node) => {
                symbols.insert(node.name);
            }
            Decl::Flow(node) => {
                symbols.insert(node.name);
            }
            Decl::Impl(_) | Decl::GenericImpl(_) | Decl::Import(_) | Decl::Extern(_) => {}
        }
    }
    symbols
}

pub(crate) fn register_symbols(
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

    fn visit_generic_func_decl(&mut self, decl: &'ast thagore_ast::GenericFuncDecl<'ast>) {
        self.record(decl.name);
    }

    fn visit_let_decl(&mut self, decl: &'ast thagore_ast::LetDecl<'ast>) {
        self.record(decl.name);
    }

    fn visit_const_decl(&mut self, decl: &'ast thagore_ast::ConstDecl<'ast>) {
        self.record(decl.name);
    }

    fn visit_struct_decl(&mut self, decl: &'ast thagore_ast::StructDecl<'ast>) {
        self.record(decl.name);
    }

    fn visit_generic_struct_decl(&mut self, decl: &'ast thagore_ast::GenericStructDecl<'ast>) {
        self.record(decl.name);
    }

    fn visit_impl_block(&mut self, decl: &'ast thagore_ast::ImplBlock<'ast>) {
        self.record(decl.target);
    }

    fn visit_import_decl(&mut self, decl: &'ast thagore_ast::ImportDecl<'ast>) {
        for segment in decl.path_segments {
            self.record(*segment);
        }
        for symbol in decl.symbols {
            self.record(symbol.name);
            if let Some(alias) = symbol.alias {
                self.record(alias);
            }
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
