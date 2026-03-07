//! Diagnostic and source-span conversion helpers for the Thagore LSP.

use thagore_ast::Span;
use thagore_parser::ParseError;
use thagore_typeck::{TypeChecker, TypeError};
use tower_lsp::lsp_types::{Diagnostic, DiagnosticSeverity, Position, Range};

/// Converts a parser error into an LSP diagnostic.
#[must_use]
pub(crate) fn diagnostic_from_parse_error(source: &str, error: &ParseError) -> Diagnostic {
    Diagnostic {
        range: span_to_range(source, error.span),
        severity: Some(DiagnosticSeverity::ERROR),
        code: Some(tower_lsp::lsp_types::NumberOrString::String("parse".to_string())),
        code_description: None,
        source: Some("thagore-parser".to_string()),
        message: error.to_string(),
        related_information: None,
        tags: None,
        data: None,
    }
}

/// Converts a type-checking error into an LSP diagnostic.
#[must_use]
pub(crate) fn diagnostic_from_type_error(
    source: &str,
    error: &TypeError,
    checker: &TypeChecker,
) -> Diagnostic {
    let message = match error {
        TypeError::UnknownIdentifier { name, .. } => {
            let looked_up = checker.resolve_symbol_name(*name).unwrap_or("unknown");
            format!("unknown identifier `{looked_up}`")
        }
        other => other.to_string(),
    };
    Diagnostic {
        range: span_to_range(source, error.span()),
        severity: Some(DiagnosticSeverity::ERROR),
        code: Some(tower_lsp::lsp_types::NumberOrString::String("type".to_string())),
        code_description: None,
        source: Some("thagore-typeck".to_string()),
        message,
        related_information: None,
        tags: None,
        data: None,
    }
}

/// Converts a byte span into an LSP range.
#[must_use]
pub(crate) fn span_to_range(source: &str, span: Span) -> Range {
    Range::new(
        offset_to_position(source, span.start as usize),
        offset_to_position(source, span.end as usize),
    )
}

/// Converts a byte offset into an LSP position.
#[must_use]
pub(crate) fn offset_to_position(source: &str, offset: usize) -> Position {
    let mut line = 0_u32;
    let mut column = 0_u32;
    let clamped = offset.min(source.len());
    for ch in source[..clamped].chars() {
        if ch == '\n' {
            line += 1;
            column = 0;
        } else {
            column += ch.len_utf16() as u32;
        }
    }
    Position::new(line, column)
}

/// Converts an LSP position into a byte offset.
#[must_use]
pub(crate) fn position_to_offset(source: &str, position: Position) -> usize {
    let target_line = position.line as usize;
    let target_col = position.character as usize;
    let mut offset = 0_usize;
    let mut line = 0_usize;

    for segment in source.split_inclusive('\n') {
        let line_without_newline = segment.strip_suffix('\n').unwrap_or(segment);
        if line == target_line {
            let mut utf16 = 0_usize;
            let mut line_offset = 0_usize;
            for ch in line_without_newline.chars() {
                if utf16 >= target_col {
                    break;
                }
                utf16 += ch.len_utf16();
                line_offset += ch.len_utf8();
            }
            return offset + line_offset;
        }
        offset += segment.len();
        line += 1;
    }

    source.len()
}

/// Returns the zero-based source line containing `offset`.
#[must_use]
pub(crate) fn line_index_from_offset(source: &str, offset: u32) -> usize {
    source[..(offset as usize).min(source.len())]
        .chars()
        .filter(|ch| *ch == '\n')
        .count()
}

/// Extracts the line text at `line`.
#[must_use]
pub(crate) fn line_text(source: &str, line: usize) -> Option<&str> {
    source.lines().nth(line)
}

/// Extracts a doc comment block immediately preceding `offset`.
#[must_use]
pub(crate) fn doc_comment_for_offset(source: &str, offset: u32) -> Option<String> {
    let line_index = line_index_from_offset(source, offset);
    if line_index == 0 {
        return None;
    }

    let lines = source.lines().collect::<Vec<_>>();
    let mut current = line_index.saturating_sub(1);
    let mut doc_lines = Vec::new();
    loop {
        let line = lines.get(current)?.trim();
        if let Some(comment) = line.strip_prefix('#') {
            doc_lines.push(comment.trim().to_string());
        } else if line.is_empty() {
            if doc_lines.is_empty() {
                return None;
            }
            break;
        } else {
            break;
        }
        if current == 0 {
            break;
        }
        current -= 1;
    }
    if doc_lines.is_empty() {
        None
    } else {
        doc_lines.reverse();
        Some(doc_lines.join("\n"))
    }
}

/// Formats a type expression as a readable label.
#[must_use]
pub(crate) fn type_expr_label<'ast>(
    parser: &thagore_parser::Parser<'_, '_, 'ast>,
    ty: thagore_ast::TypeExprRef<'ast>,
) -> String {
    match ty {
        thagore_ast::TypeExpr::Named(node) => parser
            .resolve_symbol(node.name)
            .unwrap_or("__unknown__")
            .to_string(),
        thagore_ast::TypeExpr::Generic(node) => {
            let args = node
                .args
                .iter()
                .map(|arg| type_expr_label(parser, *arg))
                .collect::<Vec<_>>()
                .join(", ");
            format!(
                "{}<{args}>",
                parser.resolve_symbol(node.name).unwrap_or("__unknown__")
            )
        }
        thagore_ast::TypeExpr::Infer(_) => "_".to_string(),
    }
}
