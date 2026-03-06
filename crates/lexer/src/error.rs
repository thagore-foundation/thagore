//! Lexer diagnostics and recovery metadata.

use crate::token::{Span, Token};

/// Hard tab indentation error message.
pub const TAB_INDENTATION_ERROR: &str = "tabs are not allowed, use 2 spaces";
/// Invalid indentation-width error message.
pub const INVALID_INDENTATION_ERROR: &str = "invalid indentation, must be a multiple of 2 spaces";
/// Dedent recovery error message.
pub const INCONSISTENT_DEDENT_ERROR: &str = "inconsistent dedent";
/// Unterminated string literal error message.
pub const UNTERMINATED_STRING_ERROR: &str = "unterminated string literal";
/// Invalid token error message.
pub const INVALID_TOKEN_ERROR: &str = "invalid token";
/// Invalid `!` operator error message.
pub const INVALID_BANG_ERROR: &str = "expected '=' after '!'";

/// A recoverable lexer diagnostic.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct LexError {
    /// The source span where the error occurred.
    pub span: Span,
    /// The static diagnostic message.
    pub message: &'static str,
}

impl LexError {
    /// Creates a new recoverable lexer error.
    #[must_use]
    pub const fn new(span: Span, message: &'static str) -> Self {
        Self { span, message }
    }

    /// Converts the error into an error token.
    #[must_use]
    pub const fn to_token(self) -> Token {
        Token::with_error(self.span, self.message)
    }
}

/// The recovery strategy used after a lexer error.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RecoveryKind {
    /// Skip a single offending byte or codepoint.
    SkipByte,
    /// Skip until the end of the current physical line.
    SkipToLineEnd,
    /// Emit pending dedents and continue at EOF.
    FlushDedents,
}

/// Metadata describing where the lexer resumed after an error.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RecoveryPoint {
    /// The span consumed while recovering.
    pub span: Span,
    /// The strategy that was applied.
    pub kind: RecoveryKind,
}

impl RecoveryPoint {
    /// Creates a new recovery point.
    #[must_use]
    pub const fn new(span: Span, kind: RecoveryKind) -> Self {
        Self { span, kind }
    }
}
