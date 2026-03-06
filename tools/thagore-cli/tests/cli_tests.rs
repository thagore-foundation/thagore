#[path = "../src/cli.rs"]
mod cli;
#[path = "../src/error.rs"]
mod error;
#[path = "../src/run.rs"]
mod run;
#[path = "../src/timer.rs"]
mod timer;

use std::fs;
use std::path::Path;
use std::process::Command;
use std::thread;
use std::time::Duration;

use clap::Parser as ClapParser;
use tempfile::TempDir;
use termcolor::Buffer;
use thagore_ast::Span;

use cli::{Cli, Command as CliCommand, EmitKind, OptLevel};
use error::{CompilerDiagnostic, ErrorReporter};
use run::RunWorkspace;
use timer::{Timer, TimingReport};

const CHECK_FIXTURES: &[&str] = &[
    "tests/fixtures/basic/arithmetic.tg",
    "tests/fixtures/basic/booleans.tg",
    "tests/fixtures/basic/hello.tg",
    "tests/fixtures/basic/strings.tg",
    "tests/fixtures/basic/variables.tg",
    "tests/fixtures/bench/binary_tree.tg",
    "tests/fixtures/bench/fibonacci.tg",
    "tests/fixtures/bench/loop_sum.tg",
    "tests/fixtures/bench/matrix_mul.tg",
    "tests/fixtures/bench/string_search.tg",
    "tests/fixtures/bench/struct_alloc.tg",
    "tests/fixtures/bench/struct_heavy.tg",
    "tests/fixtures/control/early_return.tg",
    "tests/fixtures/control/for_loop.tg",
    "tests/fixtures/control/if_else.tg",
    "tests/fixtures/control/while_loop.tg",
    "tests/fixtures/ffi/extern_math.tg",
    "tests/fixtures/ffi/extern_printf.tg",
    "tests/fixtures/flow/basic_flow.tg",
    "tests/fixtures/flow/nested_stages.tg",
    "tests/fixtures/flow/payment_flow.tg",
    "tests/fixtures/functions/basic_func.tg",
    "tests/fixtures/functions/multiple_return.tg",
    "tests/fixtures/functions/recursion.tg",
    "tests/fixtures/functions/void_func.tg",
    "tests/fixtures/hello.tg",
    "tests/fixtures/imports/import_multi.tg",
    "tests/fixtures/imports/import_std.tg",
    "tests/fixtures/intent/basic_intent.tg",
    "tests/fixtures/intent/intent_search.tg",
    "tests/fixtures/intent/intent_sort.tg",
    "tests/fixtures/structs/basic_struct.tg",
    "tests/fixtures/structs/impl_methods.tg",
    "tests/fixtures/structs/nested_struct.tg",
];

const RUN_ZERO_FIXTURES: &[&str] = &[
    "tests/fixtures/basic/arithmetic.tg",
    "tests/fixtures/basic/booleans.tg",
    "tests/fixtures/basic/hello.tg",
    "tests/fixtures/basic/strings.tg",
    "tests/fixtures/basic/variables.tg",
    "tests/fixtures/control/early_return.tg",
    "tests/fixtures/control/for_loop.tg",
    "tests/fixtures/control/if_else.tg",
    "tests/fixtures/control/while_loop.tg",
    "tests/fixtures/ffi/extern_math.tg",
    "tests/fixtures/ffi/extern_printf.tg",
    "tests/fixtures/flow/basic_flow.tg",
    "tests/fixtures/flow/nested_stages.tg",
    "tests/fixtures/flow/payment_flow.tg",
    "tests/fixtures/functions/basic_func.tg",
    "tests/fixtures/functions/multiple_return.tg",
    "tests/fixtures/functions/recursion.tg",
    "tests/fixtures/functions/void_func.tg",
    "tests/fixtures/hello.tg",
    "tests/fixtures/imports/import_multi.tg",
    "tests/fixtures/imports/import_std.tg",
    "tests/fixtures/intent/basic_intent.tg",
    "tests/fixtures/intent/intent_search.tg",
    "tests/fixtures/intent/intent_sort.tg",
    "tests/fixtures/structs/basic_struct.tg",
    "tests/fixtures/structs/impl_methods.tg",
    "tests/fixtures/structs/nested_struct.tg",
];

const RUN_BENCH_FIXTURES: &[&str] = &[
    "tests/fixtures/bench/binary_tree.tg",
    "tests/fixtures/bench/fibonacci.tg",
    "tests/fixtures/bench/loop_sum.tg",
    "tests/fixtures/bench/matrix_mul.tg",
    "tests/fixtures/bench/string_search.tg",
    "tests/fixtures/bench/struct_alloc.tg",
    "tests/fixtures/bench/struct_heavy.tg",
];

const ERROR_FIXTURES: &[&str] = &[
    "tests/fixtures/errors/bad_indent.tg",
    "tests/fixtures/errors/missing_paren.tg",
    "tests/fixtures/errors/missing_return.tg",
    "tests/fixtures/errors/tab_indent.tg",
    "tests/fixtures/errors/type_mismatch.tg",
    "tests/fixtures/errors/unknown_var.tg",
];

fn repo_path(relative: &str) -> String {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../..")
        .join(relative)
        .display()
        .to_string()
}

#[test]
fn parses_build_arguments() {
    let cli = Cli::try_parse_from([
        "thagc",
        "build",
        "tests/fixtures/hello.tg",
        "-o",
        "out/hello",
        "--opt",
        "O3",
        "--emit",
        "ll,obj,bin",
        "--debug",
        "--target",
        "x86_64-unknown-linux-gnu",
        "--include-dir",
        "vendor",
        "--define",
        "MODE=release",
        "--features",
        "ffi,std",
        "--json-errors",
        "--time",
    ])
    .expect("parse cli");

    let Some(CliCommand::Build(build)) = cli.command else {
        panic!("expected build command");
    };
    assert_eq!(build.options.output.as_deref(), Some(Path::new("out/hello")));
    assert_eq!(build.options.opt, OptLevel::O3);
    assert!(build.options.emit.contains(&EmitKind::Ll));
    assert!(build.options.emit.contains(&EmitKind::Obj));
    assert!(build.options.emit.contains(&EmitKind::Bin));
    assert!(build.options.debug);
    assert_eq!(
        build.options.target.as_deref(),
        Some("x86_64-unknown-linux-gnu")
    );
    assert_eq!(build.options.include_dirs, vec![Path::new("vendor")]);
    assert_eq!(build.options.defines, vec!["MODE=release"]);
    assert_eq!(build.options.features, vec!["ffi", "std"]);
    assert!(build.options.json_errors);
    assert!(build.options.time);
}

#[test]
fn formats_text_diagnostics() {
    let mut buffer = Buffer::no_color();
    let diagnostics = vec![CompilerDiagnostic::new(
        "E001",
        "type mismatch",
        "expected i32, found f64",
        Some(Span::new(13, 17)),
    )];

    ErrorReporter::emit_text(
        &mut buffer,
        Path::new("sample.tg"),
        "let x: i32 = 3.14\n",
        &diagnostics,
    )
    .expect("emit text");

    let rendered = String::from_utf8(buffer.into_inner()).expect("utf8");
    assert!(rendered.contains("error[E001]: type mismatch"));
    assert!(rendered.contains("sample.tg:1:14"));
    assert!(rendered.contains("expected i32, found f64"));
}

#[test]
fn emits_contract_json_diagnostics() {
    let diagnostics = vec![CompilerDiagnostic::new(
        "E001",
        "type mismatch",
        "expected i32, found f64",
        Some(Span::new(13, 17)),
    )];
    let mut rendered = Vec::new();
    ErrorReporter::emit_json(
        &mut rendered,
        Path::new("sample.tg"),
        "let x: i32 = 3.14\n",
        &diagnostics,
    )
    .expect("emit json");

    let value: serde_json::Value =
        serde_json::from_slice(&rendered).expect("parse diagnostic json");
    let array = value.as_array().expect("json array");
    assert_eq!(array.len(), 1);
    assert_eq!(array[0]["file"], "sample.tg");
    assert_eq!(array[0]["line"], 1);
    assert_eq!(array[0]["col"], 14);
    assert_eq!(array[0]["severity"], "error");
    assert!(array[0]["message"].as_str().expect("message").contains("type mismatch"));
}

#[test]
fn timer_reports_elapsed_time() {
    let timer = Timer::start();
    thread::sleep(Duration::from_millis(5));
    assert!(timer.elapsed() >= Duration::from_millis(5));

    let mut report = TimingReport::new();
    report.record("lexer", Duration::from_millis(1));
    let mut rendered = Vec::new();
    report.write(&mut rendered).expect("write timings");
    let rendered = String::from_utf8(rendered).expect("utf8");
    assert!(rendered.contains("lexer"));
    assert!(rendered.contains("total"));
}

#[test]
fn run_workspace_cleans_up_temp_directory() {
    let path = {
        let workspace = RunWorkspace::new().expect("workspace");
        let path = workspace.path().to_path_buf();
        assert!(path.exists());
        path
    };
    assert!(!path.exists());
}

#[test]
fn usage_errors_return_101() {
    let status = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .status()
        .expect("run thagc");
    assert_eq!(status.code(), Some(101));
}

#[test]
fn compile_errors_return_1() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("broken.tg");
    fs::write(&source, "func main() -> i32:\n  return\n").expect("write source");

    let status = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .status()
        .expect("run thagc check");
    assert_eq!(status.code(), Some(1));
}

#[test]
fn build_fixture_produces_runnable_binary() {
    let out_dir = TempDir::new().expect("temp dir");
    let binary = out_dir.path().join("hello");
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../tests/fixtures/hello.tg");

    let status = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            fixture.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .status()
        .expect("run thagc build");
    assert!(status.success());
    assert!(binary.exists());

    let status = Command::new(&binary).status().expect("run built binary");
    assert_eq!(status.code(), Some(0));
}

#[test]
fn run_forwards_program_exit_code() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("exit_five.tg");
    fs::write(&source, "func main() -> i32:\n  return 5\n").expect("write source");

    let status = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["run", source.to_str().expect("utf8")])
        .status()
        .expect("run thagc run");
    assert_eq!(status.code(), Some(5));
}

#[test]
fn check_json_emits_array_payload() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("broken_json.tg");
    fs::write(&source, "let broken =\n").expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8"), "--json"])
        .output()
        .expect("run thagc check --json");
    assert_eq!(output.status.code(), Some(1));

    let stdout = String::from_utf8(output.stdout).expect("utf8");
    assert!(stdout.contains("\"file\""));
    assert!(stdout.contains("\"severity\""));
}

#[test]
fn version_flag_returns_json_contract() {
    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .arg("--version")
        .output()
        .expect("run thagc --version");
    assert!(output.status.success());
    let value: serde_json::Value =
        serde_json::from_slice(&output.stdout).expect("parse version json");
    assert_eq!(value["thagc"], "0.9.0");
    assert!(value.get("llvm").is_some());
    assert!(value.get("host").is_some());
}

#[test]
fn print_target_list_returns_json_array() {
    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .arg("--print-target-list")
        .output()
        .expect("run thagc --print-target-list");
    assert!(output.status.success());
    let value: serde_json::Value =
        serde_json::from_slice(&output.stdout).expect("parse target list json");
    let list = value.as_array().expect("array");
    assert!(!list.is_empty());
}

#[test]
fn check_accepts_define_and_feature_bindings() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("feature_gate.tg");
    fs::write(
        &source,
        "func main() -> i32:\n  if (FEATURE_FFI and ENABLE_TRACE):\n    return 0\n  return 1\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "check",
            source.to_str().expect("utf8"),
            "--define",
            "ENABLE_TRACE=true",
            "--features",
            "ffi",
        ])
        .output()
        .expect("run thagc check");
    assert!(
        output.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );
}

#[test]
fn check_resolves_imports_through_include_dirs() {
    let dir = TempDir::new().expect("temp dir");
    let include_root = dir.path().join("deps");
    fs::create_dir_all(&include_root).expect("create include dir");
    fs::write(include_root.join("dep.tg"), "func helper() -> i32:\n  return 0\n")
        .expect("write dep module");
    let source = dir.path().join("main.tg");
    fs::write(&source, "import dep\n\nfunc main() -> i32:\n  return 0\n").expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "check",
            source.to_str().expect("utf8"),
            "--include-dir",
            include_root.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc check");
    assert!(
        output.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );
}

#[test]
fn check_rewrites_alias_qualified_calls_from_imported_modules() {
    let dir = TempDir::new().expect("temp dir");
    let include_root = dir.path().join("deps");
    fs::create_dir_all(include_root.join("std")).expect("create include dir");
    fs::write(
        include_root.join("std").join("string.tg"),
        "pub func slen(value: str) -> i32:\n  return 2\n",
    )
    .expect("write imported module");
    let source = dir.path().join("main.tg");
    fs::write(
        &source,
        "import std.string as str\n\nfunc main() -> i32:\n  if (str.slen(\"ok\") == 2):\n    return 0\n  return 1\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "check",
            source.to_str().expect("utf8"),
            "--include-dir",
            include_root.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc check");
    assert!(
        output.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );
}

#[test]
fn all_positive_fixtures_pass_check() {
    for fixture in CHECK_FIXTURES {
        let fixture_path = repo_path(fixture);
        let output = Command::new(env!("CARGO_BIN_EXE_thagore"))
            .args(["check", &fixture_path])
            .output()
            .unwrap_or_else(|error| panic!("failed to check {fixture}: {error}"));
        assert!(
            output.status.success(),
            "check failed for {fixture}:\n{}",
            String::from_utf8_lossy(&output.stderr)
        );
    }
}

#[test]
fn non_benchmark_fixtures_exit_zero() {
    for fixture in RUN_ZERO_FIXTURES {
        let fixture_path = repo_path(fixture);
        let output = Command::new(env!("CARGO_BIN_EXE_thagore"))
            .args(["run", &fixture_path])
            .output()
            .unwrap_or_else(|error| panic!("failed to run {fixture}: {error}"));
        assert_eq!(
            output.status.code(),
            Some(0),
            "run failed for {fixture}:\nstdout:\n{}\nstderr:\n{}",
            String::from_utf8_lossy(&output.stdout),
            String::from_utf8_lossy(&output.stderr)
        );
    }
}

#[test]
fn benchmark_fixtures_exit_normally() {
    for fixture in RUN_BENCH_FIXTURES {
        let fixture_path = repo_path(fixture);
        let output = Command::new(env!("CARGO_BIN_EXE_thagore"))
            .args(["run", &fixture_path])
            .output()
            .unwrap_or_else(|error| panic!("failed to run {fixture}: {error}"));
        assert!(
            output.status.code().is_some(),
            "benchmark fixture terminated by signal for {fixture}:\nstdout:\n{}\nstderr:\n{}",
            String::from_utf8_lossy(&output.stdout),
            String::from_utf8_lossy(&output.stderr)
        );
    }
}

#[test]
fn negative_fixtures_emit_diagnostics() {
    for fixture in ERROR_FIXTURES {
        let fixture_path = repo_path(fixture);
        let output = Command::new(env!("CARGO_BIN_EXE_thagore"))
            .args(["check", &fixture_path])
            .output()
            .unwrap_or_else(|error| panic!("failed to check {fixture}: {error}"));
        assert!(
            !output.status.success(),
            "negative fixture unexpectedly passed: {fixture}"
        );
        let stderr = String::from_utf8_lossy(&output.stderr);
        assert!(
            stderr.contains("error["),
            "missing diagnostic for {fixture}:\n{stderr}"
        );
    }
}
