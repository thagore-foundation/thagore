//! Benchmark compilation and execution runner.

use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::Instant;

use serde::Serialize;
use tempfile::TempDir;

use crate::stats::{summarize, SummaryStats};

/// Languages supported by the benchmark comparison harness.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, clap::ValueEnum)]
pub enum Language {
    /// Thagore compiler output.
    Thagore,
    /// Go reference implementation.
    Go,
    /// C reference implementation.
    C,
    /// Rust reference implementation.
    Rust,
}

impl Language {
    /// Returns the stable display label for the language.
    #[must_use]
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Thagore => "thagore",
            Self::Go => "go",
            Self::C => "c",
            Self::Rust => "rust",
        }
    }
}

/// Benchmark configuration.
#[derive(Debug, Clone)]
pub struct BenchmarkConfig {
    /// Comparison languages to run alongside Thagore.
    pub compare: Vec<Language>,
    /// Number of runs before trimming min/max.
    pub runs: usize,
    /// Explicit workload subset, or all known workloads when empty.
    pub workloads: Vec<String>,
}

/// Aggregated benchmark report.
#[derive(Debug, Clone, Serialize)]
pub struct BenchmarkReport {
    /// Ran benchmark workloads.
    pub workloads: Vec<WorkloadReport>,
    /// Unsupported requested workloads.
    pub unsupported: Vec<String>,
}

/// Benchmark result for a single workload.
#[derive(Debug, Clone, Serialize)]
pub struct WorkloadReport {
    /// Stable workload name.
    pub workload: String,
    /// Workload note for approximations.
    pub note: Option<String>,
    /// Per-language benchmark data.
    pub languages: Vec<LanguageReport>,
}

/// Per-language benchmark data for one workload.
#[derive(Debug, Clone, Serialize)]
pub struct LanguageReport {
    /// Language label.
    pub language: Language,
    /// Compile-time summary in milliseconds.
    pub compile_ms: SummaryStats,
    /// Runtime summary in milliseconds.
    pub runtime_ms: SummaryStats,
    /// Peak compile-time RSS in kilobytes.
    pub compile_peak_kb: u64,
    /// Peak runtime RSS in kilobytes.
    pub runtime_peak_kb: u64,
    /// Final binary size in bytes.
    pub binary_size_bytes: u64,
}

#[derive(Debug, Clone, Copy)]
struct Workload {
    name: &'static str,
    fixture: &'static str,
    note: Option<&'static str>,
}

const WORKLOADS: &[Workload] = &[
    Workload {
        name: "fibonacci",
        fixture: "tests/fixtures/bench/fibonacci.tg",
        note: None,
    },
    Workload {
        name: "matrix_mul",
        fixture: "tests/fixtures/bench/matrix_mul.tg",
        note: Some("Uses implicit matrix elements derived from loop indices because Thagore does not yet support array literals or heap-backed matrices."),
    },
    Workload {
        name: "string_search",
        fixture: "tests/fixtures/bench/string_search.tg",
        note: Some("Approximates substring search with a pseudo-byte stream state machine because Thagore does not yet support indexed string operations."),
    },
    Workload {
        name: "struct_heavy",
        fixture: "tests/fixtures/bench/struct_heavy.tg",
        note: Some("Approximates four-field struct mutation with parallel scalar lanes because Thagore does not yet support struct literals or heap allocation."),
    },
    Workload {
        name: "binary_tree",
        fixture: "tests/fixtures/bench/binary_tree.tg",
        note: Some("Approximates binary tree traversal over an implicit heap-indexed tree because Thagore does not yet support pointer allocation."),
    },
];

/// Runs the selected benchmark workloads.
pub fn run_benchmarks(config: &BenchmarkConfig) -> Result<BenchmarkReport, String> {
    let root = workspace_root()?;
    ensure_thagore_cli(&root)?;

    let requested = if config.workloads.is_empty() {
        WORKLOADS.iter().map(|workload| workload.name.to_string()).collect()
    } else {
        config.workloads.clone()
    };

    let mut reports = Vec::new();
    let mut unsupported = Vec::new();
    for name in requested {
        let Some(workload) = WORKLOADS.iter().find(|workload| workload.name == name) else {
            unsupported.push(name);
            continue;
        };
        reports.push(run_workload(&root, workload, config)?);
    }

    Ok(BenchmarkReport {
        workloads: reports,
        unsupported,
    })
}

fn run_workload(
    root: &Path,
    workload: &Workload,
    config: &BenchmarkConfig,
) -> Result<WorkloadReport, String> {
    let mut languages = Vec::new();
    languages.push(run_thagore(root, workload, config.runs)?);
    for language in &config.compare {
        languages.push(run_reference(root, workload, *language, config.runs)?);
    }

    Ok(WorkloadReport {
        workload: workload.name.to_string(),
        note: workload.note.map(ToString::to_string),
        languages,
    })
}

fn run_thagore(root: &Path, workload: &Workload, runs: usize) -> Result<LanguageReport, String> {
    let source = root.join(workload.fixture);
    let compiler = root.join("target/debug/thagore");
    let temp = TempDir::new().map_err(|error| error.to_string())?;

    let compile = benchmark_command(runs, |index| {
        let output = temp.path().join(format!("{}_thagore_{index}", workload.name));
        let command = vec![
            compiler.display().to_string(),
            "build".into(),
            source.display().to_string(),
            "-o".into(),
            output.display().to_string(),
        ];
        MeasuredCommand {
            program: command[0].clone(),
            args: command[1..].to_vec(),
            binary: Some(output),
            env: Vec::new(),
            allow_non_zero_exit: false,
        }
    })?;

    let runtime_binary = compile
        .last_binary
        .clone()
        .ok_or_else(|| "missing Thagore runtime binary".to_string())?;
    let runtime = benchmark_existing_binary(runs, &runtime_binary, &[])?;

    Ok(LanguageReport {
        language: Language::Thagore,
        compile_ms: summarize(&compile.durations_ms),
        runtime_ms: summarize(&runtime.durations_ms),
        compile_peak_kb: compile.peak_kb,
        runtime_peak_kb: runtime.peak_kb,
        binary_size_bytes: file_size(&runtime_binary)?,
    })
}

fn run_reference(
    root: &Path,
    workload: &Workload,
    language: Language,
    runs: usize,
) -> Result<LanguageReport, String> {
    let temp = TempDir::new().map_err(|error| error.to_string())?;
    let source_path = write_reference_source(temp.path(), workload.name, language)?;
    let compile = benchmark_command(runs, |index| {
        let binary = temp
            .path()
            .join(format!("{}_{}_{}", workload.name, language.as_str(), index));
        build_reference(language, &source_path, &binary, index)
    })?;
    let runtime_binary = compile
        .last_binary
        .clone()
        .ok_or_else(|| "missing reference runtime binary".to_string())?;
    let runtime = benchmark_existing_binary(runs, &runtime_binary, &[])?;

    let _ = root;
    Ok(LanguageReport {
        language,
        compile_ms: summarize(&compile.durations_ms),
        runtime_ms: summarize(&runtime.durations_ms),
        compile_peak_kb: compile.peak_kb,
        runtime_peak_kb: runtime.peak_kb,
        binary_size_bytes: file_size(&runtime_binary)?,
    })
}

fn benchmark_command<F>(runs: usize, mut command_for_run: F) -> Result<BenchSamples, String>
where
    F: FnMut(usize) -> MeasuredCommand,
{
    let mut durations_ms = Vec::with_capacity(runs);
    let mut peak_kb = 0u64;
    let mut last_binary = None;

    for index in 0..runs {
        let command = command_for_run(index);
        let sample = run_measured_command(&command)?;
        durations_ms.push(sample.duration_ms);
        peak_kb = peak_kb.max(sample.peak_kb);
        last_binary = command.binary.clone();
    }

    Ok(BenchSamples {
        durations_ms,
        peak_kb,
        last_binary,
    })
}

fn benchmark_existing_binary(
    runs: usize,
    binary: &Path,
    args: &[String],
) -> Result<BenchSamples, String> {
    let mut durations_ms = Vec::with_capacity(runs);
    let mut peak_kb = 0u64;

    for _ in 0..runs {
        let sample = run_measured_command(&MeasuredCommand {
            program: binary.display().to_string(),
            args: args.to_vec(),
            binary: Some(binary.to_path_buf()),
            env: Vec::new(),
            allow_non_zero_exit: true,
        })?;
        durations_ms.push(sample.duration_ms);
        peak_kb = peak_kb.max(sample.peak_kb);
    }

    Ok(BenchSamples {
        durations_ms,
        peak_kb,
        last_binary: Some(binary.to_path_buf()),
    })
}

#[derive(Debug, Clone)]
struct BenchSamples {
    durations_ms: Vec<f64>,
    peak_kb: u64,
    last_binary: Option<PathBuf>,
}

#[derive(Debug, Clone)]
struct MeasuredCommand {
    program: String,
    args: Vec<String>,
    binary: Option<PathBuf>,
    env: Vec<(String, String)>,
    allow_non_zero_exit: bool,
}

#[derive(Debug, Clone, Copy)]
struct MeasuredSample {
    duration_ms: f64,
    peak_kb: u64,
}

fn run_measured_command(command: &MeasuredCommand) -> Result<MeasuredSample, String> {
    let start = Instant::now();
    let (status, peak_kb, stderr) = if Path::new("/usr/bin/time").is_file() {
        let output = {
            let mut wrapped = Command::new("/usr/bin/time");
            wrapped.arg("-f").arg("%M");
            apply_wrapped_command(&mut wrapped, command);
            wrapped
                .stdout(Stdio::null())
                .output()
                .map_err(|error| error.to_string())?
        };
        let stderr = String::from_utf8_lossy(&output.stderr).into_owned();
        let peak = stderr
            .lines()
            .last()
            .and_then(|line| line.trim().parse::<u64>().ok())
            .unwrap_or(0);
        (output.status, peak, stderr)
    } else {
        let mut direct = Command::new(&command.program);
        apply_direct_command(&mut direct, command);
        let output = direct
            .stdout(Stdio::null())
            .stderr(Stdio::piped())
            .output()
            .map_err(|error| error.to_string())?;
        (
            output.status,
            0,
            String::from_utf8_lossy(&output.stderr).into_owned(),
        )
    };

    if !status.success() && !command.allow_non_zero_exit {
        return Err(format!(
            "command failed: {} {} (status: {:?}, stderr: {})",
            command.program,
            command.args.join(" "),
            status.code(),
            stderr.trim()
        ));
    }

    Ok(MeasuredSample {
        duration_ms: start.elapsed().as_secs_f64() * 1_000.0,
        peak_kb,
    })
}

fn apply_wrapped_command(process: &mut Command, command: &MeasuredCommand) {
    process.arg(&command.program);
    process.args(&command.args);
    for (key, value) in &command.env {
        process.env(key, value);
    }
}

fn apply_direct_command(process: &mut Command, command: &MeasuredCommand) {
    process.args(&command.args);
    for (key, value) in &command.env {
        process.env(key, value);
    }
}

fn build_reference(language: Language, source: &Path, binary: &Path, run: usize) -> MeasuredCommand {
    match language {
        Language::Go => {
            let cache_dir = env::temp_dir().join(format!("thagore_bench_gocache_{run}"));
            MeasuredCommand {
                program: "go".into(),
                args: vec![
                    "build".into(),
                    "-o".into(),
                    binary.display().to_string(),
                    source.display().to_string(),
                ],
                binary: Some(binary.to_path_buf()),
                env: vec![("GOCACHE".into(), cache_dir.display().to_string())],
                allow_non_zero_exit: false,
            }
        }
        Language::C => MeasuredCommand {
            program: "cc".into(),
            args: vec![
                "-O3".into(),
                "-march=native".into(),
                source.display().to_string(),
                "-o".into(),
                binary.display().to_string(),
            ],
            binary: Some(binary.to_path_buf()),
            env: Vec::new(),
            allow_non_zero_exit: false,
        },
        Language::Rust => MeasuredCommand {
            program: "rustc".into(),
            args: vec![
                "-O".into(),
                "-C".into(),
                "target-cpu=native".into(),
                source.display().to_string(),
                "-o".into(),
                binary.display().to_string(),
            ],
            binary: Some(binary.to_path_buf()),
            env: Vec::new(),
            allow_non_zero_exit: false,
        },
        Language::Thagore => unreachable!("Thagore compilation is handled separately"),
    }
}

fn write_reference_source(dir: &Path, workload: &str, language: Language) -> Result<PathBuf, String> {
    let (extension, source) = match language {
        Language::Go => ("go", go_source(workload)?),
        Language::C => ("c", c_source(workload)?),
        Language::Rust => ("rs", rust_source(workload)?),
        Language::Thagore => return Err("no reference source for Thagore".into()),
    };
    let path = dir.join(format!("{workload}.{extension}"));
    fs::write(&path, source).map_err(|error| error.to_string())?;
    Ok(path)
}

fn workspace_root() -> Result<PathBuf, String> {
    let manifest = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    manifest
        .ancestors()
        .nth(2)
        .map(Path::to_path_buf)
        .ok_or_else(|| "failed to resolve workspace root".to_string())
}

fn ensure_thagore_cli(root: &Path) -> Result<(), String> {
    let binary = root.join("target/debug/thagore");
    if binary.is_file() {
        return Ok(());
    }
    let status = Command::new("cargo")
        .current_dir(root)
        .args(["build", "-p", "thagore-cli"])
        .status()
        .map_err(|error| error.to_string())?;
    if status.success() {
        Ok(())
    } else {
        Err("failed to build thagore-cli".into())
    }
}

fn file_size(path: &Path) -> Result<u64, String> {
    fs::metadata(path)
        .map(|metadata| metadata.len())
        .map_err(|error| error.to_string())
}

fn go_source(workload: &str) -> Result<String, String> {
    Ok(match workload {
        "fibonacci" => r#"package main

import "os"

func fib(n int32) int32 {
    if n < 2 {
        return n
    }
    return fib(n-1) + fib(n-2)
}

func main() {
    os.Exit(int(fib(42) % 251))
}
"#,
        "matrix_mul" => r#"package main

import "os"

func main() {
    var i, j, k, total int32
    for i = 0; i < 256; i++ {
        for j = 0; j < 256; j++ {
            var sum int32 = 0
            for k = 0; k < 256; k++ {
                left := ((i * 17) + (k * 31)) % 97
                right := ((k * 13) + (j * 19)) % 89
                sum += left * right
            }
            total += sum % 251
        }
    }
    os.Exit(int(total % 251))
}
"#,
        "string_search" => r#"package main

import "os"

func byteAt(i int32) int32 {
    return ((i * 131) + 17) % 26
}

func main() {
    var i, count int32
    for i = 0; i < 10000000; i++ {
        a := byteAt(i)
        b := byteAt(i + 1)
        c := byteAt(i + 2)
        d := byteAt(i + 3)
        if a == 19 && b == 7 && c == 0 && d == 6 {
            count++
        }
    }
    os.Exit(int(count % 251))
}
"#,
        "struct_heavy" => r#"package main

import "os"

func main() {
    var i int32
    var x int32 = 1
    var y int32 = 2
    var z int32 = 3
    var w int32 = 4
    var total int32
    for i = 0; i < 2000000; i++ {
        x = x + 3
        y = y + x
        z = z + y
        w = w + z
        total += x + y + z + w
    }
    os.Exit(int(total % 251))
}
"#,
        "binary_tree" => r#"package main

import "os"

func walk(depth int32, index int32) int32 {
    if depth == 0 {
        return index % 97
    }
    return walk(depth-1, index*2) + walk(depth-1, index*2+1)
}

func main() {
    os.Exit(int(walk(20, 1) % 251))
}
"#,
        other => return Err(format!("no Go source for workload {other}")),
    }
    .into())
}

fn c_source(workload: &str) -> Result<String, String> {
    Ok(match workload {
        "fibonacci" => r#"#include <stdint.h>
#include <stdlib.h>

static int32_t fib(int32_t n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

int main(void) {
    return fib(42) % 251;
}
"#,
        "matrix_mul" => r#"#include <stdint.h>

int main(void) {
    int32_t total = 0;
    for (int32_t i = 0; i < 256; ++i) {
        for (int32_t j = 0; j < 256; ++j) {
            int32_t sum = 0;
            for (int32_t k = 0; k < 256; ++k) {
                int32_t left = ((i * 17) + (k * 31)) % 97;
                int32_t right = ((k * 13) + (j * 19)) % 89;
                sum += left * right;
            }
            total += sum % 251;
        }
    }
    return total % 251;
}
"#,
        "string_search" => r#"#include <stdint.h>

static int32_t byte_at(int32_t i) {
    return ((i * 131) + 17) % 26;
}

int main(void) {
    int32_t count = 0;
    for (int32_t i = 0; i < 10000000; ++i) {
        int32_t a = byte_at(i);
        int32_t b = byte_at(i + 1);
        int32_t c = byte_at(i + 2);
        int32_t d = byte_at(i + 3);
        if (a == 19 && b == 7 && c == 0 && d == 6) {
            ++count;
        }
    }
    return count % 251;
}
"#,
        "struct_heavy" => r#"#include <stdint.h>

int main(void) {
    int32_t x = 1, y = 2, z = 3, w = 4, total = 0;
    for (int32_t i = 0; i < 2000000; ++i) {
        x = x + 3;
        y = y + x;
        z = z + y;
        w = w + z;
        total += x + y + z + w;
    }
    return total % 251;
}
"#,
        "binary_tree" => r#"#include <stdint.h>

static int32_t walk(int32_t depth, int32_t index) {
    if (depth == 0) {
        return index % 97;
    }
    return walk(depth - 1, index * 2) + walk(depth - 1, index * 2 + 1);
}

int main(void) {
    return walk(20, 1) % 251;
}
"#,
        other => return Err(format!("no C source for workload {other}")),
    }
    .into())
}

fn rust_source(workload: &str) -> Result<String, String> {
    Ok(match workload {
        "fibonacci" => r#"fn fib(n: i32) -> i32 {
    if n < 2 { n } else { fib(n - 1) + fib(n - 2) }
}

fn main() {
    std::process::exit(fib(42) % 251);
}
"#,
        "matrix_mul" => r#"fn main() {
    let mut total = 0i32;
    for i in 0..256i32 {
        for j in 0..256i32 {
            let mut sum = 0i32;
            for k in 0..256i32 {
                let left = ((i * 17) + (k * 31)) % 97;
                let right = ((k * 13) + (j * 19)) % 89;
                sum += left * right;
            }
            total += sum % 251;
        }
    }
    std::process::exit(total % 251);
}
"#,
        "string_search" => r#"fn byte_at(i: i32) -> i32 {
    ((i * 131) + 17) % 26
}

fn main() {
    let mut count = 0i32;
    for i in 0..10_000_000i32 {
        let a = byte_at(i);
        let b = byte_at(i + 1);
        let c = byte_at(i + 2);
        let d = byte_at(i + 3);
        if a == 19 && b == 7 && c == 0 && d == 6 {
            count += 1;
        }
    }
    std::process::exit(count % 251);
}
"#,
        "struct_heavy" => r#"fn main() {
    let mut x = 1i32;
    let mut y = 2i32;
    let mut z = 3i32;
    let mut w = 4i32;
    let mut total = 0i32;
    for _ in 0..2_000_000i32 {
        x += 3;
        y += x;
        z += y;
        w += z;
        total += x + y + z + w;
    }
    std::process::exit(total % 251);
}
"#,
        "binary_tree" => r#"fn walk(depth: i32, index: i32) -> i32 {
    if depth == 0 {
        index % 97
    } else {
        walk(depth - 1, index * 2) + walk(depth - 1, index * 2 + 1)
    }
}

fn main() {
    std::process::exit(walk(20, 1) % 251);
}
"#,
        other => return Err(format!("no Rust source for workload {other}")),
    }
    .into())
}
