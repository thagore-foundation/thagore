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

#[test]
fn parses_build_arguments() {
    let cli = Cli::try_parse_from([
        "thagore",
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
        "--time",
    ])
    .expect("parse cli");

    let CliCommand::Build(build) = cli.command else {
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
    let status = Command::new(env!("CARGO_BIN_EXE_thagore"))
        .status()
        .expect("run thagore");
    assert_eq!(status.code(), Some(101));
}

#[test]
fn compile_errors_return_1() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("broken.tg");
    fs::write(&source, "func main() -> i32:\n  return\n").expect("write source");

    let status = Command::new(env!("CARGO_BIN_EXE_thagore"))
        .args(["check", source.to_str().expect("utf8")])
        .status()
        .expect("run thagore check");
    assert_eq!(status.code(), Some(1));
}

#[test]
fn build_fixture_produces_runnable_binary() {
    let out_dir = TempDir::new().expect("temp dir");
    let binary = out_dir.path().join("hello");
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../tests/fixtures/hello.tg");

    let status = Command::new(env!("CARGO_BIN_EXE_thagore"))
        .args([
            "build",
            fixture.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .status()
        .expect("run thagore build");
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

    let status = Command::new(env!("CARGO_BIN_EXE_thagore"))
        .args(["run", source.to_str().expect("utf8")])
        .status()
        .expect("run thagore run");
    assert_eq!(status.code(), Some(5));
}

#[test]
fn check_json_emits_array_payload() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("broken_json.tg");
    fs::write(&source, "let broken =\n").expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagore"))
        .args(["check", source.to_str().expect("utf8"), "--json"])
        .output()
        .expect("run thagore check --json");
    assert_eq!(output.status.code(), Some(1));

    let stdout = String::from_utf8(output.stdout).expect("utf8");
    assert!(stdout.contains("\"file\""));
    assert!(stdout.contains("\"code\""));
}
