//! Completion provider for the Thagore LSP.

use tower_lsp::lsp_types::{
    CompletionItem, CompletionItemKind, CompletionItemLabelDetails, CompletionResponse,
    Documentation, InsertTextFormat, MarkupContent, MarkupKind, Position,
};

use crate::analysis::{completion_symbols, identifier_context, module_exports, AnalysisHost};
use crate::diagnostics::position_to_offset;

/// Computes completion items for one file position.
#[must_use]
pub(crate) fn completions(
    host: &AnalysisHost,
    uri: &tower_lsp::lsp_types::Url,
    position: Position,
) -> Option<CompletionResponse> {
    let analysis = host.cached_analysis(uri)?;
    let offset = position_to_offset(&analysis.source, position);
    let line_prefix = analysis.source[..offset]
        .rsplit_once('\n')
        .map(|(_, tail)| tail)
        .unwrap_or(&analysis.source[..offset]);

    if line_prefix.trim_start().starts_with("import ") {
        let items = host
            .stdlib_exports()
            .keys()
            .map(|name| CompletionItem {
                label: name.clone(),
                kind: Some(CompletionItemKind::MODULE),
                detail: Some("stdlib module".to_string()),
                ..CompletionItem::default()
            })
            .collect::<Vec<_>>();
        return Some(CompletionResponse::Array(items));
    }

    if line_prefix.ends_with("func ") {
        return Some(CompletionResponse::Array(vec![CompletionItem {
            label: "func".to_string(),
            kind: Some(CompletionItemKind::SNIPPET),
            detail: Some("function declaration snippet".to_string()),
            insert_text: Some("func ${1:name}(${2:params}) -> ${3:type}:\n  ${0}".to_string()),
            insert_text_format: Some(InsertTextFormat::SNIPPET),
            ..CompletionItem::default()
        }]));
    }

    let (qualifier, prefix) = identifier_context(&analysis.source, offset);
    let symbols = if let Some(qualifier) = qualifier {
        module_exports(&analysis, &qualifier)
            .map(|symbols| symbols.to_vec())
            .unwrap_or_default()
    } else {
        completion_symbols(&analysis, offset as u32)
    };

    let items = symbols
        .into_iter()
        .filter(|symbol| prefix.is_empty() || symbol.name.starts_with(&prefix))
        .map(|symbol| CompletionItem {
            label: symbol.name,
            label_details: Some(CompletionItemLabelDetails {
                detail: None,
                description: symbol.module_label.clone(),
            }),
            kind: Some(symbol.category.completion_kind()),
            detail: Some(symbol.detail),
            documentation: symbol.documentation.map(|doc| {
                Documentation::MarkupContent(MarkupContent {
                    kind: MarkupKind::Markdown,
                    value: doc,
                })
            }),
            insert_text_format: Some(InsertTextFormat::PLAIN_TEXT),
            ..CompletionItem::default()
        })
        .collect::<Vec<_>>();

    Some(CompletionResponse::Array(items))
}
