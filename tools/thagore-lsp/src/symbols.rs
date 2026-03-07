//! Document and workspace symbol providers for the Thagore LSP.

use tower_lsp::lsp_types::{DocumentSymbol, SymbolInformation, Url};

use crate::analysis::{AnalysisHost, IndexedSymbol};
use crate::diagnostics::span_to_range;

/// Returns document symbols for the current file.
#[must_use]
pub(crate) fn document_symbols(
    host: &AnalysisHost,
    uri: &Url,
) -> Option<Vec<DocumentSymbol>> {
    host.cached_analysis(uri).map(|analysis| analysis.symbols)
}

/// Returns workspace symbols matching `query`.
#[must_use]
pub(crate) fn workspace_symbols(host: &AnalysisHost, query: &str) -> Vec<SymbolInformation> {
    host.workspace_symbols(query)
        .into_iter()
        .filter_map(symbol_to_info)
        .collect()
}

fn symbol_to_info(symbol: IndexedSymbol) -> Option<SymbolInformation> {
    let source = std::fs::read_to_string(&symbol.file_path).ok()?;
    let uri = Url::from_file_path(&symbol.file_path).ok()?;
    Some(SymbolInformation {
        name: symbol.name,
        kind: symbol.category.document_kind(),
        tags: None,
        deprecated: None,
        location: tower_lsp::lsp_types::Location::new(
            uri,
            span_to_range(&source, symbol.selection_span),
        ),
        container_name: symbol.module_label,
    })
}
