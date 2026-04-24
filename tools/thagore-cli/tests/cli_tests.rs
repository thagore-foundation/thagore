#[path = "../src/cli.rs"]
mod cli;
#[path = "../src/error.rs"]
mod error;
#[path = "../src/run.rs"]
mod run;
#[path = "../src/timer.rs"]
mod timer;

use std::collections::HashMap;
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
    "tests/fixtures/basic/print_values.tg",
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
    "tests/fixtures/generics/constants.tg",
    "tests/fixtures/generics/generic_functions.tg",
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
    "tests/fixtures/basic/print_values.tg",
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
    "tests/fixtures/generics/constants.tg",
    "tests/fixtures/generics/generic_functions.tg",
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

fn assert_build_fails_before_ir_or_codegen(source: &Path, output: &Path) {
    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            output.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert_eq!(build.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&build.stderr);
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
    assert!(!stderr.contains("during IR lowering"), "{stderr}");
    assert!(!stderr.contains("after an earlier error"), "{stderr}");
    assert!(!stderr.contains("missing type for AST node"), "{stderr}");
    assert!(!stderr.contains("invalid IR lowering state"), "{stderr}");
    assert!(!stderr.contains("code generation failed"), "{stderr}");
    assert!(!stderr.contains("during codegen"), "{stderr}");
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
    assert_eq!(
        build.options.output.as_deref(),
        Some(Path::new("out/hello"))
    );
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
fn parses_hidden_selfhost_replacement_check_arguments() {
    let cli = Cli::try_parse_from([
        "thagc",
        "check",
        "tests/fixtures/hello.tg",
        "--selfhost-replacement-bin",
        "out/selfhost-check",
        "--selfhost-replacement-manifest",
        "bootstrap/selfhost/corpus/frontend-differential.txt",
        "--selfhost-replacement-kind",
        "library",
        "--selfhost-replacement-strict",
        "--selfhost-replacement-report-out",
        "out/replacement-route.txt",
    ])
    .expect("parse cli");

    let Some(CliCommand::Check(check)) = cli.command else {
        panic!("expected check command");
    };
    assert_eq!(
        check.selfhost_replacement_bin.as_deref(),
        Some(Path::new("out/selfhost-check"))
    );
    assert_eq!(
        check.selfhost_replacement_manifest.as_deref(),
        Some(Path::new("bootstrap/selfhost/corpus/frontend-differential.txt"))
    );
    assert_eq!(check.selfhost_replacement_kind.as_deref(), Some("library"));
    assert!(check.selfhost_replacement_strict);
    assert_eq!(
        check.selfhost_replacement_report_out.as_deref(),
        Some(Path::new("out/replacement-route.txt"))
    );
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
    assert!(
        array[0]["message"]
            .as_str()
            .expect("message")
            .contains("type mismatch")
    );
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
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/hello.tg");

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
    assert_eq!(value["thagc"], "0.9.7");
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
    fs::write(
        include_root.join("dep.tg"),
        "func helper() -> i32:\n  return 0\n",
    )
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
fn check_supports_from_import_symbol_aliases() {
    let dir = TempDir::new().expect("temp dir");
    let include_root = dir.path().join("deps");
    fs::create_dir_all(&include_root).expect("create include dir");
    fs::write(
        include_root.join("calc.tg"),
        "func sqrt(value: f64) -> f64:\n  return value\n",
    )
    .expect("write imported module");
    let source = dir.path().join("main.tg");
    fs::write(
        &source,
        "from calc import sqrt as root\n\nfunc main() -> i32:\n  if (root(16.0) == 16.0):\n    return 0\n  return 1\n",
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
fn check_reports_ambiguous_include_all_symbols_at_use_site() {
    let dir = TempDir::new().expect("temp dir");
    let include_root = dir.path().join("deps");
    fs::create_dir_all(&include_root).expect("create include dir");
    fs::write(
        include_root.join("alpha.tg"),
        "func len(value: i32) -> i32:\n  return value\n",
    )
    .expect("write alpha module");
    fs::write(
        include_root.join("beta.tg"),
        "func len(value: i32) -> i32:\n  return value\n",
    )
    .expect("write beta module");
    let source = dir.path().join("main.tg");
    fs::write(
        &source,
        "import alpha include all\nimport beta include all\n\nfunc main() -> i32:\n  return len(1)\n",
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
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("ambiguous symbol"), "{stderr}");
    assert!(stderr.contains("alpha.len"), "{stderr}");
    assert!(stderr.contains("beta.len"), "{stderr}");
}

#[test]
fn check_resolves_relative_module_imports() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("main.tg");
    fs::write(
        dir.path().join("utils.tg"),
        "func helper() -> i32:\n  return 0\n",
    )
    .expect("write utils module");
    fs::write(
        &source,
        "from . import utils\n\nfunc main() -> i32:\n  return utils.helper()\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
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
fn build_and_run_std_string_module() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("main.tg");
    let binary = dir.path().join("string_stdlib");
    fs::write(
        &source,
        "import std.string as string\n\nfunc main() -> i32:\n  let value: str = string.concat(\"tha\", \"gore\")\n  if (string.len(value) == 7 and string.contains(value, \"gor\")):\n    return 0\n  return 1\n",
    )
    .expect("write source");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let status = Command::new(&binary).status().expect("run built binary");
    assert_eq!(status.code(), Some(0));
}

#[test]
fn build_and_run_std_string_runtime_helpers() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("main.tg");
    let binary = dir.path().join("string_runtime");
    fs::write(
        &source,
        "import std.string as string\n\nfunc main() -> i32:\n  println(string.from_int(string.len(\"hé\")))\n  println(string.pad_left(\"é\", 4, \" \"))\n  println(string.strip(\"--hello__\", \"-_\"))\n  println(string.from_bool(string.to_bool(\"true\")))\n  println(string.replace(\"abc\", \"\", \"x\"))\n  println(string.char_at(\"abc\", -1))\n  return 0\n",
    )
    .expect("write source");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary).output().expect("run built binary");
    assert_eq!(output.status.code(), Some(0));
    assert_eq!(
        String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n"),
        "3\n  é\nhello\ntrue\nabc\n\n"
    );
}

#[test]
fn build_and_run_std_string_split_join() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("main.tg");
    let binary = dir.path().join("string_split_join");
    fs::write(
        &source,
        "import std.string as string\n\nfunc main() -> i32:\n  let items = string.split(\"a,b,c\", \",\")\n  println(string.join(items, \":\"))\n  return 0\n",
    )
    .expect("write source");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary).output().expect("run built binary");
    assert_eq!(output.status.code(), Some(0));
    assert_eq!(
        String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n"),
        "a:b:c\n"
    );
}

#[test]
fn build_and_run_std_time_module() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("main.tg");
    let binary = dir.path().join("time_stdlib");
    fs::write(
        &source,
        "import std.time as time\n\nfunc main() -> i32:\n  let start = time.now_ms()\n  time.sleep_ms(25)\n  let elapsed = time.now_ms() - start\n  if (elapsed >= 10):\n    return 0\n  return 1\n",
    )
    .expect("write source");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let status = Command::new(&binary).status().expect("run built binary");
    assert_eq!(status.code(), Some(0));
}

#[test]
fn build_and_run_std_time_monotonic_module() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("main.tg");
    let binary = dir.path().join("time_monotonic_stdlib");
    fs::write(
        &source,
        "import std.time as time\n\nfunc main() -> i32:\n  let start = time.monotonic_ms()\n  time.sleep_ms(25)\n  let end = time.monotonic_ms()\n  if (end >= start and (end - start) >= 10):\n    return 0\n  return 1\n",
    )
    .expect("write source");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let status = Command::new(&binary).status().expect("run built binary");
    assert_eq!(status.code(), Some(0));
}

#[test]
fn build_and_run_std_fs_module() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("main.tg");
    let binary = dir.path().join("fs_stdlib");
    fs::write(
        &source,
        "import std.fs as fs\nimport std.string as string\n\nfunc main() -> i32:\n  let root = fs.getcwd()\n  let file = fs.path_join(root, \"fs-stdlib-probe.txt\")\n  if (fs.write(file, \"probe\") == false):\n    return 1\n  if (fs.exists(file) == false):\n    return 2\n  let text = fs.read(file)\n  let size = fs.filesize(file)\n  let removed = fs.remove(file)\n  if (string.str_eq(text, \"probe\") and size >= 5 and removed):\n    return 0\n  return 3\n",
    )
    .expect("write source");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let status = Command::new(&binary).status().expect("run built binary");
    assert_eq!(status.code(), Some(0));
}

#[test]
fn build_and_run_std_fs_missing_path_behavior() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("main.tg");
    let binary = dir.path().join("fs_missing_stdlib");
    let missing = dir.path().join("fs-stdlib-missing.txt");
    let missing = missing.to_string_lossy().replace('\\', "/");
    fs::write(
        &source,
        format!(
            "import std.fs as fs\n\nfunc main() -> i32:\n  let file = \"{missing}\"\n  if (fs.exists(file)):\n    return 1\n  if (fs.remove(file)):\n    return 2\n  if (fs.filesize(file) != 0):\n    return 3\n  return 0\n"
        ),
    )
    .expect("write source");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let status = Command::new(&binary).status().expect("run built binary");
    assert_eq!(status.code(), Some(0));
}

#[test]
fn build_and_run_std_fs_read_dir_is_sorted() {
    if cfg!(target_os = "windows") {
        eprintln!("skip on windows until array ABI is stabilized");
        return;
    }
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("main.tg");
    let binary = dir.path().join("fs_sorted_stdlib");
    let probe_dir = dir.path().join("probe-dir");
    let probe_dir = probe_dir.to_string_lossy().replace('\\', "/");
    fs::write(
        &source,
        format!(
            "import std.fs as fs\nimport std.array as array\nimport std.string as string\n\nfunc main() -> i32:\n  let root = \"{probe_dir}\"\n  if (fs.mkdir(root) == false):\n    return 1\n  if (fs.write(fs.path_join(root, \"c.txt\"), \"c\") == false):\n    return 2\n  if (fs.write(fs.path_join(root, \"a.txt\"), \"a\") == false):\n    return 3\n  if (fs.write(fs.path_join(root, \"b.txt\"), \"b\") == false):\n    return 4\n  let items = fs.read_dir(root)\n  let count = array.len(items)\n  if (count < 3):\n    return 5\n  let first = array.get(items, count - 3)\n  let second = array.get(items, count - 2)\n  let third = array.get(items, count - 1)\n  if (string.str_eq(first, \"a.txt\") and string.str_eq(second, \"b.txt\") and string.str_eq(third, \"c.txt\")):\n    return 0\n  return 6\n"
        ),
    )
    .expect("write source");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let status = Command::new(&binary).status().expect("run built binary");
    assert_eq!(status.code(), Some(0));
}

#[test]
fn build_and_run_std_fs_read_dir_missing_path_returns_empty() {
    if cfg!(target_os = "windows") {
        eprintln!("skip on windows until array ABI is stabilized");
        return;
    }
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("main.tg");
    let binary = dir.path().join("fs_read_dir_missing");
    let missing_dir = dir.path().join("missing-folder");
    let missing_dir = missing_dir.to_string_lossy().replace('\\', "/");
    fs::write(
        &source,
        format!(
            "import std.fs as fs\nimport std.array as array\n\nfunc main() -> i32:\n  let root = \"{missing_dir}\"\n  let items = fs.read_dir(root)\n  if (array.len(items) == 0):\n    return 0\n  return 1\n"
        ),
    )
    .expect("write source");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let status = Command::new(&binary).status().expect("run built binary");
    assert_eq!(status.code(), Some(0));
}

#[test]
fn build_and_run_std_array_module() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("main.tg");
    let binary = dir.path().join("array_stdlib");
    fs::write(
        &source,
        "import std.array as array\nimport std.string as string\n\nfunc main() -> i32:\n  let parts = string.split(\"alpha,beta,gamma\", \",\")\n  if (array.len(parts) == 3 and string.str_eq(array.get(parts, 1), \"beta\")):\n    return 0\n  return 1\n",
    )
    .expect("write source");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let status = Command::new(&binary).status().expect("run built binary");
    assert_eq!(status.code(), Some(0));
}

#[test]
fn build_and_run_std_path_module() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("main.tg");
    let binary = dir.path().join("path_stdlib");
    fs::write(
        &source,
        "import std.path as path\nimport std.string as string\n\nfunc main() -> i32:\n  let root = path.getcwd()\n  let joined = path.join(root, \"path-stdlib-probe.txt\")\n  if (path.is_dir(root) and string.contains(joined, \"path-stdlib-probe.txt\")):\n    return 0\n  return 1\n",
    )
    .expect("write source");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let status = Command::new(&binary).status().expect("run built binary");
    assert_eq!(status.code(), Some(0));
}

#[test]
fn build_and_run_std_path_empty_left_join() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("main.tg");
    let binary = dir.path().join("path_empty_left_stdlib");
    fs::write(
        &source,
        "import std.path as path\nimport std.string as string\n\nfunc main() -> i32:\n  let joined = path.join(\"\", \"leaf.txt\")\n  if (string.str_eq(joined, \"leaf.txt\")):\n    return 0\n  return 1\n",
    )
    .expect("write source");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let status = Command::new(&binary).status().expect("run built binary");
    assert_eq!(status.code(), Some(0));
}

#[test]
fn build_and_run_std_process_module() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("main.tg");
    let binary = dir.path().join("process_stdlib");
    fs::write(
        &source,
        "import std.process as process\nimport std.string as string\n\nfunc main() -> i32:\n  let env_value = process.env(\"THAGORE_STD_PROCESS_PROBE\")\n  let captured = string.trim(process.capture(\"echo bootstrap\"))\n  let argc = process.argc()\n  let argv0 = process.argv(0)\n  if (string.str_eq(env_value, \"ok\") and string.contains(captured, \"boot\") and argc >= 0 and string.len(argv0) > 0):\n    return 0\n  return 1\n",
    )
    .expect("write source");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let status = Command::new(&binary)
        .env("THAGORE_STD_PROCESS_PROBE", "ok")
        .status()
        .expect("run built binary");
    assert_eq!(status.code(), Some(0));
}

#[test]
fn build_and_run_std_process_missing_env_and_argv_gap() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("main.tg");
    let binary = dir.path().join("process_missing_env_stdlib");
    fs::write(
        &source,
        "import std.process as process\nimport std.string as string\n\nfunc main() -> i32:\n  let env_value = process.env(\"THAGORE_STD_PROCESS_PROBE_MISSING\")\n  let missing_argv = process.argv(999)\n  if (string.str_eq(env_value, \"\") and string.str_eq(missing_argv, \"\")):\n    return 0\n  return 1\n",
    )
    .expect("write source");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let status = Command::new(&binary).status().expect("run built binary");
    assert_eq!(status.code(), Some(0));
}

#[test]
fn build_and_run_std_math_module() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("main.tg");
    let binary = dir.path().join("math_stdlib");
    fs::write(
        &source,
        "import std.math as math\n\nfunc main() -> i32:\n  if (math.sqrt(16.0) == 4.0 and math.gcd(12, 18) == 6 and math.is_even(8) and math.abs(-5) == 5 and math.abs(-3.5) == 3.5 and math.min(3, 7) == 3 and math.max(3, 7) == 7 and math.clamp(9, 0, 5) == 5 and math.PI > 3.14 and math.E > 2.71 and math.MAX_I32 == 2147483647):\n    return 0\n  return 1\n",
    )
    .expect("write source");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let status = Command::new(&binary).status().expect("run built binary");
    assert_eq!(status.code(), Some(0));
}

#[test]
fn build_monomorphizes_generic_functions_once_per_concrete_type() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("main.tg");
    let binary = dir.path().join("generic_math");
    let llvm_ir = dir.path().join("generic_math.ll");
    fs::write(
        &source,
        "func abs<T: Numeric>(value: T) -> T:\n  if (value < 0):\n    return -value\n  return value\n\nfunc main() -> i32:\n  let left: i32 = abs(-1)\n  let right: i32 = abs(-2)\n  let third: i32 = abs(-3)\n  let decimal: f64 = abs(-3.5)\n  if (left == 1 and right == 2 and third == 3 and decimal == 3.5):\n    return 0\n  return 1\n",
    )
    .expect("write source");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
            "--emit",
            "ll,bin",
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let ir = fs::read_to_string(&llvm_ir).expect("read llvm ir");
    assert_eq!(ir.matches("define i32 @__thagore_abs_i32").count(), 1);
    assert_eq!(ir.matches("define double @__thagore_abs_f64").count(), 1);

    let status = Command::new(&binary).status().expect("run built binary");
    assert_eq!(status.code(), Some(0));
}

#[test]
fn build_and_run_std_io_module() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("main.tg");
    let binary = dir.path().join("io_stdlib");
    fs::write(
        &source,
        "import std.io as io\n\nfunc main() -> i32:\n  io.println(42)\n  io.println(true)\n  io.flush()\n  return 0\n",
    )
    .expect("write source");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary).output().expect("run built binary");
    assert_eq!(output.status.code(), Some(0));
    assert_eq!(
        String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n"),
        "42\ntrue\n"
    );
}

#[test]
fn build_and_run_std_io_streams_and_primitives() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("main.tg");
    let binary = dir.path().join("io_streams_stdlib");
    fs::write(
        &source,
        "import std.io as io\n\nfunc main() -> i32:\n  io.print(7)\n  io.println(true)\n  io.eprint(1.5)\n  io.eprintln(\"warn\")\n  io.flush()\n  return 0\n",
    )
    .expect("write source");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary).output().expect("run built binary");
    assert_eq!(output.status.code(), Some(0));
    assert_eq!(
        String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n"),
        "7true\n"
    );
    assert_eq!(
        String::from_utf8_lossy(&output.stderr).replace("\r\n", "\n"),
        "1.5warn\n"
    );
}

#[test]
fn build_and_run_bootstrap_seed_frontend() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

fn build_selfhost_frontend_binary(repo_root: &Path, binary: &Path) {
    let source = repo_root.join("bootstrap/selfhost/frontend/check.tg");
    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );
}

fn build_selfhost_frontend_main_binary(repo_root: &Path, binary: &Path) {
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );
}

fn build_selfhost_compiler_driver_binary(repo_root: &Path, binary: &Path) {
    let source = repo_root.join("bootstrap/selfhost/frontend/compiler.tg");
    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );
}

fn build_selfhost_frontend_parse_binary(repo_root: &Path, binary: &Path) {
    let source = repo_root.join("bootstrap/selfhost/frontend/parse.tg");
    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );
}

fn build_selfhost_frontend_irval_binary(repo_root: &Path, binary: &Path) {
    let source = repo_root.join("bootstrap/selfhost/frontend/irval.tg");
    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );
}

fn build_selfhost_frontend_lower_binary(repo_root: &Path, binary: &Path) {
    let source = repo_root.join("bootstrap/selfhost/frontend/lower.tg");
    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );
}

fn build_selfhost_frontend_scan_binary(repo_root: &Path, binary: &Path) {
    let source = repo_root.join("bootstrap/selfhost/frontend/scan.tg");
    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );
}

fn canonicalize_selfhost_frontend(stdout: &str) -> String {
    let marker = " || diagnostics=";
    let diagnostics = stdout
        .split(marker)
        .nth(1)
        .map(str::trim)
        .unwrap_or("");
    match diagnostics {
        "ok" => "ok".to_string(),
        "unknown return ident" | "unknown callee" | "assignment to unknown local" => {
            "unknown identifier".to_string()
        }
        "call arity mismatch" => "call arity mismatch".to_string(),
        "assignment type mismatch"
        | "assignment call result type mismatch"
        | "local type mismatch"
        | "local call result type mismatch" => {
            "type mismatch".to_string()
        }
        "missing import" => "missing import".to_string(),
        "unknown imported symbol" => "unknown imported symbol".to_string(),
        "condition type mismatch" => "condition type mismatch".to_string(),
        "return type mismatch" | "return call result type mismatch" => {
            "return type mismatch".to_string()
        }
        other => other.to_string(),
    }
}

fn canonicalize_rust_frontend(stderr: &str, status_code: Option<i32>) -> String {
    if status_code == Some(0) {
        return "ok".to_string();
    }
    if stderr.contains("argument count mismatch") {
        return "call arity mismatch".to_string();
    }
    if stderr.contains("module resolution failed") {
        return "missing import".to_string();
    }
    if stderr.contains("unresolved imported symbol") {
        return "unknown imported symbol".to_string();
    }
    if stderr.contains("condition must be bool") {
        return "condition type mismatch".to_string();
    }
    if stderr.contains("return type mismatch") {
        return "return type mismatch".to_string();
    }
    if stderr.contains("type mismatch") {
        return "type mismatch".to_string();
    }
    if stderr.contains("unknown identifier") {
        return "unknown identifier".to_string();
    }
    stderr.trim().to_string()
}

fn frontend_report_prefix(stdout: &str) -> &str {
    stdout.split(" || diagnostics=").next().unwrap_or(stdout)
}

fn run_stage_binary(binary: &Path, repo_root: &Path, fixture: &str, kind: &str) -> (String, Option<i32>) {
    let sample = repo_root.join(fixture);
    let output = Command::new(binary)
        .current_dir(repo_root)
        .args([sample.to_str().expect("utf8"), kind])
        .output()
        .unwrap_or_else(|error| panic!("run stage for {fixture}: {error}"));
    (
        String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n"),
        output.status.code(),
    )
}

fn load_corpus_manifest(repo_root: &Path, manifest: &str) -> Vec<Vec<String>> {
    fs::read_to_string(repo_root.join(manifest))
        .unwrap_or_else(|error| panic!("read corpus manifest {manifest}: {error}"))
        .replace("\r\n", "\n")
        .lines()
        .filter(|line| {
            let trimmed = line.trim();
            !trimmed.is_empty() && !trimmed.starts_with('#')
        })
        .map(|line| line.split('|').map(|part| part.trim().to_string()).collect())
        .collect()
}

fn resolve_main_orchestration_path(repo_root: &Path, raw: &str) -> Option<String> {
    if raw.is_empty() {
        return None;
    }
    if let Some(relative) = raw.strip_prefix("@abs:") {
        return Some(
            repo_root
                .join(relative)
                .canonicalize()
                .expect("canonicalize absolute orchestration path")
                .to_string_lossy()
                .replace('\\', "/"),
        );
    }
    Some(raw.to_string())
}

fn resolve_compiler_driver_path(repo_root: &Path, raw: &str) -> Option<String> {
    if raw.is_empty() {
        return None;
    }
    if let Some(relative) = raw.strip_prefix("@abs:") {
        return Some(
            repo_root
                .join(relative)
                .canonicalize()
                .expect("canonicalize absolute compiler driver path")
                .to_string_lossy()
                .replace('\\', "/"),
        );
    }
    Some(raw.to_string())
}

fn runtime_object_name_for_artifact(artifact_name: &str) -> String {
    let stem = artifact_name.strip_suffix(".exe").unwrap_or(artifact_name);
    format!("{stem}.thagore_rt.o")
}

fn assert_compiler_driver_manifest_matches(repo_root: &Path, binary: &Path, manifest: &str) {
    let cases = load_corpus_manifest(repo_root, manifest);
    let scratch = TempDir::new().expect("compiler driver scratch");
    for case in cases {
        let label = &case[0];
        let cwd = if case[1].is_empty() || case[1] == "." {
            repo_root.to_path_buf()
        } else {
            repo_root.join(&case[1])
        };
        let command_name = &case[2];
        let path_arg = resolve_compiler_driver_path(repo_root, &case[3]);
        let kind = &case[4];
        let extra = &case[5];
        let expected_exit: i32 = case[6].parse().expect("parse expected exit");
        let expected_path = repo_root.join(&case[7]);

        let mut command = Command::new(binary);
        let stage0 = Path::new(env!("CARGO_BIN_EXE_thagc"));
        let path_sep = if cfg!(windows) { ";" } else { ":" };
        let inherited_path = std::env::var("PATH").unwrap_or_default();
        let stage0_parent = stage0
            .parent()
            .expect("stage0 parent")
            .to_string_lossy()
            .into_owned();
        command
            .current_dir(&cwd)
            .env(
                "PATH",
                format!("{stage0_parent}{path_sep}{inherited_path}"),
            )
            .env(
                "THAGORE_STAGE0",
                stage0.file_name().and_then(|name| name.to_str()).expect("stage0 file name"),
            )
            .env("THAGORE_SELFHOST_TMP", scratch.path())
            .arg(command_name);
        if let Some(path_arg) = path_arg {
            command.arg(path_arg);
        }
        if !kind.is_empty() {
            command.arg(kind);
        }
        if !extra.is_empty() {
            command.arg(extra);
        }

        let output = command
            .output()
            .unwrap_or_else(|error| panic!("run compiler driver case {label}: {error}"));
        assert_eq!(
            output.status.code(),
            Some(expected_exit),
            "unexpected exit code for compiler driver case {label}"
        );

        let expected = fs::read_to_string(expected_path)
            .expect("read expected")
            .replace("\r\n", "\n");
        let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
        assert_eq!(
            actual.trim_end(),
            expected.trim_end(),
            "unexpected stdout for compiler driver case {label}"
        );
        if command_name == "build" {
            let artifact_path = scratch.path().join(extra);
            assert!(
                artifact_path.exists(),
                "missing compiler driver build artifact for {label}: {}",
                artifact_path.display()
            );
            let _ = fs::remove_file(&artifact_path);
        }
    }
}

fn assert_bootstrap_artifact_manifest_matches(repo_root: &Path, compiler_binary: &Path, manifest: &str) {
    let cases = load_corpus_manifest(repo_root, manifest);
    let scratch = TempDir::new().expect("bootstrap artifact scratch");
    let stage0 = Path::new(env!("CARGO_BIN_EXE_thagc"));
    let path_sep = if cfg!(windows) { ";" } else { ":" };
    let inherited_path = std::env::var("PATH").unwrap_or_default();
    let stage0_parent = stage0
        .parent()
        .expect("stage0 parent")
        .to_string_lossy()
        .into_owned();
    let mut build_cache: HashMap<(String, String, String), std::path::PathBuf> = HashMap::new();

    let build_artifact = |builder: &Path,
                          source_path: &Path,
                          output_name: &str,
                          cwd: &Path,
                          build_cache: &mut HashMap<(String, String, String), std::path::PathBuf>|
     -> std::path::PathBuf {
        let key = (
            builder.display().to_string(),
            source_path.display().to_string(),
            output_name.to_string(),
        );
        if let Some(path) = build_cache.get(&key) {
            assert!(path.exists(), "cached bootstrap artifact missing: {}", path.display());
            return path.clone();
        }
        let artifact_path = scratch.path().join(output_name);
        if artifact_path.exists() {
            let _ = fs::remove_file(&artifact_path);
        }
        let build_output = Command::new(builder)
            .current_dir(cwd)
            .env(
                "PATH",
                format!("{stage0_parent}{path_sep}{inherited_path}"),
            )
            .env(
                "THAGORE_STAGE0",
                stage0.file_name().and_then(|name| name.to_str()).expect("stage0 file name"),
            )
            .env("THAGORE_SELFHOST_TMP", scratch.path())
            .args(["build", source_path.to_str().expect("utf8"), output_name])
            .output()
            .unwrap_or_else(|error| panic!("build bootstrap artifact {output_name}: {error}"));
        assert_eq!(
            build_output.status.code(),
            Some(0),
            "bootstrap artifact build failed for {output_name}\nstdout:\n{}\nstderr:\n{}",
            String::from_utf8_lossy(&build_output.stdout),
            String::from_utf8_lossy(&build_output.stderr)
        );
        assert!(
            artifact_path.exists(),
            "bootstrap artifact missing: {}",
            artifact_path.display()
        );
        build_cache.insert(key, artifact_path.clone());
        artifact_path
    };

    for case in cases {
        let label = &case[0];
        let source = repo_root.join(&case[1]);
        let artifact_name = &case[2];
        let cwd = if case[3].is_empty() || case[3] == "." {
            repo_root.to_path_buf()
        } else {
            repo_root.join(&case[3])
        };
        let invoke = &case[4];
        let path_arg = resolve_compiler_driver_path(repo_root, &case[5]);
        let kind = &case[6];
        let mode = &case[7];
        let expected_exit: i32 = case[8].parse().expect("parse expected exit");
        let expected_path = repo_root.join(&case[9]);
        let artifact_path = build_artifact(compiler_binary, &source, artifact_name, repo_root, &mut build_cache);

        let output = if invoke == "version" {
            Command::new(&artifact_path)
                .current_dir(&cwd)
                .arg("version")
                .output()
                .unwrap_or_else(|error| panic!("run bootstrap artifact case {label}: {error}"))
        } else if invoke == "exec" {
            let mut command = Command::new(&artifact_path);
            command.current_dir(&cwd);
            if let Some(path_arg) = path_arg {
                command.arg(path_arg);
            }
            if !kind.is_empty() {
                command.arg(kind);
            }
            if !mode.is_empty() {
                command.arg(mode);
            }
            command
                .output()
                .unwrap_or_else(|error| panic!("run bootstrap artifact case {label}: {error}"))
        } else if invoke == "build-version"
            || invoke == "build-exec"
            || invoke == "build-build-version"
            || invoke == "build-build-exec"
            || invoke == "build-build-sidecar"
        {
            let nested_source = path_arg
                .as_ref()
                .unwrap_or_else(|| panic!("missing nested source for bootstrap artifact case {label}"));
            let nested_source_path = Path::new(nested_source);
            let nested_artifact =
                build_artifact(&artifact_path, nested_source_path, kind, &cwd, &mut build_cache);
            let nested_args: Vec<String> = if mode.is_empty() {
                Vec::new()
            } else {
                mode.split(";;")
                    .filter(|part| !part.is_empty())
                    .map(|part| part.to_string())
                    .collect()
            };
            let nested_output = if invoke == "build-version" || invoke == "build-exec" {
                let mut nested_command = Command::new(&nested_artifact);
                nested_command.current_dir(&cwd);
                nested_command.env(
                    "PATH",
                    format!("{stage0_parent}{path_sep}{inherited_path}"),
                );
                nested_command.env(
                    "THAGORE_STAGE0",
                    stage0.file_name().and_then(|name| name.to_str()).expect("stage0 file name"),
                );
                nested_command.env("THAGORE_SELFHOST_TMP", scratch.path());
                if invoke == "build-version" {
                    nested_command.arg("version");
                } else {
                    for arg in &nested_args {
                        nested_command.arg(arg);
                    }
                }
                nested_command
                    .output()
                    .unwrap_or_else(|error| panic!("run nested bootstrap artifact case {label}: {error}"))
            } else if invoke == "build-build-version" || invoke == "build-build-exec" {
                assert!(
                    nested_args.len() >= 2,
                    "build-build bootstrap artifact case requires at least source and artifact name: {label}"
                );
                let second_source = cwd.join(&nested_args[0]);
                let second_artifact_name = &nested_args[1];
                let second_artifact =
                    build_artifact(&nested_artifact, &second_source, second_artifact_name, &cwd, &mut build_cache);
                let mut second_command = Command::new(&second_artifact);
                second_command.current_dir(&cwd);
                second_command.env(
                    "PATH",
                    format!("{stage0_parent}{path_sep}{inherited_path}"),
                );
                second_command.env(
                    "THAGORE_STAGE0",
                    stage0.file_name().and_then(|name| name.to_str()).expect("stage0 file name"),
                );
                second_command.env("THAGORE_SELFHOST_TMP", scratch.path());
                if invoke == "build-build-version" {
                    second_command.arg("version");
                } else {
                    for arg in nested_args.iter().skip(2) {
                        second_command.arg(arg);
                    }
                }
                let output = second_command
                    .output()
                    .unwrap_or_else(|error| panic!("run second nested bootstrap artifact case {label}: {error}"));
                output
            } else {
                assert!(
                    nested_args.len() >= 5,
                    "build-build-sidecar bootstrap artifact case requires source, artifact, command, fixture, and output artifact name: {label}"
                );
                let second_source = cwd.join(&nested_args[0]);
                let second_artifact_name = &nested_args[1];
                let second_artifact =
                    build_artifact(&nested_artifact, &second_source, second_artifact_name, &cwd, &mut build_cache);
                let command_name = &nested_args[2];
                let fixture_path = &nested_args[3];
                let output_artifact_name = &nested_args[4];
                let sidecar_suffix = nested_args.get(5).cloned().unwrap_or_default();
                let target_sidecar = scratch.path().join(format!("{output_artifact_name}{sidecar_suffix}"));
                if target_sidecar.exists() {
                    fs::remove_file(&target_sidecar)
                        .unwrap_or_else(|error| panic!("remove stale sidecar for bootstrap artifact case {label}: {error}"));
                }
                let mut second_command = Command::new(&second_artifact);
                second_command.current_dir(&cwd);
                second_command.env(
                    "PATH",
                    format!("{stage0_parent}{path_sep}{inherited_path}"),
                );
                second_command.env(
                    "THAGORE_STAGE0",
                    stage0.file_name().and_then(|name| name.to_str()).expect("stage0 file name"),
                );
                second_command.env("THAGORE_SELFHOST_TMP", scratch.path());
                second_command.arg(command_name);
                second_command.arg(fixture_path);
                second_command.arg(output_artifact_name);
                let output = second_command
                    .output()
                    .unwrap_or_else(|error| panic!("run second nested sidecar bootstrap artifact case {label}: {error}"));
                if !output.status.success() {
                    output
                } else {
                    let stdout = fs::read_to_string(&target_sidecar).unwrap_or_else(|error| {
                        panic!(
                            "read nested sidecar bootstrap artifact case {label} from {}: {error}",
                            target_sidecar.display()
                        )
                    });
                    std::process::Output {
                        status: output.status,
                        stdout: stdout.into_bytes(),
                        stderr: output.stderr,
                    }
                }
            };
            nested_output
        } else {
            panic!("unsupported bootstrap artifact invoke mode: {invoke}");
        };
        assert_eq!(
            output.status.code(),
            Some(expected_exit),
            "unexpected exit code for bootstrap artifact case {label}"
        );

        let expected = fs::read_to_string(expected_path)
            .expect("read expected")
            .replace("\r\n", "\n");
        let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
        assert_eq!(
            actual.trim_end(),
            expected.trim_end(),
            "unexpected stdout for bootstrap artifact case {label}"
        );
    }
}

fn assert_backend_adapter_artifact_manifest_matches(repo_root: &Path, compiler_binary: &Path, manifest: &str) {
    let cases = load_corpus_manifest(repo_root, manifest);
    let scratch = TempDir::new().expect("backend adapter artifact scratch");
    let stage0 = Path::new(env!("CARGO_BIN_EXE_thagc"));
    let path_sep = if cfg!(windows) { ";" } else { ":" };
    let inherited_path = std::env::var("PATH").unwrap_or_default();
    let stage0_parent = stage0
        .parent()
        .expect("stage0 parent")
        .to_string_lossy()
        .into_owned();

    for case in cases {
        let label = &case[0];
        let cwd = if case[1].is_empty() || case[1] == "." {
            repo_root.to_path_buf()
        } else {
            repo_root.join(&case[1])
        };
        let command_name = &case[2];
        let path_arg = resolve_compiler_driver_path(repo_root, &case[3]).expect("path arg");
        let kind = &case[4];
        let artifact_name = &case[5];
        let expected_exit: i32 = case[6].parse().expect("parse expected exit");
        let stdout_expected = repo_root.join(&case[7]);
        let phase_expected = repo_root.join(&case[8]);
        let frontend_expected = repo_root.join(&case[9]);
        let plan_expected = repo_root.join(&case[10]);
        let adapter_expected = repo_root.join(&case[11]);
        let lowered_expected = repo_root.join(&case[12]);
        let emit_expected = repo_root.join(&case[13]);
        let link_expected = repo_root.join(&case[14]);
        let verify_expected = repo_root.join(&case[15]);
        let host_expected = repo_root.join(&case[16]);
        let artifact_stdout_expected = &case[17];
        let artifact_path = scratch.path().join(artifact_name);
        let runtime_object_path = scratch
            .path()
            .join(runtime_object_name_for_artifact(artifact_name));
        let phase_path = scratch.path().join(format!("{artifact_name}.phase.txt"));
        let frontend_path = scratch.path().join(format!("{artifact_name}.frontend.txt"));
        let plan_path = scratch.path().join(format!("{artifact_name}.plan.txt"));
        let adapter_path = scratch.path().join(format!("{artifact_name}.adapter.txt"));
        let lowered_path = scratch.path().join(format!("{artifact_name}.lowered.txt"));
        let emit_path = scratch.path().join(format!("{artifact_name}.emit.txt"));
        let link_path = scratch.path().join(format!("{artifact_name}.link.txt"));
        let verify_path = scratch.path().join(format!("{artifact_name}.verify.txt"));
        let host_path = scratch.path().join(format!("{artifact_name}.host.txt"));
        for path in [
            &artifact_path,
            &runtime_object_path,
            &phase_path,
            &frontend_path,
            &plan_path,
            &adapter_path,
            &lowered_path,
            &emit_path,
            &link_path,
            &verify_path,
            &host_path,
        ] {
            let _ = fs::remove_file(path);
        }

        let mut command = Command::new(compiler_binary);
        command
            .current_dir(&cwd)
            .env(
                "PATH",
                format!("{stage0_parent}{path_sep}{inherited_path}"),
            )
            .env(
                "THAGORE_STAGE0",
                stage0.file_name().and_then(|name| name.to_str()).expect("stage0 file name"),
            )
            .env("THAGORE_SELFHOST_TMP", scratch.path())
            .arg(command_name)
            .arg(path_arg);
        if !kind.is_empty() {
            command.arg(kind);
        }
        command.arg(artifact_name);

        let output = command
            .output()
            .unwrap_or_else(|error| panic!("run backend adapter artifact case {label}: {error}"));
        assert_eq!(
            output.status.code(),
            Some(expected_exit),
            "unexpected exit code for backend adapter artifact case {label}"
        );
        let expected_stdout = fs::read_to_string(stdout_expected)
            .expect("read expected stdout")
            .replace("\r\n", "\n");
        let actual_stdout = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
        assert_eq!(
            actual_stdout.trim_end(),
            expected_stdout.trim_end(),
            "unexpected stdout for backend adapter artifact case {label}"
        );
        let expected_phase = fs::read_to_string(phase_expected)
            .expect("read expected phase")
            .replace("\r\n", "\n");
        let actual_phase = fs::read_to_string(&phase_path)
            .expect("read actual phase")
            .replace("\r\n", "\n");
        assert_eq!(
            actual_phase.trim_end(),
            expected_phase.trim_end(),
            "unexpected phase artifact for backend adapter case {label}"
        );
        let expected_frontend = fs::read_to_string(frontend_expected)
            .expect("read expected frontend")
            .replace("\r\n", "\n");
        let actual_frontend = fs::read_to_string(&frontend_path)
            .expect("read actual frontend")
            .replace("\r\n", "\n");
        assert_eq!(
            actual_frontend.trim_end(),
            expected_frontend.trim_end(),
            "unexpected frontend artifact for backend adapter case {label}"
        );
        let expected_plan = fs::read_to_string(plan_expected)
            .expect("read expected plan")
            .replace("\r\n", "\n");
        let actual_plan = fs::read_to_string(&plan_path)
            .expect("read actual plan")
            .replace("\r\n", "\n");
        assert_eq!(
            actual_plan.trim_end(),
            expected_plan.trim_end(),
            "unexpected plan artifact for backend adapter case {label}"
        );
        let expected_adapter = fs::read_to_string(adapter_expected)
            .expect("read expected adapter")
            .replace("\r\n", "\n");
        let actual_adapter = fs::read_to_string(&adapter_path)
            .expect("read actual adapter")
            .replace("\r\n", "\n");
        assert_eq!(
            actual_adapter.trim_end(),
            expected_adapter.trim_end(),
            "unexpected adapter artifact for backend adapter case {label}"
        );
        let expected_lowered = fs::read_to_string(lowered_expected)
            .expect("read expected lowered")
            .replace("\r\n", "\n");
        let actual_lowered = fs::read_to_string(&lowered_path)
            .expect("read actual lowered")
            .replace("\r\n", "\n");
        assert_eq!(
            actual_lowered.trim_end(),
            expected_lowered.trim_end(),
            "unexpected lowered artifact for backend adapter case {label}"
        );
        let expected_emit = fs::read_to_string(emit_expected)
            .expect("read expected emit")
            .replace("\r\n", "\n");
        let actual_emit = fs::read_to_string(&emit_path)
            .expect("read actual emit")
            .replace("\r\n", "\n");
        assert_eq!(
            actual_emit.trim_end(),
            expected_emit.trim_end(),
            "unexpected emission artifact for backend adapter case {label}"
        );
        let expected_link = fs::read_to_string(link_expected)
            .expect("read expected link")
            .replace("\r\n", "\n");
        let actual_link = fs::read_to_string(&link_path)
            .expect("read actual link")
            .replace("\r\n", "\n");
        assert_eq!(
            actual_link.trim_end(),
            expected_link.trim_end(),
            "unexpected link artifact for backend adapter case {label}"
        );
        let expected_verify = fs::read_to_string(verify_expected)
            .expect("read expected verify")
            .replace("\r\n", "\n");
        let actual_verify = fs::read_to_string(&verify_path)
            .expect("read actual verify")
            .replace("\r\n", "\n");
        assert_eq!(
            actual_verify.trim_end(),
            expected_verify.trim_end(),
            "unexpected verify artifact for backend adapter case {label}"
        );
        let expected_host = fs::read_to_string(host_expected)
            .expect("read expected host command")
            .replace("\r\n", "\n");
        let actual_host = fs::read_to_string(&host_path)
            .expect("read actual host command")
            .replace("\r\n", "\n");
        assert_eq!(
            actual_host.trim_end(),
            expected_host.trim_end(),
            "unexpected host command artifact for backend adapter case {label}"
        );
        if command_name == "build" && expected_exit == 0 {
            assert!(
                artifact_path.exists(),
                "missing backend adapter build artifact for {label}: {}",
                artifact_path.display()
            );
            assert!(
                runtime_object_path.exists(),
                "missing backend adapter runtime object for {label}: {}",
                runtime_object_path.display()
            );
            if !artifact_stdout_expected.is_empty() {
                let built = Command::new(&artifact_path)
                    .current_dir(&cwd)
                    .output()
                    .unwrap_or_else(|error| panic!("run built backend adapter artifact for {label}: {error}"));
                assert_eq!(
                    built.status.code(),
                    Some(0),
                    "unexpected built artifact exit for backend adapter case {label}"
                );
                let expected_artifact_stdout = fs::read_to_string(repo_root.join(artifact_stdout_expected))
                    .expect("read expected artifact stdout")
                    .replace("\r\n", "\n");
                let actual_artifact_stdout = String::from_utf8_lossy(&built.stdout).replace("\r\n", "\n");
                assert_eq!(
                    actual_artifact_stdout.trim_end(),
                    expected_artifact_stdout.trim_end(),
                    "unexpected built artifact stdout for backend adapter case {label}"
                );
            }
        } else if expected_exit == 0 {
            assert!(
                runtime_object_path.exists(),
                "missing backend adapter runtime object for {label}: {}",
                runtime_object_path.display()
            );
        }
    }
}

fn assert_lowering_manifest_matches(repo_root: &Path, binary: &Path, manifest: &str) {
    let cases = load_corpus_manifest(repo_root, manifest);
    for case in cases {
        let fixture = &case[0];
        let expected_path = repo_root.join(&case[1]);
        let sample = repo_root.join(fixture);

        let output = Command::new(binary)
            .current_dir(repo_root)
            .arg(sample.to_str().expect("utf8"))
            .output()
            .unwrap_or_else(|error| panic!("run lowering slice for {fixture}: {error}"));
        assert_eq!(output.status.code(), Some(0), "lowering slice failed for {fixture}");

        let expected = fs::read_to_string(expected_path)
            .expect("read expected")
            .replace("\r\n", "\n");
        let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
        assert_eq!(
            actual.trim_end(),
            expected.trim_end(),
            "unexpected lowering slice output for {fixture}"
        );
    }
}

fn assert_main_orchestration_manifest_matches(repo_root: &Path, binary: &Path, manifest: &str) {
    let cases = load_corpus_manifest(repo_root, manifest);
    for case in cases {
        let label = &case[0];
        let cwd = if case[1].is_empty() || case[1] == "." {
            repo_root.to_path_buf()
        } else {
            repo_root.join(&case[1])
        };
        let path_arg = resolve_main_orchestration_path(repo_root, &case[2]);
        let kind = &case[3];
        let mode = &case[4];
        let expected_exit: i32 = case[5].parse().expect("parse expected exit");
        let expected_path = repo_root.join(&case[6]);

        let mut command = Command::new(binary);
        command.current_dir(&cwd);
        if let Some(path_arg) = path_arg {
            command.arg(path_arg);
        }
        if !kind.is_empty() {
            command.arg(kind);
        }
        if !mode.is_empty() {
            command.arg(mode);
        }

        let output = command
            .output()
            .unwrap_or_else(|error| panic!("run orchestration case {label}: {error}"));
        assert_eq!(
            output.status.code(),
            Some(expected_exit),
            "unexpected exit code for orchestration case {label}"
        );

        let expected = fs::read_to_string(expected_path)
            .expect("read expected")
            .replace("\r\n", "\n");
        let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
        assert_eq!(
            actual.trim_end(),
            expected.trim_end(),
            "unexpected stdout for orchestration case {label}"
        );
    }
}

fn assert_main_manifest_matches(
    repo_root: &Path,
    binary: &Path,
    manifest: &str,
    mode: Option<&str>,
) {
    let cases = load_corpus_manifest(repo_root, manifest);
    for case in cases {
        let fixture = &case[0];
        let kind = &case[1];
        let expected_path = repo_root.join(&case[2]);
        let sample = repo_root.join(fixture);

        let mut command = Command::new(binary);
        command
            .current_dir(repo_root)
            .arg(sample.to_str().expect("utf8"))
            .arg(kind);
        if let Some(mode) = mode {
            command.arg(mode);
        }

        let output = command
            .output()
            .unwrap_or_else(|error| panic!("run main target for {fixture}: {error}"));
        assert_eq!(
            output.status.code(),
            Some(0),
            "main target failed for {fixture} in mode {:?}",
            mode
        );

        let expected = fs::read_to_string(expected_path)
            .expect("read expected")
            .replace("\r\n", "\n");
        let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
        assert_eq!(
            actual.trim_end(),
            expected.trim_end(),
            "unexpected main target output for {fixture} in mode {:?}",
            mode
        );
    }
}

#[test]
fn selfhost_compiler_driver_contract_matches_goldens() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-selfhost-compiler");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    build_selfhost_compiler_driver_binary(&repo_root, &binary);

    assert_compiler_driver_manifest_matches(
        &repo_root,
        &binary,
        "bootstrap/selfhost/corpus/compiler-driver-contract.txt",
    );
}

#[test]
fn selfhost_compiler_phase_contract_matches_goldens() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-selfhost-compiler");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    build_selfhost_compiler_driver_binary(&repo_root, &binary);

    assert_compiler_driver_manifest_matches(
        &repo_root,
        &binary,
        "bootstrap/selfhost/corpus/compiler-phase-contract.txt",
    );
}

#[test]
fn selfhost_backend_adapter_contract_matches_goldens() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-selfhost-compiler");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    build_selfhost_compiler_driver_binary(&repo_root, &binary);

    assert_compiler_driver_manifest_matches(
        &repo_root,
        &binary,
        "bootstrap/selfhost/corpus/backend-adapter-contract.txt",
    );
}

#[test]
fn selfhost_backend_adapter_artifacts_match_goldens() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-selfhost-compiler");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    build_selfhost_compiler_driver_binary(&repo_root, &binary);

    assert_backend_adapter_artifact_manifest_matches(
        &repo_root,
        &binary,
        "bootstrap/selfhost/corpus/backend-adapter-artifacts.txt",
    );
}

#[test]
fn selfhost_bootstrap_artifact_contract_matches_goldens() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-selfhost-compiler");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    build_selfhost_compiler_driver_binary(&repo_root, &binary);

    assert_bootstrap_artifact_manifest_matches(
        &repo_root,
        &binary,
        "bootstrap/selfhost/corpus/bootstrap-artifact-contract.txt",
    );
}

#[test]
fn selfhost_lowering_slice_matches_goldens() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-selfhost-lower");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    build_selfhost_frontend_lower_binary(&repo_root, &binary);

    assert_lowering_manifest_matches(
        &repo_root,
        &binary,
        "bootstrap/selfhost/corpus/lowering-slice.txt",
    );
}

#[test]
fn selfhost_irval_matches_goldens() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-selfhost-irval");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    build_selfhost_frontend_irval_binary(&repo_root, &binary);

    assert_lowering_manifest_matches(
        &repo_root,
        &binary,
        "bootstrap/selfhost/corpus/lowering-validate.txt",
    );
}

#[test]
fn selfhost_frontend_main_target_manifests_match_goldens() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-selfhost-main");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    build_selfhost_frontend_main_binary(&repo_root, &binary);

    assert_main_manifest_matches(
        &repo_root,
        &binary,
        "bootstrap/selfhost/corpus/bootstrap-analyze.txt",
        None,
    );
    assert_main_manifest_matches(
        &repo_root,
        &binary,
        "bootstrap/selfhost/corpus/frontend-analyze.txt",
        None,
    );
    assert_main_manifest_matches(
        &repo_root,
        &binary,
        "bootstrap/selfhost/corpus/bootstrap-desugar.txt",
        Some("dump-desugared"),
    );
    assert_main_manifest_matches(
        &repo_root,
        &binary,
        "bootstrap/selfhost/corpus/bootstrap-report.txt",
        Some("dump-report"),
    );
    assert_main_manifest_matches(
        &repo_root,
        &binary,
        "bootstrap/selfhost/corpus/frontend-report.txt",
        Some("dump-report"),
    );
    assert_main_orchestration_manifest_matches(
        &repo_root,
        &binary,
        "bootstrap/selfhost/corpus/frontend-driver-orchestration.txt",
    );
}

#[test]
fn bootstrap_selfhost_frontend_matches_rust_frontend_on_narrow_corpus() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-selfhost-frontend");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    build_selfhost_frontend_binary(&repo_root, &binary);
    let cases = load_corpus_manifest(&repo_root, "bootstrap/selfhost/corpus/frontend-differential.txt");

    for case in cases {
        let fixture = &case[0];
        let kind = if case.len() >= 3 { &case[1] } else { "exe" };
        let expected = if case.len() >= 3 { &case[2] } else { &case[1] };
        let sample = repo_root.join(fixture);

        let selfhost = Command::new(&binary)
            .current_dir(&repo_root)
            .args([sample.to_str().expect("utf8"), kind])
            .output()
            .unwrap_or_else(|error| panic!("run selfhost frontend for {fixture}: {error}"));
        assert_eq!(selfhost.status.code(), Some(0), "selfhost failed for {fixture}");
        let selfhost_stdout = String::from_utf8_lossy(&selfhost.stdout).replace("\r\n", "\n");
        let selfhost_label = canonicalize_selfhost_frontend(&selfhost_stdout);

        let rust = Command::new(env!("CARGO_BIN_EXE_thagc"))
            .args([
                "check",
                sample.to_str().expect("utf8"),
                "--selfhost-replacement-kind",
                kind,
            ])
            .output()
            .unwrap_or_else(|error| panic!("run rust frontend for {fixture}: {error}"));
        let rust_stderr = String::from_utf8_lossy(&rust.stderr).replace("\r\n", "\n");
        let rust_label = canonicalize_rust_frontend(&rust_stderr, rust.status.code());

        assert_eq!(selfhost_label, expected.as_str(), "unexpected selfhost label for {fixture}\nstdout:\n{selfhost_stdout}");
        assert_eq!(rust_label, expected.as_str(), "unexpected rust label for {fixture}\nstderr:\n{rust_stderr}");
        assert_eq!(selfhost_label, rust_label, "frontend drift for {fixture}");
    }
}

#[test]
fn dump_selfhost_frontend_reports_match_goldens() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-selfhost-frontend");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    build_selfhost_frontend_binary(&repo_root, &binary);

    let cases = load_corpus_manifest(&repo_root, "bootstrap/selfhost/corpus/frontend-report.txt");

    for case in cases {
        let fixture = &case[0];
        let kind = &case[1];
        let expected_path = repo_root.join(&case[2]);
        let sample = repo_root.join(fixture);

        let output = Command::new(&binary)
            .current_dir(&repo_root)
            .args([sample.to_str().expect("utf8"), kind, "dump-report"])
            .output()
            .unwrap_or_else(|error| panic!("run dump-report for {fixture}: {error}"));
        assert_eq!(output.status.code(), Some(0), "dump-report failed for {fixture}");

        let expected = fs::read_to_string(expected_path)
            .expect("read expected")
            .replace("\r\n", "\n");
        let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
        assert_eq!(
            actual.trim_end(),
            expected.trim_end(),
            "unexpected report dump for {fixture}"
        );
    }
}

#[test]
fn dump_selfhost_frontend_parse_reports_match_goldens() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-selfhost-parse");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    build_selfhost_frontend_parse_binary(&repo_root, &binary);

    let cases = load_corpus_manifest(&repo_root, "bootstrap/selfhost/corpus/frontend-parse.txt");

    for case in cases {
        let fixture = &case[0];
        let kind = &case[1];
        let expected_path = repo_root.join(&case[2]);
        let sample = repo_root.join(fixture);

        let output = Command::new(&binary)
            .current_dir(&repo_root)
            .args([sample.to_str().expect("utf8"), kind])
            .output()
            .unwrap_or_else(|error| panic!("run parse stage for {fixture}: {error}"));
        assert_eq!(output.status.code(), Some(0), "parse stage failed for {fixture}");

        let expected = fs::read_to_string(expected_path)
            .expect("read expected")
            .replace("\r\n", "\n");
        let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
        assert_eq!(
            actual.trim_end(),
            expected.trim_end(),
            "unexpected parse-stage output for {fixture}"
        );
    }
}

#[test]
fn dump_selfhost_frontend_scan_reports_match_goldens() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-selfhost-scan");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    build_selfhost_frontend_scan_binary(&repo_root, &binary);

    let cases = load_corpus_manifest(&repo_root, "bootstrap/selfhost/corpus/frontend-scan.txt");

    for case in cases {
        let fixture = &case[0];
        let kind = &case[1];
        let expected_path = repo_root.join(&case[2]);
        let sample = repo_root.join(fixture);

        let output = Command::new(&binary)
            .current_dir(&repo_root)
            .args([sample.to_str().expect("utf8"), kind])
            .output()
            .unwrap_or_else(|error| panic!("run scan stage for {fixture}: {error}"));
        assert_eq!(output.status.code(), Some(0), "scan stage failed for {fixture}");

        let expected = fs::read_to_string(expected_path)
            .expect("read expected")
            .replace("\r\n", "\n");
        let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
        assert_eq!(
            actual.trim_end(),
            expected.trim_end(),
            "unexpected scan-stage output for {fixture}"
        );
    }
}

#[test]
fn build_selfhost_frontend_stage_chain() {
    let dir = TempDir::new().expect("temp dir");
    let scan_binary = dir.path().join("bootstrap-selfhost-scan");
    let parse_binary = dir.path().join("bootstrap-selfhost-parse");
    let check_binary = dir.path().join("bootstrap-selfhost-check");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/frontend/ok_helper_call.tg");

    build_selfhost_frontend_scan_binary(&repo_root, &scan_binary);
    build_selfhost_frontend_parse_binary(&repo_root, &parse_binary);
    build_selfhost_frontend_binary(&repo_root, &check_binary);

    let scan = Command::new(&scan_binary)
        .current_dir(&repo_root)
        .args([sample.to_str().expect("utf8"), "exe"])
        .output()
        .expect("run scan stage");
    assert_eq!(scan.status.code(), Some(0));
    let scan_stdout = String::from_utf8_lossy(&scan.stdout).replace("\r\n", "\n");
    assert!(scan_stdout.starts_with("tokens="), "unexpected scan output: {scan_stdout}");

    let parse = Command::new(&parse_binary)
        .current_dir(&repo_root)
        .args([sample.to_str().expect("utf8"), "exe"])
        .output()
        .expect("run parse stage");
    assert_eq!(parse.status.code(), Some(0));
    let parse_stdout = String::from_utf8_lossy(&parse.stdout).replace("\r\n", "\n");
    assert!(
        frontend_report_prefix(parse_stdout.trim_end()).starts_with(scan_stdout.trim_end()),
        "parse output does not extend scan output\nscan:\n{scan_stdout}\nparse:\n{parse_stdout}"
    );

    let check = Command::new(&check_binary)
        .current_dir(&repo_root)
        .args([sample.to_str().expect("utf8"), "exe"])
        .output()
        .expect("run check stage");
    assert_eq!(check.status.code(), Some(0));
    let check_stdout = String::from_utf8_lossy(&check.stdout).replace("\r\n", "\n");
    assert!(
        frontend_report_prefix(check_stdout.trim_end())
            .starts_with(frontend_report_prefix(parse_stdout.trim_end())),
        "check output does not extend parse output\nparse:\n{parse_stdout}\ncheck:\n{check_stdout}"
    );
    assert!(check_stdout.contains(" || diagnostics=ok"), "unexpected check output: {check_stdout}");
}

#[test]
fn build_selfhost_frontend_stage_chain_error() {
    let dir = TempDir::new().expect("temp dir");
    let scan_binary = dir.path().join("bootstrap-selfhost-scan");
    let parse_binary = dir.path().join("bootstrap-selfhost-parse");
    let check_binary = dir.path().join("bootstrap-selfhost-check");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/frontend/err_assignment_call_result_type.tg");

    build_selfhost_frontend_scan_binary(&repo_root, &scan_binary);
    build_selfhost_frontend_parse_binary(&repo_root, &parse_binary);
    build_selfhost_frontend_binary(&repo_root, &check_binary);

    let scan = Command::new(&scan_binary)
        .current_dir(&repo_root)
        .args([sample.to_str().expect("utf8"), "exe"])
        .output()
        .expect("run scan stage");
    assert_eq!(scan.status.code(), Some(0));
    let scan_stdout = String::from_utf8_lossy(&scan.stdout).replace("\r\n", "\n");
    assert!(scan_stdout.starts_with("tokens="), "unexpected scan output: {scan_stdout}");

    let parse = Command::new(&parse_binary)
        .current_dir(&repo_root)
        .args([sample.to_str().expect("utf8"), "exe"])
        .output()
        .expect("run parse stage");
    assert_eq!(parse.status.code(), Some(0));
    let parse_stdout = String::from_utf8_lossy(&parse.stdout).replace("\r\n", "\n");
    assert!(
        frontend_report_prefix(parse_stdout.trim_end()).starts_with(scan_stdout.trim_end()),
        "parse output does not extend scan output\nscan:\n{scan_stdout}\nparse:\n{parse_stdout}"
    );
    assert!(
        parse_stdout.contains(" || diagnostics=ok"),
        "unexpected parse output: {parse_stdout}"
    );

    let check = Command::new(&check_binary)
        .current_dir(&repo_root)
        .args([sample.to_str().expect("utf8"), "exe"])
        .output()
        .expect("run check stage");
    assert_eq!(check.status.code(), Some(0));
    let check_stdout = String::from_utf8_lossy(&check.stdout).replace("\r\n", "\n");
    assert!(
        frontend_report_prefix(check_stdout.trim_end())
            .starts_with(frontend_report_prefix(parse_stdout.trim_end())),
        "check output does not extend parse output\nparse:\n{parse_stdout}\ncheck:\n{check_stdout}"
    );
    assert!(
        check_stdout.contains(" || diagnostics=assignment call result type mismatch"),
        "unexpected check output: {check_stdout}"
    );
}

#[test]
fn build_selfhost_frontend_stage_chain_corpus() {
    let dir = TempDir::new().expect("temp dir");
    let scan_binary = dir.path().join("bootstrap-selfhost-scan");
    let parse_binary = dir.path().join("bootstrap-selfhost-parse");
    let check_binary = dir.path().join("bootstrap-selfhost-check");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");

    build_selfhost_frontend_scan_binary(&repo_root, &scan_binary);
    build_selfhost_frontend_parse_binary(&repo_root, &parse_binary);
    build_selfhost_frontend_binary(&repo_root, &check_binary);

    let cases = load_corpus_manifest(&repo_root, "bootstrap/selfhost/corpus/frontend-stage-chain.txt");

    for case in cases {
        let fixture = &case[0];
        let kind = &case[1];
        let expected_diag = &case[2];
        let (scan_stdout, scan_status) = run_stage_binary(&scan_binary, &repo_root, fixture, kind);
        assert_eq!(scan_status, Some(0), "scan failed for {fixture}");
        assert!(
            scan_stdout.starts_with("tokens="),
            "unexpected scan output for {fixture}: {scan_stdout}"
        );

        let (parse_stdout, parse_status) = run_stage_binary(&parse_binary, &repo_root, fixture, kind);
        assert_eq!(parse_status, Some(0), "parse failed for {fixture}");
        assert!(
            frontend_report_prefix(parse_stdout.trim_end()).starts_with(scan_stdout.trim_end()),
            "parse output does not extend scan output for {fixture}\nscan:\n{scan_stdout}\nparse:\n{parse_stdout}"
        );
        assert!(
            parse_stdout.contains(" || diagnostics=ok"),
            "unexpected parse output for {fixture}: {parse_stdout}"
        );

        let (check_stdout, check_status) = run_stage_binary(&check_binary, &repo_root, fixture, kind);
        assert_eq!(check_status, Some(0), "check failed for {fixture}");
        assert!(
            frontend_report_prefix(check_stdout.trim_end())
                .starts_with(frontend_report_prefix(parse_stdout.trim_end())),
            "check output does not extend parse output for {fixture}\nparse:\n{parse_stdout}\ncheck:\n{check_stdout}"
        );
        let diag = format!(" || diagnostics={expected_diag}");
        assert!(
            check_stdout.contains(&diag),
            "unexpected check diagnostics for {fixture}: {check_stdout}"
        );
    }
}

#[test]
fn build_and_run_bootstrap_seed_implicit_main_top_level() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_implicit_main_top_level.tg");
    let expected_path =
        repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_implicit_main_top_level.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn dump_bootstrap_seed_implicit_main_top_level() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_implicit_main_top_level.tg");
    let expected_path =
        repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_dump_implicit_main_top_level.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .args([
            sample.to_str().expect("utf8"),
            "exe",
            "dump-desugared",
        ])
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_infer_return_i32() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_infer_return_i32.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_infer_return_i32.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn dump_bootstrap_seed_infer_return_i32() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_infer_return_i32.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_dump_infer_return_i32.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .args([
            sample.to_str().expect("utf8"),
            "exe",
            "dump-desugared",
        ])
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn dump_bootstrap_seed_infer_return_identifier() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_infer_return_identifier.tg");
    let expected_path =
        repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_dump_infer_return_identifier.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .args([
            sample.to_str().expect("utf8"),
            "exe",
            "dump-desugared",
        ])
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn dump_bootstrap_seed_infer_return_call() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_infer_return_call.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_dump_infer_return_call.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .args([
            sample.to_str().expect("utf8"),
            "exe",
            "dump-desugared",
        ])
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn dump_bootstrap_seed_reports_match_goldens() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let cases = [
        (
            "bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_library_import_only.tg",
            "library",
            "bootstrap/selfhost/corpus/goldens/bootstrap/expected_report_library_import_only.txt",
        ),
        (
            "bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_implicit_main_top_level.tg",
            "exe",
            "bootstrap/selfhost/corpus/goldens/bootstrap/expected_report_implicit_main_top_level.txt",
        ),
    ];

    for (fixture, kind, expected) in cases {
        let sample = repo_root.join(fixture);
        let expected_path = repo_root.join(expected);

        let output = Command::new(&binary)
            .current_dir(&repo_root)
            .args([
                sample.to_str().expect("utf8"),
                kind,
                "dump-report",
            ])
            .output()
            .unwrap_or_else(|error| panic!("run dump-report for {fixture}: {error}"));
        assert_eq!(output.status.code(), Some(0), "dump-report failed for {fixture}");

        let expected = fs::read_to_string(expected_path)
            .expect("read expected")
            .replace("\r\n", "\n");
        let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
        assert_eq!(
            actual.trim_end(),
            expected.trim_end(),
            "unexpected report dump for {fixture}"
        );
    }
}

#[test]
fn build_and_run_bootstrap_seed_infer_return_unknown_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_infer_return_unknown.tg");
    let expected_path =
        repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_infer_return_unknown.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert!(
        actual.contains(&format!("diagnostics={}", expected.trim_end())),
        "actual output did not contain expected diagnostic.\nactual:\n{}",
        actual
    );
}

#[test]
fn build_and_run_bootstrap_seed_core_exe_rejects_implicit_main() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_implicit_main_top_level.tg");
    let expected_path =
        repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_core_exe_implicit_main_top_level.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .args([sample.to_str().expect("utf8"), "core-exe"])
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_core_exe_rejects_inferred_return_type() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_infer_return_i32.tg");
    let expected_path =
        repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_core_exe_infer_return_i32.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .args([sample.to_str().expect("utf8"), "core-exe"])
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_core_library_rejects_inferred_return_type() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_core_library_infer_return.tg");
    let expected_path =
        repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_core_library_infer_return.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .args([sample.to_str().expect("utf8"), "core-library"])
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_library_import_only() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_library_import_only.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_library_import_only.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .arg("library")
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_library_top_level_statement_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_library_top_level_statement.tg");
    let expected_path =
        repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_library_top_level_statement.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .arg("library")
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert!(
        actual.contains(&format!("diagnostics={}", expected.trim_end())),
        "actual output did not contain expected diagnostic.\nactual:\n{}",
        actual
    );
}

#[test]
fn build_and_run_bootstrap_seed_missing_return_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_missing_return.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_missing_return.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_missing_func_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_missing_func.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_missing_func.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_malformed_let_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_malformed_let.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_malformed_let.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_module_resolver() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let fixture_root = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/modules");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/modules/modules.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&fixture_root)
        .arg("main.tg")
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_missing_module_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let fixture_root = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/modules_missing");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/modules/modules_missing.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&fixture_root)
        .arg("main.tg")
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_unknown_return_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_unknown_return.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_unknown_return.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_duplicate_local_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_duplicate_local.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_duplicate_local.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_duplicate_func_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_duplicate_func.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_duplicate_func.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_missing_main_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_missing_main.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_missing_main.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_unknown_callee_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_unknown_callee.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_unknown_callee.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_duplicate_import_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let fixture_root = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/modules_duplicate_import");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/modules/modules_duplicate_import.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&fixture_root)
        .arg("main.tg")
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_duplicate_imported_symbol_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let fixture_root = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/modules_duplicate_import_symbol");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/modules/modules_duplicate_import_symbol.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&fixture_root)
        .arg("main.tg")
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_local_shadows_import_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let fixture_root = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/modules_import_shadow_local");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/modules/modules_import_shadow_local.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&fixture_root)
        .arg("main.tg")
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_second_local_shadows_import_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let fixture_root = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/modules_import_shadow_second_local");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/modules/modules_import_shadow_second_local.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&fixture_root)
        .arg("main.tg")
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_func_shadows_import_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let fixture_root = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/modules_import_shadow_func");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/modules/modules_import_shadow_func.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&fixture_root)
        .arg("main.tg")
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_call_arity_mismatch_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_call_arity_mismatch.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_call_arity_mismatch.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_return_bool_to_i32_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_return_bool_to_i32.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_return_bool_to_i32.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_return_int_to_bool_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_return_int_to_bool.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_return_int_to_bool.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_local_bool_to_i32_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_local_bool_to_i32.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_local_bool_to_i32.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_local_int_to_bool_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_local_int_to_bool.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_local_int_to_bool.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_duplicate_import_alias_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let fixture_root = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/modules_duplicate_import_alias");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/modules/modules_duplicate_import_alias.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&fixture_root)
        .arg("main.tg")
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_local_shadows_import_alias_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let fixture_root = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/modules_import_alias_shadow_local");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/modules/modules_import_alias_shadow_local.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&fixture_root)
        .arg("main.tg")
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_func_shadows_import_alias_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let fixture_root = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/modules_import_alias_shadow_func");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/modules/modules_import_alias_shadow_func.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&fixture_root)
        .arg("main.tg")
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_unknown_import_alias_usage_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_unknown_import_alias_usage.tg");
    let expected_path =
        repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_unknown_import_alias_usage.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_return_call_bool_to_i32_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_return_call_bool_to_i32.tg");
    let expected_path =
        repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_return_call_bool_to_i32.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_local_call_bool_to_i32_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_local_call_bool_to_i32.tg");
    let expected_path =
        repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_local_call_bool_to_i32.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_assignment_to_unknown_local_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_assignment_to_unknown_local.tg");
    let expected_path =
        repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_assignment_to_unknown_local.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_assignment_bool_to_i32_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_assignment_bool_to_i32.tg");
    let expected_path =
        repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_assignment_bool_to_i32.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_assignment_call_bool_to_i32_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_assignment_call_bool_to_i32.tg");
    let expected_path =
        repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_assignment_call_bool_to_i32.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert!(
        actual.contains(&format!("diagnostics={}", expected.trim_end())),
        "actual output did not contain expected diagnostic.\nactual:\n{}",
        actual
    );
}

#[test]
fn build_and_run_bootstrap_seed_condition_int_literal_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_condition_int_literal.tg");
    let expected_path =
        repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_condition_int_literal.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_condition_local_i32_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_condition_local_i32.tg");
    let expected_path =
        repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_condition_local_i32.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert!(
        actual.contains(&format!("diagnostics={}", expected.trim_end())),
        "actual output did not contain expected diagnostic.\nactual:\n{}",
        actual
    );
}

#[test]
fn build_and_run_bootstrap_seed_condition_call_i32_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let sample = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/sample_condition_call_i32.tg");
    let expected_path =
        repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/expected_condition_call_i32.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&repo_root)
        .arg(sample.to_str().expect("utf8"))
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_dotted_module_resolver() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let fixture_root = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/modules_dotted");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/modules/modules_dotted.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&fixture_root)
        .arg("main.tg")
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_dotted_missing_module_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let fixture_root = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/modules_dotted_missing");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/modules/modules_dotted_missing.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&fixture_root)
        .arg("main.tg")
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn build_and_run_bootstrap_seed_unknown_imported_symbol_reports_diagnostic() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("bootstrap-seed");
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let fixture_root = repo_root.join("bootstrap/selfhost/corpus/fixtures/bootstrap_seed/modules_dotted_symbol_missing");
    let source = repo_root.join("bootstrap/selfhost/frontend/main.tg");
    let expected_path = repo_root.join("bootstrap/selfhost/corpus/goldens/bootstrap/modules/modules_dotted_symbol_missing.txt");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary)
        .current_dir(&fixture_root)
        .arg("main.tg")
        .output()
        .expect("run built binary");
    assert_eq!(output.status.code(), Some(0));

    let expected = fs::read_to_string(expected_path)
        .expect("read expected")
        .replace("\r\n", "\n");
    let actual = String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n");
    assert_eq!(actual.trim_end(), expected.trim_end());
}

#[test]
fn run_std_io_module_uses_windows_safe_temp_binary_path() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("main.tg");
    fs::write(
        &source,
        "import std.io as io\nimport std.string as string\n\nfunc fib_iter(n: i32) -> i32:\n  if (n <= 1):\n    return n\n  let index: i32 = 2\n  let prev2: i32 = 0\n  let prev1: i32 = 1\n  while (index <= n):\n    let next: i32 = prev1 + prev2\n    prev2 = prev1\n    prev1 = next\n    index = index + 1\n  return prev1\n\nfunc main() -> i32:\n  io.println(string.from_int(fib_iter(10)))\n  return 0\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["run", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc run");
    assert!(
        output.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(
        String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n"),
        "55\n"
    );
}

#[test]
fn build_and_run_builtin_scope_fixture() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("builtins");
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/builtins_tests.tg");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            fixture.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary).output().expect("run built binary");
    assert_eq!(output.status.code(), Some(0));
    assert_eq!(String::from_utf8_lossy(&output.stdout), "hello world\n");
    assert_eq!(String::from_utf8_lossy(&output.stderr), "err line\n");
}

#[test]
fn build_and_run_return_inference_fixture() {
    let dir = TempDir::new().expect("temp dir");
    let binary = dir.path().join("return-infer");
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/return_infer_tests.tg");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            fixture.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary).output().expect("run built binary");
    assert_eq!(output.status.code(), Some(0));
    assert!(String::from_utf8_lossy(&output.stdout).contains("return inference ok"));
}

#[test]
fn build_and_run_builtin_prints_primitive_values() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("print_values.tg");
    let binary = dir.path().join("print-values");
    fs::write(
        &source,
        "func fib_iter(n: i32) -> i32:\n  if (n <= 1):\n    return n\n  let index: i32 = 2\n  let prev2: i32 = 0\n  let prev1: i32 = 1\n  while (index <= n):\n    let next: i32 = prev1 + prev2\n    prev2 = prev1\n    prev1 = next\n    index = index + 1\n  return prev1\n\nfunc main() -> i32:\n  println(fib_iter(10))\n  println(true)\n  println(1.5)\n  return 0\n",
    )
    .expect("write source");

    let build = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args([
            "build",
            source.to_str().expect("utf8"),
            "-o",
            binary.to_str().expect("utf8"),
        ])
        .output()
        .expect("run thagc build");
    assert!(
        build.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&build.stdout),
        String::from_utf8_lossy(&build.stderr)
    );

    let output = Command::new(&binary).output().expect("run built binary");
    assert_eq!(output.status.code(), Some(0));
    assert_eq!(
        String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n"),
        "55\ntrue\n1.5\n"
    );
}

#[test]
fn check_reports_inconsistent_inferred_return_types() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("bad_return.tg");
    fs::write(
        &source,
        "func bad(x: i32):\n  if (x < 0):\n    return -x\n  return \"oops\"\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("return type mismatch"), "{stderr}");
}

#[test]
fn check_reports_top_level_let_without_lowering_escape() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("top_level_let.tg");
    fs::write(
        &source,
        "let value: i32 = 1\n\nfunc main() -> i32:\n  return value\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("unsupported language feature"), "{stderr}");
    assert!(stderr.contains("top-level let declarations"), "{stderr}");
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
}

#[test]
fn check_reports_invalid_const_initializer_without_lowering_escape() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("bad_const.tg");
    fs::write(
        &source,
        "func helper() -> i32:\n  return 1\n\nconst BAD: i32 = helper()\n\nfunc main() -> i32:\n  return BAD\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("invalid const initializer"), "{stderr}");
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
}

#[test]
fn check_reports_non_struct_field_access_without_lowering_escape() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("bad_field.tg");
    fs::write(
        &source,
        "func main() -> i32:\n  let value: i32 = 1\n  return value.x\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("value has no fields"), "{stderr}");
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
}

#[test]
fn check_reports_non_indexable_values_without_lowering_escape() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("bad_index.tg");
    fs::write(
        &source,
        "func main() -> i32:\n  let value: i32 = 1\n  return value[0]\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("value is not indexable"), "{stderr}");
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
}

#[test]
fn check_reports_non_callable_values_without_lowering_escape() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("bad_call.tg");
    fs::write(
        &source,
        "func main() -> i32:\n  let value: i32 = 1\n  return value()\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("value is not callable"), "{stderr}");
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
}

#[test]
fn check_reports_generic_structs_as_unsupported_without_lowering_escape() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("generic_struct.tg");
    fs::write(
        &source,
        "struct Box<T>:\n  value: T\n\nfunc main() -> i32:\n  return 0\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("unsupported language feature"), "{stderr}");
    assert!(stderr.contains("generic structs"), "{stderr}");
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
}

#[test]
fn check_reports_generic_impl_blocks_as_unsupported_without_lowering_escape() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("generic_impl.tg");
    fs::write(
        &source,
        "struct Box<T>:\n  value: T\n\nimpl Box<T>:\n  func get(self) -> T:\n    return self.value\n\nfunc main() -> i32:\n  return 0\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("unsupported language feature"), "{stderr}");
    assert!(stderr.contains("generic impl blocks"), "{stderr}");
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
}

#[test]
fn check_reports_generic_functions_as_unsupported_without_lowering_escape() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("generic_func.tg");
    fs::write(
        &source,
        "func id<T>(value: T) -> T:\n  return value\n\nfunc main() -> i32:\n  return 0\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("unsupported language feature"), "{stderr}");
    assert!(stderr.contains("generic functions"), "{stderr}");
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
}

#[test]
fn check_reports_invalid_impl_target_without_lowering_escape() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("invalid_impl_target.tg");
    fs::write(
        &source,
        "impl Missing:\n  func get(self: Missing) -> i32:\n    return 0\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("invalid impl target"), "{stderr}");
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
}

#[test]
fn check_reports_invalid_impl_receiver_without_lowering_escape() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("invalid_impl_receiver.tg");
    fs::write(
        &source,
        "struct Point:\n  x: i32\n\nimpl Point:\n  func bad() -> i32:\n    return 0\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("invalid impl method receiver"), "{stderr}");
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
}

#[test]
fn check_rejects_method_values_outside_calls_without_lowering_escape() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("method_value.tg");
    fs::write(
        &source,
        "struct Point:\n  x: i32\n\nimpl Point:\n  func get_x(self: Point) -> i32:\n    return self.x\n\nfunc main() -> i32:\n  let point = Point(x=1)\n  let method = point.get_x\n  return 0\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("value has no fields") || stderr.contains("method"), "{stderr}");
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
}

#[test]
fn check_reports_unknown_types_without_lowering_escape() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("unknown_type.tg");
    fs::write(
        &source,
        "func main(value: Missing) -> i32:\n  return 0\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("unknown type"), "{stderr}");
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
}

#[test]
fn check_reports_invalid_control_flow_without_lowering_escape() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("invalid_control_flow.tg");
    fs::write(
        &source,
        "func main() -> i32:\n  break\n  return 0\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("invalid control flow"), "{stderr}");
    assert!(stderr.contains("break can only be used inside a loop"), "{stderr}");
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
}

#[test]
fn check_reports_invalid_assignment_targets_without_lowering_escape() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("invalid_assignment_target.tg");
    fs::write(
        &source,
        "const LIMIT: i32 = 1\n\nfunc main() -> i32:\n  LIMIT = 2\n  return 0\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("invalid assignment target"), "{stderr}");
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
}

#[test]
fn check_reports_unknown_identifiers_without_lowering_escape() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("unknown_identifier.tg");
    fs::write(
        &source,
        "func main() -> i32:\n  return missing_value\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("unknown identifier"), "{stderr}");
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
}

#[test]
fn check_reports_unknown_fields_without_lowering_escape() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("unknown_field.tg");
    fs::write(
        &source,
        "struct Point:\n  x: i32\n\nfunc read(point: Point) -> i32:\n  return point.y\n\nfunc main() -> i32:\n  return 0\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("unknown field"), "{stderr}");
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
}

#[test]
fn check_reports_non_bool_conditions_without_lowering_escape() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("non_bool_condition.tg");
    fs::write(
        &source,
        "func main() -> i32:\n  if (1):\n    return 1\n  return 0\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("condition must be bool"), "{stderr}");
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
}

#[test]
fn check_reports_argument_count_mismatches_without_lowering_escape() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("argument_count_mismatch.tg");
    fs::write(
        &source,
        "func add(left: i32, right: i32) -> i32:\n  return left + right\n\nfunc main() -> i32:\n  return add(1)\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("argument count mismatch"), "{stderr}");
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
}

#[test]
fn check_reports_continue_outside_loop_without_lowering_escape() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("continue_outside_loop.tg");
    fs::write(&source, "func main() -> i32:\n  continue\n  return 0\n").expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("invalid control flow"), "{stderr}");
    assert!(stderr.contains("continue can only be used inside a loop"), "{stderr}");
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
}

#[test]
fn check_reports_value_return_in_intent_body_without_lowering_escape() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("value_return_in_intent.tg");
    fs::write(
        &source,
        "intent Opt:\n  body:\n    return 42\n\nfunc main() -> i32:\n  return 0\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("return with a value is only allowed inside functions"), "{stderr}");
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
}

#[test]
fn check_reports_value_return_in_flow_stage_without_lowering_escape() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("value_return_in_flow.tg");
    fs::write(
        &source,
        "flow payment:\n  stage acquire:\n    return 42\n\nfunc main() -> i32:\n  return 0\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("return with a value is only allowed inside functions"), "{stderr}");
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
}

#[test]
fn check_reports_method_call_on_primitive_without_lowering_escape() {
    let dir = TempDir::new().expect("temp dir");
    let source = dir.path().join("method_on_primitive.tg");
    fs::write(
        &source,
        "func main() -> i32:\n  let value: i32 = 5\n  let ignored = value.upper()\n  return 0\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("has no fields"), "{stderr}");
    assert!(!stderr.contains("IR lowering failed"), "{stderr}");
}

#[test]
fn build_required_surface_failures_do_not_escape_to_ir_or_codegen() {
    let dir = TempDir::new().expect("temp dir");
    let cases = [
        (
            "top_level_let",
            "let value = 1\n\nfunc main() -> i32:\n  return value\n",
        ),
        (
            "invalid_const_initializer",
            "func helper() -> i32:\n  return 1\n\nconst LIMIT: i32 = helper()\n\nfunc main() -> i32:\n  return LIMIT\n",
        ),
        (
            "non_indexable_value",
            "func main() -> i32:\n  let value: i32 = 1\n  return value[0]\n",
        ),
        (
            "non_callable_value",
            "func main() -> i32:\n  let value: i32 = 1\n  return value()\n",
        ),
        (
            "unknown_identifier",
            "func main() -> i32:\n  return missing_value\n",
        ),
        (
            "unknown_field",
            "struct Point:\n  x: i32\n\nfunc read(point: Point) -> i32:\n  return point.y\n\nfunc main() -> i32:\n  return 0\n",
        ),
        (
            "unknown_type",
            "func main(value: Missing) -> i32:\n  return 0\n",
        ),
        (
            "non_bool_condition",
            "func main() -> i32:\n  if (1):\n    return 1\n  return 0\n",
        ),
        (
            "argument_count_mismatch",
            "func add(left: i32, right: i32) -> i32:\n  return left + right\n\nfunc main() -> i32:\n  return add(1)\n",
        ),
        (
            "invalid_control_flow",
            "func main() -> i32:\n  break\n  return 0\n",
        ),
        (
            "invalid_assignment_target",
            "const LIMIT: i32 = 1\n\nfunc main() -> i32:\n  LIMIT = 2\n  return 0\n",
        ),
        (
            "generic_struct",
            "struct Box<T>:\n  value: T\n\nfunc main() -> i32:\n  return 0\n",
        ),
        (
            "generic_impl",
            "struct Box<T>:\n  value: T\n\nimpl Box<T>:\n  func get(self) -> T:\n    return self.value\n\nfunc main() -> i32:\n  return 0\n",
        ),
        (
            "generic_function",
            "func id<T>(value: T) -> T:\n  return value\n\nfunc main() -> i32:\n  return 0\n",
        ),
        (
            "invalid_impl_target",
            "impl Missing:\n  func get(self: Missing) -> i32:\n    return 0\n",
        ),
        (
            "invalid_impl_receiver",
            "struct Point:\n  x: i32\n\nimpl Point:\n  func bad() -> i32:\n    return 0\n",
        ),
        (
            "method_value_outside_call",
            "struct Point:\n  x: i32\n\nimpl Point:\n  func get_x(self: Point) -> i32:\n    return self.x\n\nfunc main() -> i32:\n  let point = Point(x=1)\n  let method = point.get_x\n  return 0\n",
        ),
        (
            "continue_outside_loop",
            "func main() -> i32:\n  continue\n  return 0\n",
        ),
        (
            "value_return_in_intent",
            "intent Opt:\n  body:\n    return 42\n\nfunc main() -> i32:\n  return 0\n",
        ),
        (
            "value_return_in_flow_stage",
            "flow payment:\n  stage acquire:\n    return 42\n\nfunc main() -> i32:\n  return 0\n",
        ),
        (
            "method_call_on_primitive",
            "func main() -> i32:\n  let value: i32 = 5\n  let ignored = value.upper()\n  return 0\n",
        ),
    ];

    for (index, (name, source_text)) in cases.iter().enumerate() {
        let source = dir.path().join(format!("{index}_{name}.tg"));
        let output = dir.path().join(format!("{index}_{name}"));
        fs::write(&source, source_text).expect("write source");
        assert_build_fails_before_ir_or_codegen(&source, &output);
    }
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
