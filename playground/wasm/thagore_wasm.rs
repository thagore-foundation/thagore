//! WebAssembly entry point for the Thagore playground.

use bumpalo::Bump;
use serde_json::{json, Value as JsonValue};
use thagore_ast::{
    Decl, ExternDecl, GenericTypeExpr, InternedStr, NamedTypeExpr, NodeId, Param, Span, TypeExpr,
};
use thagore_fmt::{format_source as fmt_source, FmtConfig};
use thagore_interpreter::{Interpreter, RuntimeError, SymbolTable};
use thagore_lexer::Lexer;
use thagore_parser::{ParseError, Parser};
use thagore_typeck::{TypeChecker, TypeError};
use wasm_bindgen::prelude::*;

/// Result of compiling and running Thagore source in the browser.
#[wasm_bindgen]
pub struct CompileResult {
    /// `true` when compilation and execution succeeded.
    pub success: bool,
    output: String,
    errors: String,
}

#[wasm_bindgen]
impl CompileResult {
    /// Returns captured stdout/stderr text.
    #[wasm_bindgen(getter)]
    pub fn output(&self) -> String {
        self.output.clone()
    }

    /// Returns diagnostics as a JSON array string.
    #[wasm_bindgen(getter)]
    pub fn errors(&self) -> String {
        self.errors.clone()
    }
}

/// Parses, type-checks, and interprets one Thagore source buffer.
#[wasm_bindgen]
pub fn compile_and_run(source: &str) -> CompileResult {
    let arena = Bump::new();
    let mut lexer = Lexer::new(source);
    let tokens = lexer.lex_all_in(&arena);
    let interner = lexer.interner();
    let mut parser = Parser::new(tokens.as_slice(), &arena, interner);
    let mut decls = parser.parse_program();
    let parse_errors = parser.take_errors();
    if !parse_errors.is_empty() {
        return CompileResult {
            success: false,
            output: String::new(),
            errors: diagnostics_json(source, &parse_errors_to_json(source, &parse_errors)).to_string(),
        };
    }

    prepend_playground_builtins(&arena, &mut parser, &mut decls);
    let symbols = SymbolTable::from_snapshot(parser.symbols_snapshot());

    let mut checker = TypeChecker::new();
    register_symbol_names(&mut checker, &symbols);
    let type_errors = match checker.check(&decls) {
        Ok(_) => Vec::new(),
        Err(errors) => errors,
    };
    if !type_errors.is_empty() {
        return CompileResult {
            success: false,
            output: String::new(),
            errors: diagnostics_json(source, &type_errors_to_json(source, &type_errors)).to_string(),
        };
    }

    let mut interpreter = Interpreter::new(symbols);
    match interpreter.run(&decls) {
        Ok(_) => CompileResult {
            success: true,
            output: format!("{}{}", interpreter.stdout(), interpreter.stderr()),
            errors: String::from("[]"),
        },
        Err(error) => CompileResult {
            success: false,
            output: format!("{}{}", interpreter.stdout(), interpreter.stderr()),
            errors: diagnostics_json(source, &[runtime_error_to_json(source, error)]).to_string(),
        },
    }
}

/// Parses and type-checks one Thagore source buffer without executing it.
#[wasm_bindgen]
pub fn check_only(source: &str) -> String {
    let arena = Bump::new();
    let mut lexer = Lexer::new(source);
    let tokens = lexer.lex_all_in(&arena);
    let interner = lexer.interner();
    let mut parser = Parser::new(tokens.as_slice(), &arena, interner);
    let mut decls = parser.parse_program();
    let parse_errors = parser.take_errors();
    if !parse_errors.is_empty() {
        return diagnostics_json(source, &parse_errors_to_json(source, &parse_errors)).to_string();
    }
    prepend_playground_builtins(&arena, &mut parser, &mut decls);
    let symbols = SymbolTable::from_snapshot(parser.symbols_snapshot());
    let mut checker = TypeChecker::new();
    register_symbol_names(&mut checker, &symbols);
    match checker.check(&decls) {
        Ok(_) => String::from("[]"),
        Err(errors) => diagnostics_json(source, &type_errors_to_json(source, &errors)).to_string(),
    }
}

/// Formats one Thagore source buffer using the official formatter.
#[wasm_bindgen]
pub fn format_source(source: &str) -> String {
    let config = FmtConfig::strict();
    fmt_source(std::path::Path::new("playground.tg"), source, &config).formatted
}

fn prepend_playground_builtins<'ast>(
    arena: &'ast Bump,
    parser: &mut Parser<'_, '_, 'ast>,
    decls: &mut Vec<Decl<'ast>>,
) {
    let mut synthetic = builtin_extern_decls(arena, parser);
    synthetic.append(decls);
    *decls = synthetic;
}

fn builtin_extern_decls<'ast>(
    arena: &'ast Bump,
    parser: &mut Parser<'_, '_, 'ast>,
) -> Vec<Decl<'ast>> {
    static BUILTINS: &[(&str, &[(&str, &str)], &str)] = &[
        ("print", &[("s", "str")], "void"),
        ("println", &[("s", "str")], "void"),
        ("eprint", &[("s", "str")], "void"),
        ("eprintln", &[("s", "str")], "void"),
        ("flush", &[], "void"),
        ("from_int", &[("n", "i64")], "str"),
        ("from_f64", &[("n", "f64")], "str"),
        ("from_bool", &[("b", "bool")], "str"),
        ("to_int", &[("s", "str")], "i32"),
        ("to_f64", &[("s", "str")], "f64"),
        ("to_bool", &[("s", "str")], "bool"),
        ("len", &[("value", "str")], "i32"),
        ("concat", &[("a", "str"), ("b", "str")], "str"),
        ("trim", &[("s", "str")], "str"),
        ("contains", &[("s", "str"), ("sub", "str")], "bool"),
        ("starts_with", &[("s", "str"), ("pre", "str")], "bool"),
        ("ends_with", &[("s", "str"), ("suf", "str")], "bool"),
        ("find", &[("s", "str"), ("sub", "str")], "i32"),
        ("replace", &[("s", "str"), ("old", "str"), ("new", "str")], "str"),
        ("to_upper", &[("s", "str")], "str"),
        ("to_lower", &[("s", "str")], "str"),
        ("pad_left", &[("s", "str"), ("n", "i32"), ("ch", "str")], "str"),
        ("pad_right", &[("s", "str"), ("n", "i32"), ("ch", "str")], "str"),
        ("repeat", &[("s", "str"), ("n", "i32")], "str"),
        ("reverse", &[("s", "str")], "str"),
        ("strip", &[("s", "str"), ("ch", "str")], "str"),
        ("char_at", &[("s", "str"), ("i", "i32")], "str"),
        ("split", &[("s", "str"), ("sep", "str")], "Array<str>"),
        ("join", &[("parts", "Array<str>"), ("sep", "str")], "str"),
        ("is_empty", &[("s", "str")], "bool"),
        ("abs", &[("x", "f64")], "f64"),
        ("min", &[("a", "f64"), ("b", "f64")], "f64"),
        ("max", &[("a", "f64"), ("b", "f64")], "f64"),
        ("clamp", &[("x", "f64"), ("lo", "f64"), ("hi", "f64")], "f64"),
        ("pow", &[("base", "f64"), ("exp", "f64")], "f64"),
        ("sqrt", &[("x", "f64")], "f64"),
        ("floor", &[("x", "f64")], "f64"),
        ("ceil", &[("x", "f64")], "f64"),
        ("round", &[("x", "f64")], "f64"),
        ("log", &[("x", "f64")], "f64"),
        ("log2", &[("x", "f64")], "f64"),
        ("log10", &[("x", "f64")], "f64"),
        ("gcd", &[("a", "i32"), ("b", "i32")], "i32"),
        ("lcm", &[("a", "i32"), ("b", "i32")], "i32"),
        ("is_even", &[("n", "i32")], "bool"),
        ("is_odd", &[("n", "i32")], "bool"),
        ("read_line", &[], "str"),
        ("read_int", &[], "i32"),
        ("read_i64", &[], "i64"),
        ("read_f64", &[], "f64"),
        ("read_word", &[], "str"),
        ("read_all", &[], "str"),
        ("read_ints", &[("n", "i32")], "Array<i32>"),
        ("read_i64s", &[("n", "i32")], "Array<i64>"),
        ("now_ms", &[], "i64"),
        ("monotonic_ms", &[], "i64"),
        ("sleep_ms", &[("ms", "i64")], "void"),
    ];

    let mut next_node_cursor = 1_500_000_u32;
    BUILTINS
        .iter()
        .map(|(name, params, return_type)| {
            let params = params
                .iter()
                .map(|(param_name, ty)| Param {
                    id: next_synthetic_id(&mut next_node_cursor),
                    span: Span::empty(),
                    name: parser.intern_text(param_name),
                    ty: arena.alloc(parse_surface_type(arena, parser, ty, &mut next_node_cursor)),
                })
                .collect::<Vec<_>>();
            let params = arena.alloc_slice_fill_iter(params);
            Decl::Extern(ExternDecl {
                id: next_synthetic_id(&mut next_node_cursor),
                span: Span::empty(),
                name: parser.intern_text(name),
                params,
                return_type: arena.alloc(parse_surface_type(
                    arena,
                    parser,
                    return_type,
                    &mut next_node_cursor,
                )),
            })
        })
        .collect()
}

fn parse_surface_type<'ast>(
    arena: &'ast Bump,
    parser: &mut Parser<'_, '_, 'ast>,
    text: &'static str,
    next_node_id: &mut u32,
) -> TypeExpr<'ast> {
    if let Some(inner) = text.strip_prefix("Array<").and_then(|value| value.strip_suffix('>')) {
        let arg: &thagore_ast::TypeExpr<'ast> =
            arena.alloc(parse_surface_type(arena, parser, inner, next_node_id));
        let args = arena.alloc_slice_copy(&[arg]);
        TypeExpr::Generic(GenericTypeExpr {
            id: next_synthetic_id(next_node_id),
            span: Span::empty(),
            name: parser.intern_text("Array"),
            args,
        })
    } else {
        TypeExpr::Named(NamedTypeExpr {
            id: next_synthetic_id(next_node_id),
            span: Span::empty(),
            name: parser.intern_text(text),
        })
    }
}

fn next_synthetic_id(next_node_id: &mut u32) -> NodeId {
    let id = NodeId::new(*next_node_id);
    *next_node_id = next_node_id.saturating_add(1);
    id
}

fn register_symbol_names(checker: &mut TypeChecker, symbols: &SymbolTable) {
    for (index, name) in symbols.names().iter().enumerate() {
        checker.register_symbol_name(InternedStr::new(index as u32), name);
    }
}

fn parse_errors_to_json(source: &str, errors: &[ParseError]) -> Vec<JsonValue> {
    errors
        .iter()
        .map(|error| diagnostic_json(source, error.span.start as usize, error.span.end as usize, error.message(), "error"))
        .collect()
}

fn type_errors_to_json(source: &str, errors: &[TypeError]) -> Vec<JsonValue> {
    errors
        .iter()
        .map(|error| diagnostic_json(source, error.span().start as usize, error.span().end as usize, &error.to_string(), "error"))
        .collect()
}

fn runtime_error_to_json(source: &str, error: RuntimeError) -> JsonValue {
    diagnostic_json(source, 0, 0, &format!("{error:?}"), "error")
}

fn diagnostics_json(_source: &str, diagnostics: &[JsonValue]) -> JsonValue {
    JsonValue::Array(diagnostics.to_vec())
}

fn diagnostic_json(source: &str, start: usize, end: usize, message: &str, severity: &str) -> JsonValue {
    let (line, col) = line_col(source, start);
    let (end_line, end_col) = line_col(source, end);
    json!({
        "line": line,
        "col": col,
        "end_line": end_line,
        "end_col": end_col,
        "message": message,
        "severity": severity,
        "start": start,
        "end": end,
    })
}

fn line_col(source: &str, offset: usize) -> (usize, usize) {
    let mut line = 1_usize;
    let mut col = 1_usize;
    for (index, ch) in source.char_indices() {
        if index >= offset {
            break;
        }
        if ch == '\n' {
            line += 1;
            col = 1;
        } else {
            col += 1;
        }
    }
    (line, col)
}
