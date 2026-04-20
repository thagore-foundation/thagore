//! Dual-run stdlib parity: same .tg source through interpreter and compiled binary must produce
//! identical stdout.  Each `parity_*` test is a self-contained regression gate.

use bumpalo::Bump;
use std::fs;
use std::process::Command;
use thagore_interpreter::{Interpreter, SymbolTable};
use thagore_lexer::Lexer;
use thagore_parser::Parser;

fn interpret_source(source: &str) -> String {
    let arena = Bump::new();
    let mut lexer = Lexer::new(source);
    let tokens = lexer.lex_all_in(&arena);
    let interner = lexer.interner();
    let mut parser = Parser::new(tokens.as_slice(), &arena, interner);
    let decls = parser.parse_program();
    let errors = parser.take_errors();
    assert!(errors.is_empty(), "parse errors in parity fixture: {errors:?}");
    let symbols = SymbolTable::from_snapshot(parser.symbols_snapshot());
    let mut interp = Interpreter::new(symbols);
    interp
        .run(&decls)
        .unwrap_or_else(|e| panic!("interpreter error in parity fixture: {e:?}"));
    interp.stdout().to_string()
}

fn compile_run_source(source: &str) -> String {
    let dir = tempfile::tempdir().expect("tempdir");
    let src = dir.path().join("parity_test.tg");
    let bin = dir.path().join(if cfg!(windows) {
        "parity_test.exe"
    } else {
        "parity_test"
    });
    fs::write(&src, source).expect("write fixture");
    let thagc = env!("CARGO_BIN_EXE_thagc");
    let build = Command::new(thagc)
        .args(["build", src.to_str().unwrap(), "-o", bin.to_str().unwrap()])
        .output()
        .expect("thagc build");
    assert!(
        build.status.success(),
        "thagc build failed: {}",
        String::from_utf8_lossy(&build.stderr)
    );
    let run = Command::new(&bin).output().expect("run binary");
    String::from_utf8_lossy(&run.stdout).replace("\r\n", "\n")
}

fn assert_parity(label: &str, source: &str, expected: &str) {
    let interp = interpret_source(source);
    let compiled = compile_run_source(source);
    assert_eq!(interp, expected, "{label}: interpreter stdout mismatch");
    assert_eq!(compiled, expected, "{label}: compiled stdout mismatch");
}

// ---------------------------------------------------------------------------
// std.io — println with i32 and bool
// ---------------------------------------------------------------------------

#[test]
fn parity_std_io_println() {
    assert_parity(
        "std.io",
        "import std.io as io\n\nfunc main() -> i32:\n  io.println(42)\n  io.println(true)\n  return 0\n",
        "42\ntrue\n",
    );
}

// ---------------------------------------------------------------------------
// std.io streams — print (no newline) + println on stdout and stderr
// ---------------------------------------------------------------------------

#[test]
fn parity_std_io_print_and_println() {
    assert_parity(
        "std.io print",
        "import std.io as io\n\nfunc main() -> i32:\n  io.print(7)\n  io.println(true)\n  return 0\n",
        "7true\n",
    );
}

// ---------------------------------------------------------------------------
// std.string — from_int, len, concat
// ---------------------------------------------------------------------------

#[test]
fn parity_std_string_from_int_len_concat() {
    assert_parity(
        "std.string",
        "import std.string as string\n\nfunc main() -> i32:\n  println(string.from_int(99))\n  println(string.len(\"hello\"))\n  println(string.concat(\"foo\", \"bar\"))\n  return 0\n",
        "99\n5\nfoobar\n",
    );
}

// ---------------------------------------------------------------------------
// std.string — split + join round-trip
// ---------------------------------------------------------------------------

#[test]
fn parity_std_string_split_join() {
    assert_parity(
        "std.string split/join",
        "import std.string as string\n\nfunc main() -> i32:\n  let items = string.split(\"a,b,c\", \",\")\n  println(string.join(items, \":\"))\n  return 0\n",
        "a:b:c\n",
    );
}

// ---------------------------------------------------------------------------
// std.math — gcd, abs, is_even
// ---------------------------------------------------------------------------

#[test]
fn parity_std_math_gcd_abs_is_even() {
    assert_parity(
        "std.math",
        "import std.math as math\n\nfunc main() -> i32:\n  println(math.gcd(12, 8))\n  println(math.abs(-5))\n  println(math.is_even(4))\n  return 0\n",
        "4\n5\ntrue\n",
    );
}

// ---------------------------------------------------------------------------
// std.path — join with empty left component
// ---------------------------------------------------------------------------

#[test]
fn parity_std_path_join_empty_left() {
    assert_parity(
        "std.path",
        "import std.path as path\n\nfunc main() -> i32:\n  println(path.join(\"\", \"leaf.txt\"))\n  return 0\n",
        "leaf.txt\n",
    );
}
