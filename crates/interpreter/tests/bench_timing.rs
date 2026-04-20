/// Wall-clock timing tests for the interpreter hot paths.
///
/// These tests report timing to stdout (visible with --nocapture) and are used
/// in CI to record before/after performance metrics across commits.
use std::time::Instant;

use bumpalo::Bump;
use thagore_interpreter::{Interpreter, SymbolTable};
use thagore_lexer::Lexer;
use thagore_parser::Parser;

fn run_source(source: &str, max_steps: usize) -> (thagore_interpreter::Value, std::time::Duration) {
    let arena = Bump::new();
    let mut lexer = Lexer::new(source);
    let tokens = lexer.lex_all_in(&arena);
    let interner = lexer.interner();
    let mut parser = Parser::new(tokens.as_slice(), &arena, interner);
    let decls = parser.parse_program();
    assert!(parser.take_errors().is_empty(), "parse errors in benchmark fixture");
    let symbols = SymbolTable::from_snapshot(parser.symbols_snapshot());
    let mut interp = Interpreter::with_max_steps(symbols, max_steps);
    let start = Instant::now();
    let result = interp.run(&decls).expect("interpreter error in benchmark fixture");
    let elapsed = start.elapsed();
    (result, elapsed)
}

#[test]
fn bench_fib25_interpreter() {
    let source = include_str!("../../../tests/interp_bench/fib.tg");
    let (result, elapsed) = run_source(source, 50_000_000);
    println!("[bench] fib(25) interpreter: {:?}  result={result:?}", elapsed);
    // fib(25) = 75025
    assert_eq!(result, thagore_interpreter::Value::I32(75025));
    assert!(
        elapsed.as_secs() < 30,
        "fib(25) took {elapsed:?} — interpreter is too slow"
    );
}

#[test]
fn bench_tight_loop_interpreter() {
    let source = include_str!("../../../tests/interp_bench/loop.tg");
    let (result, elapsed) = run_source(source, 5_000_000);
    println!("[bench] tight loop 100K interpreter: {:?}  result={result:?}", elapsed);
    assert!(
        elapsed.as_secs() < 30,
        "tight loop took {elapsed:?} — interpreter is too slow"
    );
}
