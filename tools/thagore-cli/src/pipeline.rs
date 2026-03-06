//! End-to-end compiler pipeline orchestration for the Thagore CLI.

use std::boxed::Box;
use std::collections::BTreeSet;
use std::fs;
use std::path::{Path, PathBuf};
use std::time::Instant;

use bumpalo::Bump;
use tempfile::NamedTempFile;
use thagore_ast::visitor::{walk_decl, Visitor};
use thagore_ast::{
    Decl, Expr, GenericTypeExpr, IdentExpr, InternedStr, LetDecl, LitExpr, Literal,
    NamedTypeExpr, NodeId, Span, TypeExpr,
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
    let mut decls = parser.parse_program();
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

    inject_compile_time_bindings(
        &mut decls,
        &mut parser,
        &arena,
        defines,
        features,
        &source,
        &timings,
    )?;
    validate_imports(path, include_dirs, &parser, &decls, &source, &timings)?;

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
    let mut decls = parser.parse_program();
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

    inject_compile_time_bindings(
        &mut decls,
        &mut parser,
        &arena,
        &options.defines,
        &options.features,
        &source,
        &timings,
    )?;
    validate_imports(path, &options.include_dirs, &parser, &decls, &source, &timings)?;

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

fn validate_imports<'src, 'tok, 'ast>(
    entry: &Path,
    include_dirs: &[PathBuf],
    parser: &Parser<'src, 'tok, 'ast>,
    decls: &[Decl<'ast>],
    source: &str,
    timings: &TimingReport,
) -> Result<(), PipelineFailure> {
    let mut diagnostics = Vec::new();
    for decl in decls {
        let Decl::Import(import_decl) = decl else {
            continue;
        };

        let Some(module_path) = import_decl
            .path_segments
            .iter()
            .map(|segment| parser.resolve_symbol(*segment))
            .collect::<Option<Vec<_>>>()
        else {
            continue;
        };

        if module_path.first().copied() == Some("std") {
            continue;
        }

        if resolve_import_path(entry, include_dirs, &module_path).is_none() {
            diagnostics.push(
                CompilerDiagnostic::new(
                    "CLI003",
                    "unresolved import",
                    format!("could not resolve import `{}`", module_path.join(".")),
                    Some(import_decl.span),
                )
                .with_hint("pass --include-dir for dependency roots or add the module to stdlib"),
            );
        }
    }

    if diagnostics.is_empty() {
        Ok(())
    } else {
        Err(PipelineFailure {
            diagnostics,
            source: source.to_string(),
            timings: timings.clone(),
        })
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
