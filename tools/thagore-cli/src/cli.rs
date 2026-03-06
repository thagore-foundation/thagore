//! Command-line interface definitions for the Thagore compiler driver.
//!
//! This module owns all user-facing command parsing through `clap` derive
//! macros. Later pipeline stages consume these typed command structures rather
//! than interacting with raw argument vectors.

use std::ffi::OsString;
use std::path::PathBuf;

use clap::{Args, Parser, Subcommand, ValueEnum};

/// Top-level command-line entrypoint for the Thagore compiler.
#[derive(Debug, Clone, Parser)]
#[command(
    name = "thagore",
    version = "0.1.0",
    about = "Thagore compiler driver",
    long_about = None,
    arg_required_else_help = true
)]
pub struct Cli {
    /// Selected compiler subcommand.
    #[command(subcommand)]
    pub command: Command,
}

/// Supported top-level compiler subcommands.
#[derive(Debug, Clone, Subcommand)]
pub enum Command {
    /// Build a `.tg` source file through the full compiler pipeline.
    Build(BuildArgs),
    /// Type-check a `.tg` source file without generating code.
    Check(CheckArgs),
    /// Build and immediately execute a `.tg` source file.
    Run(RunArgs),
    /// Print the compiler version, LLVM version, host triple, and commit.
    Version,
}

/// Command-line arguments for `thagore build`.
#[derive(Debug, Clone, Args)]
pub struct BuildArgs {
    /// Input Thagore source file.
    pub file: PathBuf,
    /// Requested build configuration.
    #[command(flatten)]
    pub options: BuildOptions,
}

/// Shared build configuration used by `build` and `run`.
#[derive(Debug, Clone, Args)]
pub struct BuildOptions {
    /// Output binary path.
    #[arg(short = 'o', long = "output")]
    pub output: Option<PathBuf>,
    /// Requested optimization level.
    #[arg(long = "opt", value_enum, default_value = "O2")]
    pub opt: OptLevel,
    /// Requested output artifacts.
    #[arg(
        long = "emit",
        value_enum,
        value_delimiter = ',',
        default_value = "bin"
    )]
    pub emit: Vec<EmitKind>,
    /// Emit DWARF debug information.
    #[arg(long = "debug")]
    pub debug: bool,
    /// LLVM target triple. Defaults to the host target when omitted.
    #[arg(long = "target")]
    pub target: Option<String>,
    /// Print per-stage timing after the pipeline completes.
    #[arg(long = "time")]
    pub time: bool,
}

/// Command-line arguments for `thagore check`.
#[derive(Debug, Clone, Args)]
pub struct CheckArgs {
    /// Input Thagore source file.
    pub file: PathBuf,
    /// Emit structured diagnostics as JSON.
    #[arg(long = "json")]
    pub json: bool,
}

/// Command-line arguments for `thagore run`.
#[derive(Debug, Clone, Args)]
#[command(trailing_var_arg = true)]
pub struct RunArgs {
    /// Input Thagore source file.
    pub file: PathBuf,
    /// Requested build configuration.
    #[command(flatten)]
    pub options: RunOptions,
    /// Arguments forwarded to the compiled program after `--`.
    pub args: Vec<OsString>,
}

/// Build configuration for `run`.
#[derive(Debug, Clone, Args)]
pub struct RunOptions {
    /// Requested optimization level.
    #[arg(long = "opt", value_enum, default_value = "O2")]
    pub opt: OptLevel,
    /// Requested output artifacts.
    #[arg(
        long = "emit",
        value_enum,
        value_delimiter = ',',
        default_value = "bin"
    )]
    pub emit: Vec<EmitKind>,
    /// Emit DWARF debug information.
    #[arg(long = "debug")]
    pub debug: bool,
    /// LLVM target triple. Defaults to the host target when omitted.
    #[arg(long = "target")]
    pub target: Option<String>,
    /// Print per-stage timing after the pipeline completes.
    #[arg(long = "time")]
    pub time: bool,
}

/// Supported LLVM optimization levels.
#[derive(Debug, Clone, Copy, PartialEq, Eq, ValueEnum)]
pub enum OptLevel {
    /// Disable optimization.
    #[value(name = "O0")]
    O0,
    /// Enable basic optimization.
    #[value(name = "O1")]
    O1,
    /// Enable the default optimized build profile.
    #[value(name = "O2")]
    O2,
    /// Enable aggressive optimization.
    #[value(name = "O3")]
    O3,
}

impl Default for OptLevel {
    fn default() -> Self {
        Self::O2
    }
}

/// Supported artifact kinds produced by `--emit`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, ValueEnum)]
pub enum EmitKind {
    /// Emit LLVM textual IR.
    #[value(name = "ll")]
    Ll,
    /// Emit LLVM bitcode.
    #[value(name = "bc")]
    Bc,
    /// Emit a native object file.
    #[value(name = "obj")]
    Obj,
    /// Emit a linked native binary.
    #[value(name = "bin")]
    Bin,
}

impl Default for EmitKind {
    fn default() -> Self {
        Self::Bin
    }
}
