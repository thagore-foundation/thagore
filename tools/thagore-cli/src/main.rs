//! Entrypoint for the Thagore compiler CLI.

mod builtins;
mod cli;
mod error;
mod ice;
mod pipeline;
mod run;
mod session;
mod self_update;
mod timer;
mod targets;

use std::io::{self, Write};
use std::path::Path;
use std::process::{self, Command as ProcessCommand};

use clap::error::ErrorKind as ClapErrorKind;
use clap::{CommandFactory, Parser as ClapParser};
use serde_json::json;
use termcolor::{ColorChoice, StandardStream};

use crate::cli::{Cli, Command};
use crate::error::ErrorReporter;
use crate::ice::with_ice_handler;
use crate::pipeline::build_options_for_run;
use crate::session::{build_file, check_file};
use crate::run::{execute_binary, RunWorkspace};

const SUCCESS_EXIT_CODE: i32 = 0;
const COMPILE_ERROR_EXIT_CODE: i32 = 1;
const USAGE_EXIT_CODE: i32 = 101;
const THAGC_VERSION: &str = "0.9.1";

fn main() {
    process::exit(with_ice_handler(real_main));
}

fn real_main() -> i32 {
    let cli = match Cli::try_parse() {
        Ok(cli) => cli,
        Err(error) => return handle_clap_error(error),
    };

    if cli.version_json {
        return handle_version_json();
    }
    if cli.print_target_list {
        return handle_print_target_list();
    }

    match cli.command {
        Some(Command::Build(args)) => handle_build(&args.entry, &args.options),
        Some(Command::Check(args)) => {
            handle_check(
                &args.file,
                args.json,
                &args.include_dirs,
                &args.defines,
                &args.features,
                args.legacy_flatten,
            )
        }
        Some(Command::Run(args)) => handle_run(&args),
        Some(Command::SelfUpdate(args)) => handle_self_update(&args),
        Some(Command::Version) => handle_version_human(),
        None => USAGE_EXIT_CODE,
    }
}

fn handle_build(path: &Path, options: &cli::BuildOptions) -> i32 {
    match build_file(path, options) {
        Ok(result) => {
            if options.time {
                let _ = result.timings.write(io::stderr());
            }
            SUCCESS_EXIT_CODE
        }
        Err(failure) => {
            if options.json_errors {
                let _ = ErrorReporter::emit_json(io::stdout(), path, &failure.source, &failure.diagnostics);
            } else {
                let mut stderr = StandardStream::stderr(ColorChoice::Auto);
                let _ =
                    ErrorReporter::emit_text(&mut stderr, path, &failure.source, &failure.diagnostics);
            }
            if options.time {
                let _ = failure.timings.write(io::stderr());
            }
            COMPILE_ERROR_EXIT_CODE
        }
    }
}

fn handle_check(
    path: &Path,
    json: bool,
    include_dirs: &[std::path::PathBuf],
    defines: &[String],
    features: &[String],
    legacy_flatten: bool,
) -> i32 {
    match check_file(path, include_dirs, defines, features, legacy_flatten) {
        Ok(_) => {
            if json {
                let _ = writeln!(io::stdout(), "[]");
            }
            SUCCESS_EXIT_CODE
        }
        Err(failure) => {
            if json {
                let _ = ErrorReporter::emit_json(io::stdout(), path, &failure.source, &failure.diagnostics);
            } else {
                let mut stderr = StandardStream::stderr(ColorChoice::Auto);
                let _ = ErrorReporter::emit_text(&mut stderr, path, &failure.source, &failure.diagnostics);
            }
            COMPILE_ERROR_EXIT_CODE
        }
    }
}

fn handle_run(args: &cli::RunArgs) -> i32 {
    let workspace = match RunWorkspace::new() {
        Ok(workspace) => workspace,
        Err(error) => {
            eprintln!("failed to create temporary run directory: {error}");
            return COMPILE_ERROR_EXIT_CODE;
        }
    };

    let mut build_options = build_options_for_run(&args.options);
    build_options.output = Some(workspace.binary_path(&args.entry));

    match build_file(&args.entry, &build_options) {
        Ok(result) => {
            if args.options.time {
                let _ = result.timings.write(io::stderr());
            }
            let Some(binary) = result.artifacts.binary else {
                eprintln!("internal compiler error: run build completed without a binary");
                return ice::ICE_EXIT_CODE;
            };
            match execute_binary(&binary, &args.args) {
                Ok(code) => code,
                Err(error) => {
                    eprintln!("failed to execute {}: {error}", binary.display());
                    COMPILE_ERROR_EXIT_CODE
                }
            }
        }
        Err(failure) => {
            if args.options.json_errors {
                let _ = ErrorReporter::emit_json(
                    io::stdout(),
                    &args.entry,
                    &failure.source,
                    &failure.diagnostics,
                );
            } else {
                let mut stderr = StandardStream::stderr(ColorChoice::Auto);
                let _ = ErrorReporter::emit_text(
                    &mut stderr,
                    &args.entry,
                    &failure.source,
                    &failure.diagnostics,
                );
            }
            if args.options.time {
                let _ = failure.timings.write(io::stderr());
            }
            COMPILE_ERROR_EXIT_CODE
        }
    }
}

fn handle_self_update(args: &cli::SelfUpdateArgs) -> i32 {
    match self_update::run(args) {
        Ok(()) => SUCCESS_EXIT_CODE,
        Err(error) => {
            eprintln!("{error}");
            COMPILE_ERROR_EXIT_CODE
        }
    }
}

fn handle_version_human() -> i32 {
    println!("thagc {THAGC_VERSION}");
    println!("llvm:   {}", detect_llvm_version().unwrap_or_else(|| "unknown".to_string()));
    println!("host:   {}", detect_host_triple().unwrap_or_else(|| "unknown".to_string()));
    println!("commit: {}", detect_commit().unwrap_or_else(|| "unknown".to_string()));
    SUCCESS_EXIT_CODE
}

fn handle_version_json() -> i32 {
    let payload = json!({
        "thagc": THAGC_VERSION,
        "llvm": detect_llvm_version().unwrap_or_else(|| "unknown".to_string()),
        "host": detect_host_triple().unwrap_or_else(|| "unknown".to_string()),
    });
    println!("{payload}");
    SUCCESS_EXIT_CODE
}

fn handle_print_target_list() -> i32 {
    let payload = json!(targets::target_triples());
    println!("{payload}");
    SUCCESS_EXIT_CODE
}

fn handle_clap_error(error: clap::Error) -> i32 {
    let mut command = Cli::command();
    match error.kind() {
        ClapErrorKind::DisplayHelp | ClapErrorKind::DisplayVersion => {
            let _ = error.print();
            SUCCESS_EXIT_CODE
        }
        _ => {
            let _ = error.format(&mut command).print();
            USAGE_EXIT_CODE
        }
    }
}

fn detect_llvm_version() -> Option<String> {
    let output = ProcessCommand::new("llvm-config")
        .arg("--version")
        .output()
        .ok()?;
    if !output.status.success() {
        return None;
    }
    let version = String::from_utf8(output.stdout).ok()?;
    Some(version.trim().to_string())
}

fn detect_host_triple() -> Option<String> {
    let output = ProcessCommand::new("cc").arg("-dumpmachine").output().ok()?;
    if !output.status.success() {
        return None;
    }
    let host = String::from_utf8(output.stdout).ok()?;
    Some(host.trim().to_string())
}

fn detect_commit() -> Option<String> {
    let output = ProcessCommand::new("git")
        .args(["rev-parse", "--short", "HEAD"])
        .output()
        .ok()?;
    if !output.status.success() {
        return None;
    }
    let commit = String::from_utf8(output.stdout).ok()?;
    Some(commit.trim().to_string())
}
