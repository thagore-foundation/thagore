use bumpalo::Bump;
use thagore_ast::{BinOp, Decl, Expr, Stmt};
use thagore_lexer::Lexer;
use thagore_parser::Parser;

fn with_parsed_source(
    source: &str,
    check: impl for<'ast> FnOnce(Vec<Decl<'ast>>, Vec<thagore_parser::ParseError>),
) {
    let arena = Bump::new();
    let mut lexer = Lexer::new(source);
    let tokens = lexer.lex_all_in(&arena);
    let mut parser = Parser::new(tokens.as_slice(), &arena, lexer.interner());
    let decls = parser.parse_program();
    let errors = parser.take_errors();
    check(decls, errors);
}

#[test]
fn parses_all_declaration_forms() {
    let source = "\
import std.io as io
from math import sqrt, abs as absolute
import src.utils include all
extern func printf(fmt: str) -> i32
let answer: i32 = 42
struct Point:
  x: f64
  y: f64
impl Point:
  func distance(self) -> f64:
    return 0.0
func add(x: i32, y: i32) -> i32:
  return x + y
intent Optimize:
  minimize memory_usage
  body:
    return 0
flow Transaction:
  stage acquire:
    return 1
  stage commit:
    return 2
  compensate:
    return 3
";

    with_parsed_source(source, |decls, errors| {
        assert!(errors.is_empty(), "{errors:?}");
        assert_eq!(decls.len(), 10);
        let Decl::Import(import_decl) = &decls[0] else {
            panic!("expected import")
        };
        assert_eq!(import_decl.path_segments.len(), 2);
        assert!(import_decl.alias.is_some());
        let Decl::Import(from_import) = &decls[1] else {
            panic!("expected from import")
        };
        assert!(from_import.is_from);
        assert_eq!(from_import.symbols.len(), 2);
        assert!(from_import.symbols[1].alias.is_some());
        let Decl::Import(include_all) = &decls[2] else {
            panic!("expected include-all import")
        };
        assert!(include_all.include_all);
        assert!(matches!(decls[3], Decl::Extern(_)));
        assert!(matches!(decls[4], Decl::Let(_)));
        assert!(matches!(decls[5], Decl::Struct(_)));
        assert!(matches!(decls[6], Decl::Impl(_)));
        assert!(matches!(decls[7], Decl::Func(_)));
        assert!(matches!(decls[8], Decl::Intent(_)));
        assert!(matches!(decls[9], Decl::Flow(_)));
    });
}

#[test]
fn parses_relative_import_forms() {
    let source = "\
from . import utils
from .. import core
from .utils import helper as helper_alias
from . import shared include all
";

    with_parsed_source(source, |decls, errors| {
        assert!(errors.is_empty(), "{errors:?}");
        assert_eq!(decls.len(), 4);

        let Decl::Import(current_dir) = &decls[0] else {
            panic!("expected import")
        };
        assert_eq!(current_dir.relative_level, 1);
        assert!(current_dir.path_segments.is_empty());
        assert_eq!(current_dir.symbols.len(), 1);

        let Decl::Import(parent_dir) = &decls[1] else {
            panic!("expected import")
        };
        assert_eq!(parent_dir.relative_level, 2);
        assert!(parent_dir.path_segments.is_empty());

        let Decl::Import(relative_symbol) = &decls[2] else {
            panic!("expected import")
        };
        assert_eq!(relative_symbol.relative_level, 1);
        assert_eq!(relative_symbol.path_segments.len(), 1);
        assert_eq!(relative_symbol.symbols.len(), 1);
        assert!(relative_symbol.symbols[0].alias.is_some());

        let Decl::Import(include_all) = &decls[3] else {
            panic!("expected import")
        };
        assert!(include_all.include_all);
    });
}

#[test]
fn parses_pub_break_and_continue_surface_syntax() {
    let source = "\
pub func walk(flag: bool) -> i32:
  while (flag):
    continue
  for item in items:
    break
  return 0
";

    with_parsed_source(source, |decls, errors| {
        assert!(errors.is_empty(), "{errors:?}");
        let Decl::Func(func) = &decls[0] else {
            panic!("expected function")
        };
        assert!(matches!(func.body.statements[0], Stmt::While(_)));
        let Stmt::While(while_stmt) = &func.body.statements[0] else {
            panic!("expected while")
        };
        assert!(matches!(while_stmt.body.statements[0], Stmt::Continue(_)));
        let Stmt::For(for_stmt) = &func.body.statements[1] else {
            panic!("expected for")
        };
        assert!(matches!(for_stmt.body.statements[0], Stmt::Break(_)));
    });
}

#[test]
fn parses_builtin_alias_and_builtin_named_field_access() {
    let source = "\
import std.string as str
func main() -> i32:
  if (str.slen(\"ok\") == 2):
    return 0
  return 1
";

    with_parsed_source(source, |decls, errors| {
        assert!(errors.is_empty(), "{errors:?}");
        let Decl::Import(import_decl) = &decls[0] else {
            panic!("expected import")
        };
        assert!(import_decl.alias.is_some());
        let Decl::Func(func) = &decls[1] else {
            panic!("expected function")
        };
        let Stmt::If(if_stmt) = &func.body.statements[0] else {
            panic!("expected if")
        };
        let Expr::Binary(binary) = if_stmt.condition else {
            panic!("expected comparison")
        };
        let Expr::Call(call) = binary.left else {
            panic!("expected call")
        };
        let Expr::FieldAccess(field) = call.callee else {
            panic!("expected namespaced callee")
        };
        let Expr::Ident(base) = field.object else {
            panic!("expected alias identifier")
        };
        assert_ne!(field.field, base.name);
    });
}

#[test]
fn parses_expression_precedence_levels() {
    let source = "\
func logic(a: i32, b: i32, c: i32, d: i32, e: i32, f: i32, g: i32):
  return a or b and not c == d < e + f * g
";

    with_parsed_source(source, |decls, errors| {
        assert!(errors.is_empty(), "{errors:?}");
        let Decl::Func(func) = &decls[0] else {
            panic!("expected function")
        };
        let Stmt::Return(ret) = &func.body.statements[0] else {
            panic!("expected return")
        };
        let expr = ret.value.expect("return value");

        let Expr::Binary(or_expr) = expr else {
            panic!("expected or expression")
        };
        assert_eq!(or_expr.op, BinOp::Or);
        let Expr::Binary(and_expr) = or_expr.right else {
            panic!("expected and expression")
        };
        assert_eq!(and_expr.op, BinOp::And);
        let Expr::Unary(not_expr) = and_expr.right else {
            panic!("expected unary not")
        };
        let Expr::Binary(eq_expr) = not_expr.operand else {
            panic!("expected equality")
        };
        assert_eq!(eq_expr.op, BinOp::Eq);
        let Expr::Binary(cmp_expr) = eq_expr.right else {
            panic!("expected comparison")
        };
        assert_eq!(cmp_expr.op, BinOp::Lt);
        let Expr::Binary(add_expr) = cmp_expr.right else {
            panic!("expected addition")
        };
        assert_eq!(add_expr.op, BinOp::Add);
        let Expr::Binary(mul_expr) = add_expr.right else {
            panic!("expected multiplication")
        };
        assert_eq!(mul_expr.op, BinOp::Mul);
    });
}

#[test]
fn parses_control_flow_statements() {
    let source = "\
func iterate(flag: bool, items: i32):
  if (flag):
    return 1
  else:
    while (flag):
      for item in items:
        return item
";

    with_parsed_source(source, |decls, errors| {
        assert!(errors.is_empty(), "{errors:?}");
        let Decl::Func(func) = &decls[0] else {
            panic!("expected function")
        };
        assert!(matches!(func.body.statements[0], Stmt::If(_)));
    });
}

#[test]
fn recovers_after_syntax_error() {
    let source = "\
let broken = 
let good = 42
";

    with_parsed_source(source, |decls, errors| {
        assert!(!errors.is_empty());
        assert_eq!(decls.len(), 2);
        assert!(matches!(decls[1], Decl::Let(_)));
    });
}

#[test]
fn parses_intent_and_flow_sections() {
    let source = "\
intent Optimize:
  minimize memory_usage
  body:
    return 0
flow Transaction:
  stage acquire:
    return 1
  compensate:
    return 2
";

    with_parsed_source(source, |decls, errors| {
        assert!(errors.is_empty(), "{errors:?}");
        let Decl::Intent(intent) = &decls[0] else {
            panic!("expected intent")
        };
        assert_eq!(intent.constraints.len(), 1);
        let Decl::Flow(flow) = &decls[1] else {
            panic!("expected flow")
        };
        assert_eq!(flow.stages.len(), 1);
        assert!(flow.compensation.is_some());
    });
}

#[test]
fn display_round_trip_uses_valid_thagore_shape() {
    let source = "\
func sample(x: i32) -> i32:
  if (x):
    return x
  else:
    return 0
";

    with_parsed_source(source, |decls, errors| {
        assert!(errors.is_empty(), "{errors:?}");
        let rendered = format!("{}", decls[0]);
        assert_eq!(
            rendered,
            "\
func sym_0(sym_1: sym_2) -> sym_2:
    if (sym_1):
        return sym_1
    else:
        return 0"
        );
    });
}

#[test]
fn missing_condition_parens_emit_errors_but_parser_continues() {
    let source = "\
func sample(flag: bool):
  if flag:
    return 1
  while flag:
    return 0
";

    with_parsed_source(source, |decls, errors| {
        assert_eq!(decls.len(), 1);
        assert!(errors.len() >= 2);
        assert!(errors.iter().all(|error| error.message().contains('(')));
    });
}

#[test]
fn lexer_indent_errors_do_not_stall_recovery() {
    let source = "\
func main() -> i32:
   return 0
func next() -> i32:
  return 1
";

    with_parsed_source(source, |decls, errors| {
        assert_eq!(decls.len(), 2);
        assert!(!errors.is_empty());
        assert!(matches!(decls[1], Decl::Func(_)));
    });
}
