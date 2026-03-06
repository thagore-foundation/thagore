//! Debug information support for the Thagore LLVM backend.
//!
//! This backend currently exposes a stable interface for debug emission and
//! degrades to a no-op implementation when detailed source mapping is not
//! available.

use thagore_ast::Span;

/// Debug emission configuration.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DebugOptions {
    /// Whether DWARF debug info emission is requested.
    pub enabled: bool,
    /// Logical source file name attached to the compilation unit.
    pub source_name: String,
}

impl Default for DebugOptions {
    fn default() -> Self {
        Self {
            enabled: false,
            source_name: "module.tg".into(),
        }
    }
}

/// Lightweight debug state wrapper.
#[derive(Debug, Clone)]
pub struct DebugState {
    options: DebugOptions,
}

impl DebugState {
    /// Creates a new debug state.
    #[must_use]
    pub fn new(options: DebugOptions) -> Self {
        Self { options }
    }

    /// Returns `true` when debug emission is enabled.
    #[must_use]
    pub fn enabled(&self) -> bool {
        self.options.enabled
    }

    /// Returns the logical source file name.
    #[must_use]
    pub fn source_name(&self) -> &str {
        &self.options.source_name
    }

    /// Returns a best-effort line number derived from a span.
    #[must_use]
    pub fn line_for_span(&self, span: Option<Span>) -> u32 {
        span.map(|span| span.start.saturating_add(1)).unwrap_or(1)
    }
}
