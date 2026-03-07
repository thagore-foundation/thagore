//! Go-to-definition provider for the Thagore LSP.

use tower_lsp::lsp_types::{GotoDefinitionResponse, Location, Position, Url};

use crate::analysis::{identifier_context, module_exports, symbol_at_offset, AnalysisHost};
use crate::diagnostics::{position_to_offset, span_to_range};

/// Resolves a definition location for the identifier at `position`.
#[must_use]
pub(crate) fn goto_definition(
    host: &AnalysisHost,
    uri: &Url,
    position: Position,
) -> Option<GotoDefinitionResponse> {
    let analysis = host.cached_analysis(uri)?;
    let offset = position_to_offset(&analysis.source, position);
    let (qualifier, ident) = identifier_context(&analysis.source, offset);

    if let Some(import) = analysis
        .imports
        .iter()
        .find(|import| import.span.contains(offset as u32))
        .and_then(|import| import.file_path.as_ref())
    {
        let uri = Url::from_file_path(import).ok()?;
        return Some(GotoDefinitionResponse::Scalar(Location::new(
            uri,
            tower_lsp::lsp_types::Range::new(
                tower_lsp::lsp_types::Position::new(0, 0),
                tower_lsp::lsp_types::Position::new(0, 0),
            ),
        )));
    }

    if let Some(qualifier) = qualifier {
        let symbol = module_exports(&analysis, &qualifier)
            .and_then(|symbols| symbols.iter().find(|symbol| symbol.name == ident))?;
        let source = std::fs::read_to_string(&symbol.file_path).ok()?;
        let uri = Url::from_file_path(&symbol.file_path).ok()?;
        return Some(GotoDefinitionResponse::Scalar(Location::new(
            uri,
            span_to_range(&source, symbol.selection_span),
        )));
    }

    let symbol = symbol_at_offset(&analysis, offset as u32).or_else(|| {
        analysis
            .imports
            .iter()
            .flat_map(|import| import.direct_symbols.iter())
            .find(|symbol| symbol.name == ident)
            .cloned()
    })?;
    let source = std::fs::read_to_string(&symbol.file_path).ok()?;
    let uri = Url::from_file_path(&symbol.file_path).ok()?;
    Some(GotoDefinitionResponse::Scalar(Location::new(
        uri,
        span_to_range(&source, symbol.selection_span),
    )))
}
