//! Debounced per-document analysis orchestration for the Thagore LSP.

use std::time::Duration;

use ropey::Rope;
use tokio::time::sleep;
use tower_lsp::Client;
use tower_lsp::lsp_types::{DidChangeTextDocumentParams, DidSaveTextDocumentParams, Url};

use crate::analysis::AnalysisHost;
use crate::diagnostics::position_to_offset;

/// Applies incremental text changes to the host document buffer.
pub(crate) fn apply_changes(host: &AnalysisHost, params: DidChangeTextDocumentParams) {
    let uri = params.text_document.uri;
    let mut rope = host
        .document_text(&uri)
        .map(|text| Rope::from_str(&text))
        .unwrap_or_default();

    for change in params.content_changes {
        if let Some(range) = change.range {
            let current = rope.to_string();
            let start = position_to_offset(&current, range.start);
            let end = position_to_offset(&current, range.end);
            let start_char = current[..start].chars().count();
            let end_char = current[..end].chars().count();
            rope.remove(start_char..end_char);
            rope.insert(start_char, &change.text);
        } else {
            rope = Rope::from_str(&change.text);
        }
    }

    host.set_document_rope(uri, rope);
}

/// Schedules a debounced re-analysis for `uri`.
pub(crate) fn schedule_recheck(host: AnalysisHost, client: Client, uri: Url) {
    host.cancel_debounce(&uri);
    let host_clone = host.clone();
    let uri_clone = uri.clone();
    let handle = tokio::spawn(async move {
        sleep(Duration::from_millis(300)).await;
        publish_analysis(host_clone, client, uri_clone).await;
    });
    host.set_debounce(uri, handle);
}

/// Runs an immediate re-analysis for `uri`.
pub(crate) async fn run_immediate_check(host: AnalysisHost, client: Client, uri: Url) {
    host.cancel_debounce(&uri);
    publish_analysis(host, client, uri).await;
}

/// Syncs the saved on-disk contents back into the tracked buffer before analyzing.
pub(crate) async fn handle_save(host: AnalysisHost, client: Client, params: DidSaveTextDocumentParams) {
    if let Ok(path) = params.text_document.uri.to_file_path() {
        if let Ok(source) = std::fs::read_to_string(path) {
            host.set_document(params.text_document.uri.clone(), source);
        }
    }
    run_immediate_check(host, client, params.text_document.uri).await;
}

async fn publish_analysis(host: AnalysisHost, client: Client, uri: Url) {
    eprintln!("[thagore-lsp] checking: {uri}");
    let analysis = host.analyze_uri(&uri);
    let diagnostics = analysis.diagnostics.clone();
    host.cache_analysis(uri.clone(), analysis);
    client.publish_diagnostics(uri, diagnostics, None).await;
}
