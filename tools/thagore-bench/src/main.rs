//! CLI entrypoint for the Thagore benchmark runner.

mod report;
mod runner;
mod stats;

use clap::{Parser, Subcommand};

use crate::report::render_text;
use crate::runner::{run_benchmarks, BenchmarkConfig, Language};

/// Thagore benchmark CLI.
#[derive(Debug, Parser)]
#[command(name = "thagore-bench", about = "Benchmark the Thagore compiler and runtime")]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

/// Benchmark subcommands.
#[derive(Debug, Subcommand)]
enum Command {
    /// Run the configured benchmark suite.
    Run(RunArgs),
}

/// Arguments for `thagore-bench run`.
#[derive(Debug, clap::Args)]
struct RunArgs {
    /// Comparison languages to benchmark against.
    #[arg(long = "compare", value_enum, value_delimiter = ',', default_value = "go")]
    compare: Vec<Language>,
    /// Number of runs per compile/runtime measurement.
    #[arg(long = "runs", default_value_t = 21)]
    runs: usize,
    /// Restrict execution to a comma-separated subset of workloads.
    #[arg(long = "workloads", value_delimiter = ',')]
    workloads: Vec<String>,
    /// Emit the report as JSON.
    #[arg(long = "json")]
    json: bool,
}

fn main() {
    let cli = Cli::parse();
    let exit_code = match cli.command {
        Command::Run(args) => run(args),
    };
    std::process::exit(exit_code);
}

fn run(args: RunArgs) -> i32 {
    let config = BenchmarkConfig {
        compare: args.compare.clone(),
        runs: args.runs.max(3),
        workloads: args.workloads,
    };
    match run_benchmarks(&config) {
        Ok(report) => {
            if args.json {
                match serde_json::to_string_pretty(&report) {
                    Ok(json) => println!("{json}"),
                    Err(error) => {
                        eprintln!("failed to serialize benchmark report: {error}");
                        return 1;
                    }
                }
            } else {
                let primary = args.compare.first().copied().unwrap_or(Language::Go);
                print!("{}", render_text(&report, primary));
            }
            0
        }
        Err(error) => {
            eprintln!("{error}");
            1
        }
    }
}
