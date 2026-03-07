use std::fs;
use std::path::Path;

use tempfile::tempdir;

#[path = "../src/config.rs"]
mod config;
#[path = "../src/diff.rs"]
mod diff;
#[path = "../src/formatter.rs"]
mod formatter;
#[path = "../src/rules.rs"]
mod rules;

fn strict() -> config::FmtConfig {
    config::FmtConfig::strict()
}

fn relaxed() -> config::FmtConfig {
    config::FmtConfig::relaxed()
}

fn assert_idempotent(source: &str, cfg: &config::FmtConfig) {
    let path = Path::new("sample.tg");
    let once = formatter::format_source(path, source, cfg).formatted;
    let twice = formatter::format_source(path, &once, cfg).formatted;
    assert_eq!(once, twice, "formatter is not idempotent for:\n{source}");
}

#[test]
fn strict_import_sorting_is_idempotent() {
    let source = "import   src.utils as utils\nfrom math import sqrt,abs,min\nimport http\n";
    let formatted = formatter::format_source(Path::new("main.tg"), source, &strict()).formatted;
    assert!(formatted.contains("import http"));
    assert!(formatted.contains("from math import abs, min, sqrt"));
    assert_idempotent(source, &strict());
}

#[test]
fn relaxed_preserves_import_order() {
    let source = "import zeta\nimport alpha\n";
    let formatted = formatter::format_source(Path::new("main.tg"), source, &relaxed()).formatted;
    let lines = formatted.lines().collect::<Vec<_>>();
    assert_eq!(lines[0], "import zeta");
    assert_eq!(lines[1], "import alpha");
    assert_idempotent(source, &relaxed());
}

#[test]
fn function_spacing_and_blank_lines_are_normalized() {
    let source = "const X: i32=1\nfunc   add ( a:i32,b : i32 )->i32:\n  return a+b\n";
    let formatted = formatter::format_source(Path::new("main.tg"), source, &strict()).formatted;
    assert!(formatted.contains("const X: i32 = 1"));
    assert!(formatted.contains("func add(a: i32, b: i32) -> i32:"));
    assert!(formatted.contains("return a + b"));
    assert!(formatted.contains("\n\nfunc add"));
    assert_idempotent(source, &strict());
}

#[test]
fn comments_are_preserved() {
    let source = "#head\nfunc main():\n  let x=1 #inline\n  # keep me\n  return x\n";
    let formatted = formatter::format_source(Path::new("main.tg"), source, &strict()).formatted;
    assert!(formatted.contains("# head"));
    assert!(formatted.contains("# inline"));
    assert!(formatted.contains("# keep me"));
    assert_idempotent(source, &strict());
}

#[test]
fn unified_diff_mentions_formatted_header() {
    let original = "func add ( a:i32,b:i32 )->i32:\n  return a+b\n";
    let formatted = formatter::format_source(Path::new("main.tg"), original, &strict()).formatted;
    assert!(diff::needs_formatting(original, &formatted));
    let diff_text = diff::unified_diff(Path::new("main.tg"), original, &formatted);
    assert!(diff_text.contains("--- main.tg"));
    assert!(diff_text.contains("+++ main.tg (formatted)"));
}

#[test]
fn config_file_overrides_defaults() {
    let dir = tempdir().unwrap();
    let config_path = dir.path().join(".thagore-fmt.toml");
    fs::write(
        &config_path,
        "style = \"relaxed\"\nmax_line_length = 80\nsort_imports = false\n",
    )
    .unwrap();
    let loaded = config::FmtConfig::load(Some(&config_path), None, Some(dir.path())).unwrap();
    assert_eq!(loaded.style, config::StylePreset::Relaxed);
    assert_eq!(loaded.max_line_length, 80);
    assert!(!loaded.sort_imports);
}

#[test]
fn project_root_lookup_finds_drago_manifest() {
    let root = config::find_project_root(Path::new("/media/lehungquangminh/QM_SSD/drago/src/main.tg"))
        .expect("project root");
    assert!(root.join("drago.toml").exists(), "expected drago.toml at project root");
}
