use std::fs;
use std::path::{Path, PathBuf};

use tempfile::tempdir;
use tower_lsp::lsp_types::{Position, Url};

#[path = "../src/analysis.rs"]
mod analysis;
#[path = "../src/completion.rs"]
mod completion;
#[path = "../src/diagnostics.rs"]
mod diagnostics;
#[path = "../src/goto.rs"]
mod goto;
#[path = "../src/hover.rs"]
mod hover;
#[path = "../src/symbols.rs"]
mod symbols;

fn repo_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .ancestors()
        .nth(2)
        .unwrap()
        .to_path_buf()
}

#[test]
fn diagnostics_capture_type_errors() {
    let root = repo_root();
    let host = analysis::AnalysisHost::with_paths(root.clone(), root.join("stdlib"), false);
    let file = root.join("tests/fixtures/errors/type_mismatch.tg");
    let source = fs::read_to_string(&file).unwrap();
    let analysis = host.analyze_source(&file, source);
    assert!(!analysis.diagnostics.is_empty());
    assert!(analysis.diagnostics.iter().any(|diagnostic| !diagnostic.message.is_empty()));
}

#[test]
fn completion_lists_stdlib_exports_after_dot() {
    let root = repo_root();
    let host = analysis::AnalysisHost::with_paths(root.clone(), root.join("stdlib"), false);
    let file = root.join("tests/fixtures/basic/strings.tg");
    let source = "import math\nfunc main() -> i32:\n  math.\n  return 0\n".to_string();
    let uri = Url::from_file_path(&file).unwrap();
    host.set_document(uri.clone(), source.clone());
    let analysis = host.analyze_source(&file, source);
    host.cache_analysis(uri.clone(), analysis);
    let response = completion::completions(&host, &uri, Position::new(2, 7)).unwrap();
    let tower_lsp::lsp_types::CompletionResponse::Array(items) = response else {
        panic!("expected array completion response");
    };
    assert!(items.iter().any(|item| item.label == "sqrt"));
}

#[test]
fn hover_shows_builtin_signature() {
    let root = repo_root();
    let host = analysis::AnalysisHost::with_paths(root.clone(), root.join("stdlib"), false);
    let dir = tempdir().unwrap();
    let file = dir.path().join("hover_builtin.tg");
    let source = "func main():\n  println(\"hi\")\n".to_string();
    fs::write(&file, &source).unwrap();
    let uri = Url::from_file_path(&file).unwrap();
    host.set_document(uri.clone(), source.clone());
    let analysis = host.analyze_source(&file, source);
    host.cache_analysis(uri.clone(), analysis);
    let hover = hover::hover(&host, &uri, Position::new(1, 5)).unwrap();
    let tower_lsp::lsp_types::HoverContents::Scalar(tower_lsp::lsp_types::MarkedString::String(value)) =
        hover.contents
    else {
        panic!("expected string hover");
    };
    assert!(value.contains("println"));
}

#[test]
fn goto_definition_jumps_to_local_binding() {
    let root = repo_root();
    let host = analysis::AnalysisHost::with_paths(root.clone(), root.join("stdlib"), false);
    let dir = tempdir().unwrap();
    let file = dir.path().join("goto_local.tg");
    let source = "func main() -> i32:\n  let value: i32 = 1\n  value\n  return 0\n".to_string();
    fs::write(&file, &source).unwrap();
    let uri = Url::from_file_path(&file).unwrap();
    host.set_document(uri.clone(), source.clone());
    let analysis = host.analyze_source(&file, source);
    host.cache_analysis(uri.clone(), analysis);
    let goto = goto::goto_definition(&host, &uri, Position::new(2, 3)).unwrap();
    let tower_lsp::lsp_types::GotoDefinitionResponse::Scalar(location) = goto else {
        panic!("expected scalar goto response");
    };
    assert_eq!(location.uri, uri);
    assert_eq!(location.range.start.line, 1);
}
