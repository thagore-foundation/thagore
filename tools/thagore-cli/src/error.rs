//! Diagnostic formatting for the Thagore CLI.

use std::borrow::Cow;
use std::io::{self, Write};
use std::path::Path;

use serde::Serialize;
use termcolor::{Color, ColorSpec, WriteColor};
use thagore_ast::Span;

/// Structured compiler diagnostic emitted by the CLI pipeline.
#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct CompilerDiagnostic {
    /// Stable machine-readable diagnostic code.
    pub code: &'static str,
    /// Short headline shown beside the diagnostic code.
    pub title: Cow<'static, str>,
    /// Detailed message shown beside the caret marker.
    pub message: String,
    /// Source location for the diagnostic, when available.
    pub span: Option<Span>,
    /// Optional remediation hint.
    pub hint: Option<Cow<'static, str>>,
    /// Diagnostic severity.
    pub severity: Severity,
}

impl CompilerDiagnostic {
    /// Creates a new compiler diagnostic.
    #[must_use]
    pub(crate) fn new(
        code: &'static str,
        title: impl Into<Cow<'static, str>>,
        message: impl Into<String>,
        span: Option<Span>,
    ) -> Self {
        Self {
            code,
            title: title.into(),
            message: message.into(),
            span,
            hint: None,
            severity: Severity::Error,
        }
    }

    /// Attaches an optional remediation hint.
    #[must_use]
    pub(crate) fn with_hint(mut self, hint: impl Into<Cow<'static, str>>) -> Self {
        self.hint = Some(hint.into());
        self
    }
}

/// Machine-readable diagnostic severity.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
#[allow(dead_code)]
pub(crate) enum Severity {
    /// Hard compilation error.
    Error,
    /// Non-fatal warning.
    Warning,
}

/// JSON-serializable diagnostic payload for editor integration.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub(crate) struct JsonDiagnostic {
    /// Source file path.
    pub file: String,
    /// One-based line number.
    pub line: usize,
    /// One-based column number.
    pub col: usize,
    /// One-based inclusive end line number.
    pub end_line: usize,
    /// One-based inclusive end column number.
    pub end_col: usize,
    /// Full user-facing diagnostic message.
    pub message: String,
    /// Diagnostic severity.
    pub severity: Severity,
    /// Optional remediation hint.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub hint: Option<String>,
}

/// Renders diagnostics in text or JSON form.
#[derive(Debug, Default)]
pub(crate) struct ErrorReporter;

impl ErrorReporter {
    /// Emits human-readable diagnostics with source snippets and carets.
    pub(crate) fn emit_text<W>(
        writer: &mut W,
        file: &Path,
        source: &str,
        diagnostics: &[CompilerDiagnostic],
    ) -> io::Result<()>
    where
        W: WriteColor,
    {
        for diagnostic in diagnostics {
            render_text_diagnostic(writer, file, source, diagnostic)?;
        }
        Ok(())
    }

    /// Emits diagnostics as a JSON array.
    pub(crate) fn emit_json<W: Write>(
        mut writer: W,
        file: &Path,
        source: &str,
        diagnostics: &[CompilerDiagnostic],
    ) -> io::Result<()> {
        let json = diagnostics
            .iter()
            .map(|diagnostic| json_diagnostic(file, source, diagnostic))
            .collect::<Vec<_>>();
        serde_json::to_writer(&mut writer, &json)?;
        writeln!(writer)
    }
}

fn render_text_diagnostic<W>(
    writer: &mut W,
    file: &Path,
    source: &str,
    diagnostic: &CompilerDiagnostic,
) -> io::Result<()>
where
    W: WriteColor,
{
    writer.set_color(ColorSpec::new().set_fg(Some(Color::Red)).set_bold(true))?;
    write!(writer, "error[{}]", diagnostic.code)?;
    writer.reset()?;
    writeln!(writer, ": {}", diagnostic.title)?;

    let location = diagnostic
        .span
        .map(|span| resolve_location(source, span))
        .unwrap_or_else(|| SourceLocation::fallback());
    writeln!(
        writer,
        "  --> {}:{}:{}",
        file.display(),
        location.line,
        location.column
    )?;
    writeln!(writer, "   |")?;
    writeln!(writer, "{:>2} | {}", location.line, location.text)?;

    writer.write_all(b"   | ")?;
    for _ in 0..location.column.saturating_sub(1) {
        writer.write_all(b" ")?;
    }
    writer.set_color(ColorSpec::new().set_fg(Some(Color::Red)).set_bold(true))?;
    for _ in 0..location.caret_width {
        writer.write_all(b"^")?;
    }
    writer.reset()?;
    writeln!(writer, " {}", diagnostic.message)?;
    writeln!(writer, "   |")?;

    if let Some(hint) = &diagnostic.hint {
        writer.set_color(ColorSpec::new().set_fg(Some(Color::Cyan)).set_bold(true))?;
        write!(writer, "   = hint:")?;
        writer.reset()?;
        writeln!(writer, " {hint}")?;
    }
    Ok(())
}

fn json_diagnostic(file: &Path, source: &str, diagnostic: &CompilerDiagnostic) -> JsonDiagnostic {
    let location = diagnostic
        .span
        .map(|span| resolve_location(source, span))
        .unwrap_or_else(|| SourceLocation::fallback());

    JsonDiagnostic {
        file: file.display().to_string(),
        line: location.line,
        col: location.column,
        end_line: location.end_line,
        end_col: location.end_column,
        message: if diagnostic.message.is_empty() {
            diagnostic.title.to_string()
        } else {
            format!("{}: {}", diagnostic.title, diagnostic.message)
        },
        severity: diagnostic.severity,
        hint: diagnostic.hint.as_ref().map(ToString::to_string),
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct SourceLocation {
    line: usize,
    column: usize,
    end_line: usize,
    end_column: usize,
    caret_width: usize,
    text: String,
}

impl SourceLocation {
    fn fallback() -> Self {
        Self {
            line: 1,
            column: 1,
            end_line: 1,
            end_column: 1,
            caret_width: 1,
            text: String::new(),
        }
    }
}

fn resolve_location(source: &str, span: Span) -> SourceLocation {
    let start = usize::min(span.start as usize, source.len());
    let end = usize::min(span.end as usize, source.len());

    let line_start = source[..start].rfind('\n').map_or(0, |idx| idx + 1);
    let line_end = source[end..]
        .find('\n')
        .map_or(source.len(), |idx| end + idx);
    let line = source[..start].bytes().filter(|byte| *byte == b'\n').count() + 1;
    let column = source[line_start..start].chars().count() + 1;
    let end_line = source[..end].bytes().filter(|byte| *byte == b'\n').count() + 1;
    let end_column = source[line_start..end].chars().count() + 1;
    let caret_width = usize::max(1, source[start..end].chars().count());

    SourceLocation {
        line,
        column,
        end_line,
        end_column: usize::max(column, end_column),
        caret_width,
        text: source[line_start..line_end].to_string(),
    }
}

#[cfg(test)]
mod tests {
    use super::{CompilerDiagnostic, ErrorReporter};
    use std::path::Path;
    use termcolor::Buffer;
    use thagore_ast::Span;

    #[test]
    fn text_formatter_renders_snippet_and_hint() {
        let mut buffer = Buffer::no_color();
        let diagnostics = vec![CompilerDiagnostic::new(
            "E001",
            "type mismatch",
            "expected i32, found f64",
            Some(Span::new(17, 21)),
        )
        .with_hint("use an explicit conversion")];

        ErrorReporter::emit_text(
            &mut buffer,
            Path::new("src/main.tg"),
            "let x: i32 = 3.14\n",
            &diagnostics,
        )
        .expect("emit text");

        let rendered = String::from_utf8(buffer.into_inner()).expect("utf8");
        assert!(rendered.contains("error[E001]: type mismatch"));
        assert!(rendered.contains("src/main.tg:1:18"));
        assert!(rendered.contains("expected i32, found f64"));
        assert!(rendered.contains("hint: use an explicit conversion"));
    }

    #[test]
    fn json_formatter_emits_array_payload() {
        let diagnostics = vec![CompilerDiagnostic::new(
            "P001",
            "expected expression",
            "expected expression",
            Some(Span::new(4, 4)),
        )];
        let mut buffer = Vec::new();

        ErrorReporter::emit_json(
            &mut buffer,
            Path::new("broken.tg"),
            "let \n",
            &diagnostics,
        )
        .expect("emit json");

        let rendered: serde_json::Value = serde_json::from_slice(&buffer).expect("json");
        let array = rendered.as_array().expect("array");
        assert_eq!(array[0]["file"], "broken.tg");
        assert_eq!(array[0]["severity"], "error");
        assert_eq!(array[0]["end_line"], 1);
    }
}
