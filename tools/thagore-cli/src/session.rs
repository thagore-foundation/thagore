//! Session-based per-module compilation for the Thagore CLI.

use std::collections::{BTreeMap, HashMap};
use std::fs;
use std::path::{Path, PathBuf};

use bumpalo::Bump;
use sha2::{Digest, Sha256};
use thagore_ast::{
    Decl, ExternDecl, GenericTypeExpr, ImportDecl, InternedStr, NamedTypeExpr, Param, Span,
    TypeExpr, TypeExprRef,
};
use thagore_codegen::{
    emit_module, link_objects, Codegen, CodegenOptions, DebugOptions, OptimizationLevel,
    OutputArtifacts, TargetMachineConfig,
};
use thagore_ir::lower_module;
use thagore_lexer::Lexer;
use thagore_module_graph::{
    ExportedSymbol as GraphExportedSymbol, ImportSpec, ImportTable, ModuleGraph, ModuleId,
    ModulePath, ModuleResolver, ModuleSource, SymbolKind, TypeSig,
};
use thagore_parser::Parser;
use thagore_typeck::{check_module, TypeChecker};

use crate::builtins::{prepend_builtin_externs, register_builtin_runtime_symbols};
use crate::cli::{BuildOptions, EmitKind, OptLevel};
use crate::error::CompilerDiagnostic;
use crate::pipeline::{
    collect_module_exports, collect_symbol_texts, convert_codegen_error, convert_lowering_error,
    convert_parse_error, convert_type_error, default_binary_output, include_all_binding,
    inject_compile_time_bindings, intern_owned_cached, is_relative_module_import,
    legacy_build_file, legacy_check_file, max_existing_node_id, module_name_from_path,
    module_namespace, next_synthetic_node, normalize_path, register_symbols, BuildResult,
    DirectImportBinding, ExportedSymbol, ImportBindings, ModuleAliasBinding, ModuleRewriter,
    PipelineFailure, RequestedOutputs, MODULE_SYMBOL,
};
use crate::timer::TimingReport;

const SESSION_FALLBACK_CODE: &str = "CLI900";
const MODULE_CACHE_VERSION: &str = "session-v2";

/// Runs the session pipeline for `thagc check`.
pub(crate) fn check_file(
    path: &Path,
    include_dirs: &[PathBuf],
    defines: &[String],
    features: &[String],
    legacy_flatten: bool,
) -> Result<(), PipelineFailure> {
    if legacy_flatten || entry_uses_relative_imports(path) {
        return legacy_check_file(path, include_dirs, defines, features);
    }

    let options = SessionOptions::for_check(include_dirs, defines, features);
    let mut session = CompilationSession::new(path, options)?;
    match session.check_all() {
        Ok(()) => Ok(()),
        Err(failure) if should_fallback_to_legacy(&failure) => {
            legacy_check_file(path, include_dirs, defines, features)
        }
        Err(failure) => Err(failure),
    }
}

/// Runs the session pipeline for `thagc build`.
pub(crate) fn build_file(path: &Path, options: &BuildOptions) -> Result<BuildResult, PipelineFailure> {
    if options.legacy_flatten || entry_uses_relative_imports(path) {
        return legacy_build_file(path, options);
    }

    let session_options = SessionOptions::for_build(path, options);
    let mut session = CompilationSession::new(path, session_options)?;
    let objects = match session.compile_all() {
        Ok(objects) => objects,
        Err(failure) if should_fallback_to_legacy(&failure) => {
            return legacy_build_file(path, options);
        }
        Err(failure) => return Err(failure),
    };

    let mut artifacts = session.entry_artifacts.clone();
    if session.options.emit.bin {
        session.link(objects, &session.binary_output)?;
        artifacts.binary = Some(session.binary_output.clone());
    }

    if !session.options.emit.obj {
        artifacts.object = None;
    }

    Ok(BuildResult {
        artifacts,
        timings: session.timings,
    })
}

#[derive(Debug, Clone)]
struct SessionOptions {
    include_dirs: Vec<PathBuf>,
    defines: Vec<String>,
    features: Vec<String>,
    emit: RequestedOutputs,
    opt: OptLevel,
    debug: bool,
    target: Option<String>,
    binary_output: PathBuf,
}

impl SessionOptions {
    fn for_build(entry: &Path, options: &BuildOptions) -> Self {
        Self {
            include_dirs: options.include_dirs.clone(),
            defines: options.defines.clone(),
            features: options.features.clone(),
            emit: RequestedOutputs::new(&options.emit, true),
            opt: options.opt,
            debug: options.debug,
            target: options.target.clone(),
            binary_output: options
                .output
                .clone()
                .unwrap_or_else(|| default_binary_output(entry)),
        }
    }

    fn for_check(include_dirs: &[PathBuf], defines: &[String], features: &[String]) -> Self {
        Self {
            include_dirs: include_dirs.to_vec(),
            defines: defines.to_vec(),
            features: features.to_vec(),
            emit: RequestedOutputs::new(&[EmitKind::Bin], false),
            opt: OptLevel::O0,
            debug: false,
            target: None,
            binary_output: PathBuf::new(),
        }
    }
}

#[derive(Debug, Clone)]
struct CacheEntry {
    hash: String,
    object: PathBuf,
}

#[derive(Debug, Default)]
struct ModuleCache {
    entries: HashMap<PathBuf, CacheEntry>,
}

impl ModuleCache {
    fn load(path: &Path) -> Self {
        let Ok(source) = fs::read_to_string(path) else {
            return Self::default();
        };
        let mut entries = HashMap::new();
        let mut current_path: Option<PathBuf> = None;
        let mut current_hash: Option<String> = None;
        let mut current_object: Option<PathBuf> = None;

        for line in source.lines() {
            let trimmed = line.trim();
            if trimmed == "[[module]]" {
                flush_cache_entry(&mut entries, &mut current_path, &mut current_hash, &mut current_object);
                continue;
            }
            if let Some(value) = parse_cache_value(trimmed, "path") {
                current_path = Some(PathBuf::from(value));
            } else if let Some(value) = parse_cache_value(trimmed, "hash") {
                current_hash = Some(value.to_string());
            } else if let Some(value) = parse_cache_value(trimmed, "object") {
                current_object = Some(PathBuf::from(value));
            }
        }
        flush_cache_entry(&mut entries, &mut current_path, &mut current_hash, &mut current_object);
        Self { entries }
    }

    fn lookup(&self, path: &Path, hash: &str) -> Option<PathBuf> {
        let entry = self.entries.get(path)?;
        (entry.hash == hash && entry.object.is_file()).then(|| entry.object.clone())
    }

    fn record(&mut self, path: PathBuf, hash: String, object: PathBuf) {
        self.entries.insert(path, CacheEntry { hash, object });
    }

    fn save(&self, path: &Path) -> std::io::Result<()> {
        let mut lines = Vec::new();
        let mut entries = self.entries.iter().collect::<Vec<_>>();
        entries.sort_by(|left, right| left.0.cmp(right.0));
        for (module_path, entry) in entries {
            lines.push(String::from("[[module]]"));
            lines.push(format!("path = \"{}\"", module_path.display()));
            lines.push(format!("hash = \"{}\"", entry.hash));
            lines.push(format!("object = \"{}\"", entry.object.display()));
        }
        fs::write(path, lines.join("\n"))
    }
}

fn flush_cache_entry(
    entries: &mut HashMap<PathBuf, CacheEntry>,
    current_path: &mut Option<PathBuf>,
    current_hash: &mut Option<String>,
    current_object: &mut Option<PathBuf>,
) {
    let (Some(path), Some(hash), Some(object)) =
        (current_path.take(), current_hash.take(), current_object.take())
    else {
        return;
    };
    entries.insert(path, CacheEntry { hash, object });
}

fn parse_cache_value<'a>(line: &'a str, key: &str) -> Option<&'a str> {
    let needle = format!("{key} = \"");
    let value = line.strip_prefix(&needle)?;
    value.strip_suffix('"')
}

#[derive(Debug)]
struct ImportedExtern {
    export: GraphExportedSymbol,
}

#[derive(Debug)]
struct ResolvedImports {
    bindings: ImportBindings,
    externs: Vec<ImportedExtern>,
}

#[derive(Debug)]
pub(crate) struct CompilationSession {
    entry: PathBuf,
    entry_id: ModuleId,
    graph: ModuleGraph,
    resolver: ModuleResolver,
    import_table: ImportTable,
    options: SessionOptions,
    module_ids_by_path: HashMap<PathBuf, ModuleId>,
    object_files: Vec<PathBuf>,
    build_dir: PathBuf,
    cache_path: PathBuf,
    cache: ModuleCache,
    timings: TimingReport,
    entry_artifacts: OutputArtifacts,
    binary_output: PathBuf,
}

impl CompilationSession {
    fn new(entry: &Path, options: SessionOptions) -> Result<Self, PipelineFailure> {
        let normalized_entry = normalize_path(entry);
        let project_root = crate::pipeline::project_root(&normalized_entry);
        let stdlib_root = crate::pipeline::stdlib_root();
        let mut resolver = ModuleResolver::new(stdlib_root, project_root.clone(), options.include_dirs.clone());
        let graph = ModuleGraph::build(&normalized_entry, &mut resolver).map_err(|error| {
            module_error_failure(&normalized_entry, error.to_string())
        })?;
        let entry_id = graph
            .nodes
            .iter()
            .find_map(|(id, module)| (module.file_path == normalized_entry).then_some(*id))
            .ok_or_else(|| module_error_failure(&normalized_entry, "entry module missing from graph"))?;
        let module_ids_by_path = graph
            .nodes
            .iter()
            .map(|(id, module)| (module.file_path.clone(), *id))
            .collect::<HashMap<_, _>>();
        let thagore_dir = project_root.join(".thagore");
        let build_dir = thagore_dir.join("build");
        let incremental_dir = thagore_dir.join("incremental");
        fs::create_dir_all(&build_dir).map_err(|error| module_error_failure(&normalized_entry, error.to_string()))?;
        fs::create_dir_all(&incremental_dir)
            .map_err(|error| module_error_failure(&normalized_entry, error.to_string()))?;
        let cache_path = incremental_dir.join("modules.cache");
        let cache = ModuleCache::load(&cache_path);
        let binary_output = options.binary_output.clone();

        Ok(Self {
            entry: normalized_entry,
            entry_id,
            graph,
            resolver,
            import_table: ImportTable::new(),
            options,
            module_ids_by_path,
            object_files: Vec::new(),
            build_dir,
            cache_path,
            cache,
            timings: TimingReport::new(),
            entry_artifacts: OutputArtifacts::default(),
            binary_output,
        })
    }

    pub(crate) fn compile_all(&mut self) -> Result<Vec<PathBuf>, PipelineFailure> {
        self.object_files.clear();
        for module_id in self.graph.topo_order.clone() {
            let object = self.compile_module(module_id)?;
            self.object_files.push(object);
        }
        self.cache
            .save(&self.cache_path)
            .map_err(|error| module_error_failure(&self.entry, error.to_string()))?;
        Ok(self.object_files.clone())
    }

    pub(crate) fn check_all(&mut self) -> Result<(), PipelineFailure> {
        for module_id in self.graph.topo_order.clone() {
            self.check_module(module_id)?;
        }
        Ok(())
    }

    pub(crate) fn link(&self, objects: Vec<PathBuf>, output: &Path) -> Result<(), PipelineFailure> {
        let timer = std::time::Instant::now();
        let result = link_objects(&objects, output);
        let mut timings = self.timings.clone();
        timings.record("link", timer.elapsed());
        match result {
            Ok(()) => Ok(()),
            Err(error) => Err(PipelineFailure {
                diagnostics: vec![CompilerDiagnostic::new(
                    "C007",
                    "failed to emit output artifact",
                    error.to_string(),
                    None,
                )],
                source: String::new(),
                timings,
            }),
        }
    }

    pub(crate) fn compile_module(&mut self, id: ModuleId) -> Result<PathBuf, PipelineFailure> {
        let module_path = self
            .graph
            .nodes
            .get(&id)
            .cloned()
            .ok_or_else(|| module_error_failure(&self.entry, format!("unknown module id {id}")))?;
        let source = fs::read_to_string(&module_path.file_path).map_err(|error| PipelineFailure {
            diagnostics: vec![CompilerDiagnostic::new(
                "CLI001",
                "failed to read source file",
                error.to_string(),
                None,
            )],
            source: String::new(),
            timings: self.timings.clone(),
        })?;

        let arena = Bump::new();
        let mut lexer = Lexer::new(&source);
        let stage = std::time::Instant::now();
        let tokens = lexer.lex_all_in(&arena);
        self.timings.record("lexer", stage.elapsed());

        let mut parser = Parser::new(tokens.as_slice(), &arena, lexer.interner());
        let stage = std::time::Instant::now();
        let parsed = parser.parse_program();
        let parse_errors = parser.take_errors();
        self.timings.record("parser", stage.elapsed());
        if !parse_errors.is_empty() {
            return Err(PipelineFailure {
                diagnostics: parse_errors.iter().map(convert_parse_error).collect(),
                source: source.clone(),
                timings: self.timings.clone(),
            });
        }
        if parsed
            .iter()
            .any(|decl| matches!(decl, Decl::Import(import) if import.relative_level > 0))
        {
            return Err(session_fallback_failure(
                "relative imports still use the legacy module pipeline",
                &source,
            ));
        }

        let source_symbols = collect_symbol_texts(&parsed, &parser);
        let namespace = self.module_namespace_for(id, &module_path);
        let module_label = self.module_label_for(id, &module_path);
        let exports = map_exports_with_types(
            collect_module_exports(
                &parsed,
                &source_symbols,
                if id == self.entry_id { "" } else { &namespace },
            ),
            &parsed,
            &source_symbols,
            &parser,
        );
        let mut next_node_id = max_existing_node_id(&parsed).saturating_add(1);
        let mut intern_cache = HashMap::new();
        let imports = self.resolve_imports(
            &module_path.file_path,
            &parsed,
            &source_symbols,
            &mut parser,
            &mut intern_cache,
        )?;
        let mut rewrite_diagnostics = Vec::new();
        let mut rewriter = ModuleRewriter::new(
            &arena,
            &mut parser,
            &mut next_node_id,
            &mut intern_cache,
            &mut rewrite_diagnostics,
            &source_symbols,
            &parsed,
            (id != self.entry_id).then_some(namespace.as_str()),
            &imports.bindings,
        );
        let mut decls = rewriter.rewrite_program(&parsed);
        drop(rewriter);
        if !rewrite_diagnostics.is_empty() {
            return Err(PipelineFailure {
                diagnostics: rewrite_diagnostics,
                source: source.clone(),
                timings: self.timings.clone(),
            });
        }

        prepend_import_externs(
            &arena,
            &mut parser,
            &mut intern_cache,
            &mut next_node_id,
            &imports.externs,
            &mut decls,
        );
        let builtin_bindings = prepend_builtin_externs(
            &arena,
            &mut parser,
            &mut intern_cache,
            &mut next_node_id,
            &mut decls,
        );
        inject_compile_time_bindings(
            &mut decls,
            &mut parser,
            &arena,
            &self.options.defines,
            &self.options.features,
            &source,
            &self.timings,
        )?;

        let mut checker = TypeChecker::new();
        let mut codegen = Codegen::new();
        let module_name = self.module_symbol(&mut parser, &mut intern_cache, &namespace);
        codegen.register_symbol_name(MODULE_SYMBOL, &module_name);
        register_symbols(&decls, &parser, &mut checker, Some(&mut codegen));
        register_builtin_runtime_symbols(&mut codegen, &builtin_bindings);

        let stage = std::time::Instant::now();
        let table = match check_module(&mut checker, &decls) {
            Ok(table) => table,
            Err(errors) => {
                let diagnostics = errors
                    .iter()
                    .map(|error| convert_type_error(error, checker.types(), &parser))
                    .collect();
                return Err(PipelineFailure {
                    diagnostics,
                    source: source.clone(),
                    timings: self.timings.clone(),
                });
            }
        };
        self.timings.record("typeck", stage.elapsed());

        self.import_table
            .register_module_exports(id, module_label, exports);

        let cache_hash = self.module_hash(&source, &namespace);
        let wants_entry_text = id == self.entry_id && (self.options.emit.ll || self.options.emit.bc);
        let default_object = self.object_path_for(id, &cache_hash);
        let object_path = if id == self.entry_id && self.options.emit.obj {
            self.binary_output.with_extension("o")
        } else {
            default_object
        };

        if let Some(cached_object) = self.cache.lookup(&module_path.file_path, &cache_hash) {
            if !(id == self.entry_id && wants_entry_text) {
                if id == self.entry_id {
                    self.entry_artifacts.object = Some(cached_object.clone());
                }
                return Ok(cached_object);
            }
        }

        let types = checker.types().clone();
        for instance in checker.monomorph_instances() {
            if let Some(name) = checker.resolve_symbol_name(instance.result.mangled_name) {
                codegen.register_symbol_name(instance.result.mangled_name, name);
            }
        }

        let stage = std::time::Instant::now();
        let ir_module = match lower_module(
            MODULE_SYMBOL,
            &types,
            &table,
            checker.monomorph_instances(),
            &decls,
        ) {
            Ok(module) => module,
            Err(errors) => {
                let diagnostics = errors
                    .iter()
                    .map(|error| convert_lowering_error(error, &types, &parser))
                    .collect();
                return Err(PipelineFailure {
                    diagnostics,
                    source: source.clone(),
                    timings: self.timings.clone(),
                });
            }
        };
        self.timings.record("ir lower", stage.elapsed());

        let stage = std::time::Instant::now();
        let llvm_ir_path = (id == self.entry_id && self.options.emit.ll)
            .then(|| self.binary_output.with_extension("ll"));
        let bitcode_path = (id == self.entry_id && self.options.emit.bc)
            .then(|| self.binary_output.with_extension("bc"));
        let options = self.codegen_options(&module_path.file_path);
        if let Err(errors) = emit_module(
            &mut codegen,
            options,
            &ir_module,
            &types,
            &object_path,
            llvm_ir_path.clone(),
            bitcode_path.clone(),
        ) {
            let diagnostics = errors
                .iter()
                .map(|error| convert_codegen_error(error, &types, &parser))
                .collect();
            return Err(PipelineFailure {
                diagnostics,
                source: source.clone(),
                timings: self.timings.clone(),
            });
        }
        self.timings.record("codegen", stage.elapsed());

        self.cache
            .record(module_path.file_path.clone(), cache_hash, object_path.clone());

        if id == self.entry_id {
            self.entry_artifacts.object = Some(object_path.clone());
            self.entry_artifacts.llvm_ir = llvm_ir_path;
            self.entry_artifacts.bitcode = bitcode_path;
        }

        Ok(object_path)
    }

    pub(crate) fn check_module(&mut self, id: ModuleId) -> Result<(), PipelineFailure> {
        let module_path = self
            .graph
            .nodes
            .get(&id)
            .cloned()
            .ok_or_else(|| module_error_failure(&self.entry, format!("unknown module id {id}")))?;
        let source = fs::read_to_string(&module_path.file_path).map_err(|error| PipelineFailure {
            diagnostics: vec![CompilerDiagnostic::new(
                "CLI001",
                "failed to read source file",
                error.to_string(),
                None,
            )],
            source: String::new(),
            timings: self.timings.clone(),
        })?;

        let arena = Bump::new();
        let mut lexer = Lexer::new(&source);
        let stage = std::time::Instant::now();
        let tokens = lexer.lex_all_in(&arena);
        self.timings.record("lexer", stage.elapsed());

        let mut parser = Parser::new(tokens.as_slice(), &arena, lexer.interner());
        let stage = std::time::Instant::now();
        let decls = parser.parse_program();
        let parse_errors = parser.take_errors();
        self.timings.record("parser", stage.elapsed());
        if !parse_errors.is_empty() {
            return Err(PipelineFailure {
                diagnostics: parse_errors.iter().map(convert_parse_error).collect(),
                source: source.clone(),
                timings: self.timings.clone(),
            });
        }
        if decls
            .iter()
            .any(|decl| matches!(decl, Decl::Import(import) if import.relative_level > 0))
        {
            return Err(session_fallback_failure(
                "relative imports still use the legacy module pipeline",
                &source,
            ));
        }

        let source_symbols = collect_symbol_texts(&decls, &parser);
        let namespace = self.module_namespace_for(id, &module_path);
        let module_label = self.module_label_for(id, &module_path);
        let exports = map_exports_with_types(
            collect_module_exports(
                &decls,
                &source_symbols,
                if id == self.entry_id { "" } else { &namespace },
            ),
            &decls,
            &source_symbols,
            &parser,
        );
        let mut next_node_id = max_existing_node_id(&decls).saturating_add(1);
        let mut intern_cache = HashMap::new();
        let imports = self.resolve_imports(
            &module_path.file_path,
            &decls,
            &source_symbols,
            &mut parser,
            &mut intern_cache,
        )?;
        let mut rewrite_diagnostics = Vec::new();
        let mut rewriter = ModuleRewriter::new(
            &arena,
            &mut parser,
            &mut next_node_id,
            &mut intern_cache,
            &mut rewrite_diagnostics,
            &source_symbols,
            &decls,
            (id != self.entry_id).then_some(namespace.as_str()),
            &imports.bindings,
        );
        let mut decls = rewriter.rewrite_program(&decls);
        drop(rewriter);
        if !rewrite_diagnostics.is_empty() {
            return Err(PipelineFailure {
                diagnostics: rewrite_diagnostics,
                source: source.clone(),
                timings: self.timings.clone(),
            });
        }
        prepend_import_externs(
            &arena,
            &mut parser,
            &mut intern_cache,
            &mut next_node_id,
            &imports.externs,
            &mut decls,
        );
        let _builtin_bindings = prepend_builtin_externs(
            &arena,
            &mut parser,
            &mut intern_cache,
            &mut next_node_id,
            &mut decls,
        );
        inject_compile_time_bindings(
            &mut decls,
            &mut parser,
            &arena,
            &self.options.defines,
            &self.options.features,
            &source,
            &self.timings,
        )?;

        let mut checker = TypeChecker::new();
        register_symbols(&decls, &parser, &mut checker, None);
        let stage = std::time::Instant::now();
        let table = match check_module(&mut checker, &decls) {
            Ok(table) => table,
            Err(errors) => {
                let diagnostics = errors
                    .iter()
                    .map(|error| convert_type_error(error, checker.types(), &parser))
                    .collect();
                return Err(PipelineFailure {
                    diagnostics,
                    source: source.clone(),
                    timings: self.timings.clone(),
                });
            }
        };
        let _ = table;
        self.timings.record("typeck", stage.elapsed());
        self.import_table
            .register_module_exports(id, module_label, exports);
        Ok(())
    }

    fn resolve_imports<'ast>(
        &mut self,
        current_path: &Path,
        decls: &[Decl<'ast>],
        source_symbols: &BTreeMap<InternedStr, String>,
        parser: &mut Parser<'_, '_, 'ast>,
        intern_cache: &mut HashMap<String, InternedStr>,
    ) -> Result<ResolvedImports, PipelineFailure> {
        let mut bindings = ImportBindings {
            module_aliases: Vec::new(),
            direct_symbols: Vec::new(),
            include_all: Vec::new(),
        };
        let mut externs = BTreeMap::<String, ImportedExtern>::new();

        for decl in decls {
            let Decl::Import(import_decl) = decl else {
                continue;
            };

            for target in self.resolve_import_targets(current_path, import_decl, source_symbols)? {
                let exports = self.import_table.module_exports(target.module_id);
                if exports.iter().any(|export| export.kind != SymbolKind::Function) {
                    return Err(session_fallback_failure(
                        "imports that expose consts or types still use the legacy module pipeline",
                        &fs::read_to_string(current_path).unwrap_or_default(),
                    ));
                }
                for export in exports {
                    if export.kind == SymbolKind::Function {
                        externs
                            .entry(export.qualified_name.clone())
                            .or_insert_with(|| ImportedExtern {
                                export: export.clone(),
                            });
                    }
                }

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
                                &map_graph_exports(exports),
                                target.module_label.clone(),
                                parser,
                                intern_cache,
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
                        if let Some(export) = exports.iter().find(|item| item.name == symbol_name) {
                            bindings.direct_symbols.push(DirectImportBinding {
                                local_name,
                                qualified_name: intern_owned_cached(
                                    parser,
                                    intern_cache,
                                    &export.qualified_name,
                                ),
                            });
                        } else {
                            return Err(PipelineFailure {
                                diagnostics: vec![CompilerDiagnostic::new(
                                    "CLI003",
                                    "unresolved imported symbol",
                                    format!(
                                        "module `{}` has no symbol `{}`",
                                        target.module_label, symbol_name
                                    ),
                                    Some(import_decl.span),
                                )],
                                source: fs::read_to_string(current_path).unwrap_or_default(),
                                timings: self.timings.clone(),
                            });
                        }
                    }
                    if import_decl.include_all {
                        bindings.include_all.push(include_all_binding(
                            &map_graph_exports(exports),
                            target.module_label.clone(),
                            parser,
                            intern_cache,
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
                        &map_graph_exports(exports),
                        target.module_label.clone(),
                        parser,
                        intern_cache,
                    ));
                }
            }
        }

        Ok(ResolvedImports {
            bindings,
            externs: externs.into_values().collect(),
        })
    }

    fn resolve_import_targets<'ast>(
        &mut self,
        current_path: &Path,
        import_decl: &ImportDecl<'ast>,
        source_symbols: &BTreeMap<InternedStr, String>,
    ) -> Result<Vec<ResolvedTarget>, PipelineFailure> {
        let mut targets = Vec::new();

        if is_relative_module_import(import_decl) {
            for symbol in import_decl.symbols {
                let module_name = source_symbols
                    .get(&symbol.name)
                    .cloned()
                    .unwrap_or_else(|| format!("sym_{}", symbol.name.as_u32()));
                let spec = ImportSpec {
                    path: relative_import_string(import_decl.relative_level, &[module_name.clone()]),
                    alias: None,
                    line: 0,
                };
                let resolved = self.resolver.resolve(&spec, current_path).map_err(|error| {
                    PipelineFailure {
                        diagnostics: vec![CompilerDiagnostic::new(
                            "CLI003",
                            "unresolved import",
                            error.to_string(),
                            Some(import_decl.span),
                        )],
                        source: fs::read_to_string(current_path).unwrap_or_default(),
                        timings: self.timings.clone(),
                    }
                })?;
                let module_id = self
                    .module_ids_by_path
                    .get(&resolved.module.file_path)
                    .copied()
                    .ok_or_else(|| module_error_failure(current_path, "resolved module missing from graph"))?;
                targets.push(ResolvedTarget {
                    module_id,
                    module_label: self.module_label_for(module_id, &resolved.module),
                    namespace: self.module_namespace_for(module_id, &resolved.module),
                });
            }
            return Ok(targets);
        }

        let segments = import_decl
            .path_segments
            .iter()
            .map(|segment| {
                source_symbols
                    .get(segment)
                    .cloned()
                    .unwrap_or_else(|| format!("sym_{}", segment.as_u32()))
            })
            .collect::<Vec<_>>();
        let spec = ImportSpec {
            path: if import_decl.relative_level > 0 {
                relative_import_string(import_decl.relative_level, &segments)
            } else {
                segments.join(".")
            },
            alias: import_decl.alias.and_then(|alias| source_symbols.get(&alias).cloned()),
            line: 0,
        };
        let resolved = self.resolver.resolve(&spec, current_path).map_err(|error| PipelineFailure {
            diagnostics: vec![CompilerDiagnostic::new(
                "CLI003",
                "unresolved import",
                error.to_string(),
                Some(import_decl.span),
            )],
            source: fs::read_to_string(current_path).unwrap_or_default(),
            timings: self.timings.clone(),
        })?;
        let module_id = self
            .module_ids_by_path
            .get(&resolved.module.file_path)
            .copied()
            .ok_or_else(|| module_error_failure(current_path, "resolved module missing from graph"))?;
        targets.push(ResolvedTarget {
            module_id,
            module_label: self.module_label_for(module_id, &resolved.module),
            namespace: self.module_namespace_for(module_id, &resolved.module),
        });
        Ok(targets)
    }

    fn module_namespace_for(&self, id: ModuleId, module_path: &ModulePath) -> String {
        if id == self.entry_id {
            return String::new();
        }
        let segments = self.module_segments_for(module_path);
        module_namespace(&segments)
    }

    fn module_label_for(&self, _id: ModuleId, module_path: &ModulePath) -> String {
        self.module_segments_for(module_path).join(".")
    }

    fn module_segments_for(&self, module_path: &ModulePath) -> Vec<String> {
        match &module_path.source {
            ModuleSource::Stdlib => {
                let relative = module_path
                    .file_path
                    .strip_prefix(&self.resolver.stdlib_root)
                    .unwrap_or(&module_path.file_path);
                let mut segments = vec![String::from("std")];
                segments.extend(path_segments_without_extension(relative));
                segments
            }
            ModuleSource::Project | ModuleSource::Relative => {
                let relative = module_path
                    .file_path
                    .strip_prefix(&self.resolver.project_root)
                    .unwrap_or(&module_path.file_path);
                path_segments_without_extension(relative)
            }
            ModuleSource::Dependency { name, .. } => {
                let mut segments = vec![name.clone()];
                let relative = self
                    .options
                    .include_dirs
                    .iter()
                    .find_map(|root| module_path.file_path.strip_prefix(root).ok())
                    .or_else(|| {
                        self.options
                            .include_dirs
                            .iter()
                            .find_map(|root| module_path.file_path.strip_prefix(root.join("src")).ok())
                    })
                    .unwrap_or(module_path.file_path.as_path());
                segments.extend(path_segments_without_extension(relative));
                segments
            }
        }
    }

    fn module_symbol(
        &self,
        parser: &mut Parser<'_, '_, '_>,
        intern_cache: &mut HashMap<String, InternedStr>,
        namespace: &str,
    ) -> String {
        let label = if namespace.is_empty() {
            module_name_from_path(&self.entry)
        } else {
            namespace.to_string()
        };
        let _ = intern_owned_cached(parser, intern_cache, &label);
        label
    }

    fn object_path_for(&self, id: ModuleId, hash: &str) -> PathBuf {
        let suffix = hash.trim_start_matches("sha256:");
        self.build_dir
            .join(format!("{}_{}.o", id.as_u32(), &suffix[..16.min(suffix.len())]))
    }

    fn module_hash(&self, source: &str, namespace: &str) -> String {
        let mut hasher = Sha256::new();
        hasher.update(MODULE_CACHE_VERSION.as_bytes());
        hasher.update(source.as_bytes());
        hasher.update(namespace.as_bytes());
        hasher.update(format!("{:?}{:?}{:?}", self.options.opt, self.options.target, self.options.debug));
        for define in &self.options.defines {
            hasher.update(define.as_bytes());
        }
        for feature in &self.options.features {
            hasher.update(feature.as_bytes());
        }
        format!("sha256:{:x}", hasher.finalize())
    }

    fn codegen_options(&self, source_path: &Path) -> CodegenOptions {
        let mut options = CodegenOptions::default();
        options.optimization = match self.options.opt {
            OptLevel::O0 => OptimizationLevel::O0,
            OptLevel::O1 => OptimizationLevel::O1,
            OptLevel::O2 => OptimizationLevel::O2,
            OptLevel::O3 => OptimizationLevel::O3,
        };
        options.target = TargetMachineConfig {
            triple: self.options.target.clone(),
            ..TargetMachineConfig::default()
        };
        options.debug = DebugOptions {
            enabled: self.options.debug,
            source_name: source_path.display().to_string(),
        };
        options
    }
}

#[derive(Debug)]
struct ResolvedTarget {
    module_id: ModuleId,
    module_label: String,
    namespace: String,
}

fn map_graph_exports(exports: &[GraphExportedSymbol]) -> Vec<ExportedSymbol> {
    exports
        .iter()
        .map(|export| ExportedSymbol {
            local_name: export.name.clone(),
            qualified_name: export.qualified_name.clone(),
        })
        .collect()
}

fn map_exports_with_types<'ast>(
    exports: Vec<ExportedSymbol>,
    decls: &[Decl<'ast>],
    source_symbols: &BTreeMap<InternedStr, String>,
    parser: &Parser<'_, '_, '_>,
) -> Vec<GraphExportedSymbol> {
    let mut type_sigs = HashMap::new();
    for decl in decls {
        match decl {
            Decl::Func(node) => {
                type_sigs.insert(
                    source_symbols
                        .get(&node.name)
                        .cloned()
                        .unwrap_or_else(|| format!("sym_{}", node.name.as_u32())),
                    TypeSig::Function {
                        params: node.params.iter().map(|param| render_type_expr(param.ty, parser)).collect(),
                        return_type: node
                            .return_type
                            .map(|ty| render_type_expr(ty, parser))
                            .unwrap_or_else(|| String::from("void")),
                    },
                );
            }
            Decl::Extern(node) => {
                type_sigs.insert(
                    source_symbols
                        .get(&node.name)
                        .cloned()
                        .unwrap_or_else(|| format!("sym_{}", node.name.as_u32())),
                    TypeSig::Function {
                        params: node.params.iter().map(|param| render_type_expr(param.ty, parser)).collect(),
                        return_type: render_type_expr(node.return_type, parser),
                    },
                );
            }
            Decl::Struct(node) => {
                type_sigs.insert(
                    source_symbols
                        .get(&node.name)
                        .cloned()
                        .unwrap_or_else(|| format!("sym_{}", node.name.as_u32())),
                    TypeSig::Struct {
                        name: source_symbols
                            .get(&node.name)
                            .cloned()
                            .unwrap_or_else(|| format!("sym_{}", node.name.as_u32())),
                    },
                );
            }
            Decl::Const(node) => {
                type_sigs.insert(
                    source_symbols
                        .get(&node.name)
                        .cloned()
                        .unwrap_or_else(|| format!("sym_{}", node.name.as_u32())),
                    TypeSig::Const {
                        ty: render_type_expr(node.type_ann, parser),
                    },
                );
            }
            _ => {}
        }
    }

    exports
        .into_iter()
        .map(|export| GraphExportedSymbol {
            name: export.local_name.clone(),
            qualified_name: export.qualified_name,
            alias: None,
            kind: match type_sigs.get(&export.local_name) {
                Some(TypeSig::Function { .. }) => SymbolKind::Function,
                Some(TypeSig::Struct { .. }) => SymbolKind::Struct,
                Some(TypeSig::Const { .. }) => SymbolKind::Const,
                _ => SymbolKind::Function,
            },
            type_sig: type_sigs
                .remove(&export.local_name)
                .unwrap_or_else(|| TypeSig::Named(String::from("unknown"))),
        })
        .collect()
}

fn render_type_expr(ty: TypeExprRef<'_>, parser: &Parser<'_, '_, '_>) -> String {
    match ty {
        TypeExpr::Named(node) => parser
            .resolve_symbol(node.name)
            .map(ToString::to_string)
            .unwrap_or_else(|| format!("sym_{}", node.name.as_u32())),
        TypeExpr::Generic(node) => {
            let base = parser
                .resolve_symbol(node.name)
                .map(ToString::to_string)
                .unwrap_or_else(|| format!("sym_{}", node.name.as_u32()));
            let args = node
                .args
                .iter()
                .map(|arg| render_type_expr(*arg, parser))
                .collect::<Vec<_>>();
            format!("{base}<{}>", args.join(","))
        }
        TypeExpr::Infer(_) => String::from("_"),
    }
}

fn prepend_import_externs<'ast>(
    arena: &'ast Bump,
    parser: &mut Parser<'_, '_, 'ast>,
    intern_cache: &mut HashMap<String, InternedStr>,
    next_node_id: &mut u32,
    externs: &[ImportedExtern],
    decls: &mut Vec<Decl<'ast>>,
) {
    if externs.is_empty() {
        return;
    }

    let mut synthetic = Vec::new();
    for imported in externs {
        let TypeSig::Function { params, return_type } = &imported.export.type_sig else {
            continue;
        };
        let name = intern_owned_cached(parser, intern_cache, &imported.export.qualified_name);
        let params = params
            .iter()
            .enumerate()
            .map(|(index, ty)| Param {
                id: next_synthetic_node(next_node_id),
                span: Span::empty(),
                name: intern_owned_cached(parser, intern_cache, &format!("arg_{index}")),
                ty: parse_type_signature(arena, parser, intern_cache, next_node_id, ty),
            })
            .collect::<Vec<_>>();
        let params = arena.alloc_slice_fill_iter(params);
        let return_type =
            parse_type_signature(arena, parser, intern_cache, next_node_id, return_type);
        synthetic.push(Decl::Extern(ExternDecl {
            id: next_synthetic_node(next_node_id),
            span: Span::empty(),
            name,
            params,
            return_type,
        }));
    }
    if !synthetic.is_empty() {
        synthetic.append(decls);
        *decls = synthetic;
    }
}

fn parse_type_signature<'ast>(
    arena: &'ast Bump,
    parser: &mut Parser<'_, '_, 'ast>,
    intern_cache: &mut HashMap<String, InternedStr>,
    next_node_id: &mut u32,
    text: &str,
) -> TypeExprRef<'ast> {
    let trimmed = text.trim();
    if let Some((name, args)) = split_generic(trimmed) {
        let name = intern_owned_cached(parser, intern_cache, name);
        let args = split_top_level_args(args)
            .into_iter()
            .map(|arg| parse_type_signature(arena, parser, intern_cache, next_node_id, arg))
            .collect::<Vec<_>>();
        let args = arena.alloc_slice_fill_iter(args);
        return arena.alloc(TypeExpr::Generic(GenericTypeExpr {
            id: next_synthetic_node(next_node_id),
            span: Span::empty(),
            name,
            args,
        }));
    }

    arena.alloc(TypeExpr::Named(NamedTypeExpr {
        id: next_synthetic_node(next_node_id),
        span: Span::empty(),
        name: intern_owned_cached(parser, intern_cache, trimmed),
    }))
}

fn split_generic(text: &str) -> Option<(&str, &str)> {
    let start = text.find('<')?;
    let end = text.rfind('>')?;
    (end > start).then_some((&text[..start], &text[start + 1..end]))
}

fn split_top_level_args(args: &str) -> Vec<&str> {
    let mut result = Vec::new();
    let mut depth = 0_u32;
    let mut start = 0_usize;
    for (index, ch) in args.char_indices() {
        match ch {
            '<' => depth = depth.saturating_add(1),
            '>' => depth = depth.saturating_sub(1),
            ',' if depth == 0 => {
                result.push(args[start..index].trim());
                start = index + 1;
            }
            _ => {}
        }
    }
    let tail = args[start..].trim();
    if !tail.is_empty() {
        result.push(tail);
    }
    result
}

fn path_segments_without_extension(path: &Path) -> Vec<String> {
    let mut segments = path
        .iter()
        .map(|segment| segment.to_string_lossy().into_owned())
        .collect::<Vec<_>>();
    if let Some(last) = segments.last_mut() {
        if last == "main.tg" {
            segments.pop();
        } else if let Some(stripped) = last.strip_suffix(".tg") {
            *last = stripped.to_string();
        }
    }
    segments.into_iter().filter(|segment| !segment.is_empty()).collect()
}

fn relative_import_string(relative_level: u8, segments: &[String]) -> String {
    let mut prefix = String::new();
    for index in 0..relative_level {
        if index == 0 {
            prefix.push('.');
        } else {
            prefix.push_str("..");
        }
        if index + 1 != relative_level {
            prefix.push('/');
        }
    }
    if !segments.is_empty() {
        if !prefix.is_empty() {
            prefix.push('/');
        }
        prefix.push_str(&segments.join("."));
    }
    prefix
}

fn module_error_failure(path: &Path, message: impl Into<String>) -> PipelineFailure {
    PipelineFailure {
        diagnostics: vec![CompilerDiagnostic::new(
            "CLI003",
            "module resolution failed",
            message.into(),
            None,
        )],
        source: fs::read_to_string(path).unwrap_or_default(),
        timings: TimingReport::new(),
    }
}

fn session_fallback_failure(message: impl Into<String>, source: &str) -> PipelineFailure {
    PipelineFailure {
        diagnostics: vec![CompilerDiagnostic::new(
            SESSION_FALLBACK_CODE,
            "legacy module fallback required",
            message.into(),
            None,
        )],
        source: source.to_string(),
        timings: TimingReport::new(),
    }
}

fn should_fallback_to_legacy(failure: &PipelineFailure) -> bool {
    failure
        .diagnostics
        .iter()
        .any(|diagnostic| diagnostic.code == SESSION_FALLBACK_CODE)
}

fn entry_uses_relative_imports(path: &Path) -> bool {
    let Ok(source) = fs::read_to_string(path) else {
        return false;
    };
    source.lines().any(|line| {
        let trimmed = line.trim_start();
        trimmed.starts_with("from .") || trimmed.starts_with("from ..")
    })
}
