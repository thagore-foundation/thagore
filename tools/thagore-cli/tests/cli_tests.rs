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
    assert_eq!(value["thagc"], "0.9.6");
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
        "import std.path as path\nimport std.string as string\n\nfunc main() -> i32:\n  let root = path.getcwd()\n  let joined = path.path_join(root, \"path-stdlib-probe.txt\")\n  if (path.is_dir(root) and string.contains(joined, \"path-stdlib-probe.txt\")):\n    return 0\n  return 1\n",
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
        "struct Point:\n  x: i32\n\nimpl Point:\n  func bad(self: i32) -> i32:\n    return self\n",
    )
    .expect("write source");

    let output = Command::new(env!("CARGO_BIN_EXE_thagc"))
        .args(["check", source.to_str().expect("utf8")])
        .output()
        .expect("run thagc check");
    assert_eq!(output.status.code(), Some(1));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("invalid method receiver"), "{stderr}");
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
