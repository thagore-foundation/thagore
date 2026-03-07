//! Hover provider for the Thagore LSP.

use tower_lsp::lsp_types::{Hover, HoverContents, MarkedString, Position, Url};

use crate::analysis::{
    identifier_context, module_exports, nearest_symbol_name, symbol_at_offset, AnalysisHost,
    BUILTINS,
};
use crate::diagnostics::{position_to_offset, span_to_range};

/// Computes hover contents for one file position.
#[must_use]
pub(crate) fn hover(host: &AnalysisHost, uri: &Url, position: Position) -> Option<Hover> {
    let analysis = host.cached_analysis(uri)?;
    let offset = position_to_offset(&analysis.source, position);
    let (qualifier, ident) = identifier_context(&analysis.source, offset);

    if let Some(qualifier) = qualifier {
        if let Some(symbol) = module_exports(&analysis, &qualifier)
            .and_then(|symbols| symbols.iter().find(|symbol| symbol.name == ident))
        {
            let mut value = symbol.detail.clone();
            if let Some(doc) = &symbol.documentation {
                value.push_str("\n\n");
                value.push_str(doc);
            }
            return Some(Hover {
                contents: HoverContents::Scalar(MarkedString::String(value)),
                range: Some(span_to_range(&analysis.source, symbol.selection_span)),
            });
        }
    }

    if let Some(import) = analysis.imports.iter().find(|import| import.qualifier == ident) {
        let target = import
            .file_path
            .as_ref()
            .map(|path| path.display().to_string())
            .unwrap_or_else(|| import.import_path.clone());
        return Some(Hover {
            contents: HoverContents::Scalar(MarkedString::String(format!(
                "import {} -> {}",
                import.import_path, target
            ))),
            range: Some(span_to_range(&analysis.source, import.span)),
        });
    }

    if let Some((name, params, doc)) = BUILTINS.iter().find(|(name, _, _)| *name == ident) {
        return Some(Hover {
            contents: HoverContents::Scalar(MarkedString::String(format!(
                "{name}{params}\n\n{doc}"
            ))),
            range: None,
        });
    }

    if let Some(symbol) = symbol_at_offset(&analysis, offset as u32) {
        let mut value = symbol.detail.clone();
        if let Some(doc) = &symbol.documentation {
            value.push_str("\n\n");
            value.push_str(doc);
        }
        return Some(Hover {
            contents: HoverContents::Scalar(MarkedString::String(value)),
            range: Some(span_to_range(&analysis.source, symbol.selection_span)),
        });
    }

    nearest_symbol_name(
        &ident,
        BUILTINS
            .iter()
            .map(|(name, _, _)| *name)
            .chain(analysis.indexed_symbols.iter().map(|symbol| symbol.name.as_str())),
    )
    .map(|suggestion| Hover {
        contents: HoverContents::Scalar(MarkedString::String(format!(
            "unknown symbol `{ident}`\n\ndid you mean `{suggestion}`?"
        ))),
        range: None,
    })
}
