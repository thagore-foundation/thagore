//! CLI entrypoint for `thagore-fmt`.

mod config;
mod diff;
mod formatter;
mod rules;

use std::fs;
use std::io::{self, Read, Write};
use std::path::{Path, PathBuf};

use clap::Parser;
use walkdir::WalkDir;

use crate::config::{find_project_root, FmtConfig, StylePreset};
use crate::diff::{needs_formatting, unified_diff};
use crate::formatter::format_source;

#[derive(Debug, Parser)]
#[command(name = "thagore-fmt", version = "0.1.0")]
struct Cli {
    /// Check if files are formatted without writing changes.
    #[arg(long)]
    check: bool,
    /// Print unified diff for formatting changes without writing.
    #[arg(long)]
    diff: bool,
    /// Read source from stdin and write formatted output to stdout.
    #[arg(long)]
    stdin: bool,
    /// Write formatted output to stdout instead of rewriting files.
    #[arg(long)]
    stdout: bool,
    /// Format all `.tg` files in the current project.
    #[arg(long)]
    project: bool,
    /// Use a formatter config file from an explicit path.
    #[arg(long)]
    config: Option<PathBuf>,
    /// Use a built-in style preset.
    #[arg(long, value_enum, default_value_t = StylePreset::Strict)]
    style: StylePreset,
    /// Explicitly write changes back to disk.
    #[arg(long)]
    write: bool,
    /// Suppress summary output.
    #[arg(long)]
    quiet: bool,
    /// Print every file being formatted.
    #[arg(long)]
    verbose: bool,
    /// Files to format.
    files: Vec<PathBuf>,
}

fn main() {
    let cli = Cli::parse();
    std::process::exit(run(cli));
}

fn run(cli: Cli) -> i32 {
    let write_mode = !cli.check && !cli.diff && !cli.stdin && !cli.stdout || cli.write;

    if cli.stdin {
        return run_stdin(&cli);
    }

    let files = match discover_files(&cli) {
        Ok(files) => files,
        Err(message) => {
            eprintln!("error: {message}");
            return 1;
        }
    };

    let mut changed = 0_usize;
    let mut had_error = false;
    for file in &files {
        if cli.verbose {
            eprintln!("formatting {}", file.display());
        }
        let source = match fs::read_to_string(file) {
            Ok(source) => source,
            Err(error) => {
                eprintln!("error: failed to read {}: {error}", file.display());
                had_error = true;
                continue;
            }
        };
        let config = match FmtConfig::load(cli.config.as_deref(), Some(cli.style), Some(file)) {
            Ok(config) => config,
            Err(message) => {
                eprintln!("error: {message}");
                had_error = true;
                continue;
            }
        };
        let result = format_source(file, &source, &config);
        if !result.parse_errors.is_empty() {
            eprintln!("error: failed to parse {}", file.display());
            had_error = true;
            continue;
        }
        let is_changed = needs_formatting(&source, &result.formatted);
        if is_changed {
            changed += 1;
        }
        if cli.diff && is_changed {
            print!("{}", unified_diff(file, &source, &result.formatted));
        }
        if cli.stdout {
            print!("{}", result.formatted);
            continue;
        }
        if cli.check && is_changed {
            eprintln!("error: {} is not formatted", file.display());
            continue;
        }
        if write_mode && is_changed {
            if let Err(error) = fs::write(file, result.formatted) {
                eprintln!("error: failed to write {}: {error}", file.display());
                had_error = true;
            }
        }
    }

    if cli.check && changed > 0 {
        eprintln!(
            "{} files need formatting. Run `thagore-fmt` to fix.",
            changed
        );
        return 1;
    }
    if had_error {
        return 1;
    }
    if !cli.quiet && !cli.stdout && !cli.stdin {
        eprintln!("formatted {} files ({} changed)", files.len(), changed);
    }
    0
}

fn run_stdin(cli: &Cli) -> i32 {
    let mut source = String::new();
    if let Err(error) = io::stdin().read_to_string(&mut source) {
        eprintln!("error: failed to read stdin: {error}");
        return 1;
    }
    let cwd = std::env::current_dir().unwrap_or_else(|_| PathBuf::from("."));
    let config = match FmtConfig::load(cli.config.as_deref(), Some(cli.style), Some(&cwd)) {
        Ok(config) => config,
        Err(message) => {
            eprintln!("error: {message}");
            return 1;
        }
    };
    let path = cwd.join("stdin.tg");
    let result = format_source(&path, &source, &config);
    if !result.parse_errors.is_empty() {
        eprintln!("error: failed to parse stdin");
        return 1;
    }
    if let Err(error) = io::stdout().write_all(result.formatted.as_bytes()) {
        eprintln!("error: failed to write stdout: {error}");
        return 1;
    }
    0
}

fn discover_files(cli: &Cli) -> Result<Vec<PathBuf>, String> {
    if !cli.files.is_empty() {
        return Ok(cli.files.clone());
    }

    let base = if let Some(config_path) = &cli.config {
        config_path
            .parent()
            .map(Path::to_path_buf)
            .ok_or_else(|| format!("invalid config path {}", config_path.display()))?
    } else {
        std::env::current_dir().map_err(|error| error.to_string())?
    };

    if cli.project {
        let project_root = find_project_root(&base)
            .or_else(|| find_project_root(&std::env::current_dir().ok()?))
            .ok_or_else(|| "could not find project root containing drago.toml".to_string())?;
        return Ok(collect_project_files(&project_root));
    }

    let src_dir = base.join("src");
    if src_dir.is_dir() {
        Ok(collect_tg_files(&src_dir))
    } else {
        Ok(Vec::new())
    }
}

fn collect_project_files(root: &Path) -> Vec<PathBuf> {
    collect_tg_files(&root.join("src"))
}

fn collect_tg_files(root: &Path) -> Vec<PathBuf> {
    if !root.exists() {
        return Vec::new();
    }
    let mut files = WalkDir::new(root)
        .into_iter()
        .filter_map(Result::ok)
        .filter(|entry| entry.file_type().is_file())
        .filter(|entry| entry.path().extension().and_then(|ext| ext.to_str()) == Some("tg"))
        .map(|entry| entry.path().to_path_buf())
        .collect::<Vec<_>>();
    files.sort();
    files
}
