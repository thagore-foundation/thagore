//! Parse diagnostics and panic-mode recovery helpers for the Thagore parser.
//!
//! This module is allocation-free and `no_std` compatible. It provides the
//! structured error types emitted by the recursive-descent parser and the token
//! classification helpers used for synchronization after a syntax error.

use core::fmt;

use thagore_ast::Span;
use thagore_lexer::{Token, TokenKind};

/// A structured parser diagnostic.
///
/// Each parse error carries the source span of the failure site together with a
/// machine-readable [`ErrorKind`] that downstream tooling can inspect.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ParseError {
    /// Source span of the parse failure.
    pub span: Span,
    /// Semantic category of the parse failure.
    pub kind: ErrorKind,
}

impl ParseError {
    /// Creates a new parse error.
    #[must_use]
    pub const fn new(span: Span, kind: ErrorKind) -> Self {
        Self { span, kind }
    }

    /// Creates an error for an unexpected token.
    #[must_use]
    pub const fn unexpected_token(found: TokenKind, span: Span, expected: Expectation) -> Self {
        Self::new(span, ErrorKind::UnexpectedToken { expected, found })
    }

    /// Creates an error for a missing token that should have been present at
    /// the current cursor position.
    #[must_use]
    pub const fn missing_token(span: Span, expected: TokenKind) -> Self {
        Self::new(span, ErrorKind::MissingToken { expected })
    }

    /// Creates an error for a missing opening parenthesis around a condition.
    #[must_use]
    pub const fn missing_condition_lparen(span: Span) -> Self {
        Self::new(
            span,
            ErrorKind::MissingConditionDelimiter {
                expected: ConditionDelimiter::OpenParen,
            },
        )
    }

    /// Creates an error for a missing closing parenthesis around a condition.
    #[must_use]
    pub const fn missing_condition_rparen(span: Span) -> Self {
        Self::new(
            span,
            ErrorKind::MissingConditionDelimiter {
                expected: ConditionDelimiter::CloseParen,
            },
        )
    }

    /// Creates an error for a missing block-introducing colon.
    #[must_use]
    pub const fn missing_block_colon(span: Span) -> Self {
        Self::new(span, ErrorKind::MissingBlockColon)
    }

    /// Creates an error for a missing dedent that should have closed a block.
    #[must_use]
    pub const fn missing_dedent(span: Span) -> Self {
        Self::new(span, ErrorKind::MissingDedent)
    }

    /// Creates an error for an expression recovery placeholder.
    #[must_use]
    pub const fn expected_expression(span: Span, found: TokenKind) -> Self {
        Self::new(
            span,
            ErrorKind::UnexpectedToken {
                expected: Expectation::Expression,
                found,
            },
        )
    }

    /// Creates an error that wraps a recoverable lexer token.
    #[must_use]
    pub const fn lexer_error(span: Span, message: &'static str) -> Self {
        Self::new(span, ErrorKind::LexerError { message })
    }

    /// Returns the human-readable diagnostic message.
    #[must_use]
    pub const fn message(&self) -> &'static str {
        self.kind.message()
    }
}

impl fmt::Display for ParseError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} at {}", self.kind, self.span)
    }
}

/// A parser error category.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ErrorKind {
    /// The current token does not satisfy the expected grammar production.
    UnexpectedToken {
        /// What the parser expected at this position.
        expected: Expectation,
        /// The token category that was actually found.
        found: TokenKind,
    },
    /// A required token was omitted in the source.
    MissingToken {
        /// The token category that should have appeared.
        expected: TokenKind,
    },
    /// A block header omitted the required trailing `:`.
    MissingBlockColon,
    /// A condition omitted a required delimiter.
    MissingConditionDelimiter {
        /// Which condition delimiter was missing.
        expected: ConditionDelimiter,
    },
    /// A nested block was not closed with a matching `DEDENT`.
    MissingDedent,
    /// The lexer produced a recoverable token-level error.
    LexerError {
        /// Static lexer error message.
        message: &'static str,
    },
}

impl ErrorKind {
    /// Returns the human-readable diagnostic message.
    #[must_use]
    pub const fn message(&self) -> &'static str {
        match self {
            Self::UnexpectedToken {
                expected: Expectation::Identifier,
                ..
            } => "expected identifier",
            Self::UnexpectedToken {
                expected: Expectation::Expression,
                ..
            } => "expected expression",
            Self::UnexpectedToken {
                expected: Expectation::TypeExpr,
                ..
            } => "expected type expression",
            Self::UnexpectedToken {
                expected: Expectation::Declaration,
                ..
            } => "expected declaration",
            Self::UnexpectedToken {
                expected: Expectation::Statement,
                ..
            } => "expected statement",
            Self::UnexpectedToken {
                expected: Expectation::Block,
                ..
            } => "expected block",
            Self::UnexpectedToken {
                expected: Expectation::Parameter,
                ..
            } => "expected parameter",
            Self::UnexpectedToken {
                expected: Expectation::Field,
                ..
            } => "expected field definition",
            Self::UnexpectedToken {
                expected: Expectation::FlowStage,
                ..
            } => "expected flow stage",
            Self::UnexpectedToken {
                expected: Expectation::ImportPathSegment,
                ..
            } => "expected import path segment",
            Self::UnexpectedToken {
                expected: Expectation::Token(_),
                ..
            } => "unexpected token",
            Self::MissingToken { .. } => "missing token",
            Self::MissingBlockColon => "expected ':' before block body",
            Self::MissingConditionDelimiter {
                expected: ConditionDelimiter::OpenParen,
            } => "expected '(' before condition",
            Self::MissingConditionDelimiter {
                expected: ConditionDelimiter::CloseParen,
            } => "expected ')' after condition",
            Self::MissingDedent => "expected dedent to close block",
            Self::LexerError { message } => message,
        }
    }
}

impl fmt::Display for ErrorKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::UnexpectedToken { expected, found } => {
                write!(f, "expected {expected}, found {found}")
            }
            Self::MissingToken { expected } => write!(f, "expected {expected}"),
            Self::MissingBlockColon => f.write_str("expected ':' before block body"),
            Self::MissingConditionDelimiter { expected } => write!(f, "expected {expected}"),
            Self::MissingDedent => f.write_str("expected dedent to close block"),
            Self::LexerError { message } => f.write_str(message),
        }
    }
}

/// A parser expectation used when building diagnostics.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Expectation {
    /// A specific token category was expected.
    Token(TokenKind),
    /// Any identifier token was expected.
    Identifier,
    /// Any expression was expected.
    Expression,
    /// Any type expression was expected.
    TypeExpr,
    /// Any declaration was expected.
    Declaration,
    /// Any statement was expected.
    Statement,
    /// Any block body was expected.
    Block,
    /// A function or extern parameter was expected.
    Parameter,
    /// A struct field definition was expected.
    Field,
    /// A flow stage was expected.
    FlowStage,
    /// A path segment in an import declaration was expected.
    ImportPathSegment,
}

impl fmt::Display for Expectation {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Token(kind) => write!(f, "{kind}"),
            Self::Identifier => f.write_str("identifier"),
            Self::Expression => f.write_str("expression"),
            Self::TypeExpr => f.write_str("type expression"),
            Self::Declaration => f.write_str("declaration"),
            Self::Statement => f.write_str("statement"),
            Self::Block => f.write_str("block"),
            Self::Parameter => f.write_str("parameter"),
            Self::Field => f.write_str("field definition"),
            Self::FlowStage => f.write_str("flow stage"),
            Self::ImportPathSegment => f.write_str("import path segment"),
        }
    }
}

/// A required delimiter around `if` and `while` conditions.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ConditionDelimiter {
    /// Opening parenthesis before the condition expression.
    OpenParen,
    /// Closing parenthesis after the condition expression.
    CloseParen,
}

impl fmt::Display for ConditionDelimiter {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::OpenParen => f.write_str("'(' before condition"),
            Self::CloseParen => f.write_str("')' after condition"),
        }
    }
}

/// Converts a lexer span into the AST/parser span type.
#[must_use]
pub const fn span_from_lexer(span: thagore_lexer::Span) -> Span {
    Span::new(span.start, span.end)
}

/// Returns `true` when `kind` ends the current statement for panic-mode
/// recovery.
#[must_use]
pub const fn is_statement_boundary(kind: TokenKind) -> bool {
    matches!(
        kind,
        TokenKind::Newline | TokenKind::Dedent | TokenKind::Eof
    )
}

/// Returns `true` when `kind` can start a declaration at the current scope.
#[must_use]
pub const fn is_declaration_start(kind: TokenKind) -> bool {
    matches!(
        kind,
        TokenKind::Func
            | TokenKind::Let
            | TokenKind::Const
            | TokenKind::Struct
            | TokenKind::Impl
            | TokenKind::Import
            | TokenKind::From
            | TokenKind::Extern
            | TokenKind::Intent
            | TokenKind::Flow
    )
}

/// Returns `true` when `kind` can start a statement.
#[must_use]
pub const fn is_statement_start(kind: TokenKind) -> bool {
    is_declaration_start(kind)
        || matches!(
            kind,
            TokenKind::If
                | TokenKind::While
                | TokenKind::For
                | TokenKind::Return
                | TokenKind::Identifier
                | TokenKind::Integer
                | TokenKind::Float
                | TokenKind::String
                | TokenKind::Bool
                | TokenKind::LParen
                | TokenKind::Minus
        )
}

/// Returns `true` when `kind` is a safe panic-mode synchronization point.
#[must_use]
pub const fn is_sync_point(kind: TokenKind) -> bool {
    is_statement_boundary(kind)
        || matches!(kind, TokenKind::Else)
        || is_declaration_start(kind)
        || is_statement_start(kind)
}

/// Returns `true` when a token should terminate expression parsing.
#[must_use]
pub const fn is_expr_terminator(kind: TokenKind) -> bool {
    matches!(
        kind,
        TokenKind::Newline
            | TokenKind::Dedent
            | TokenKind::Eof
            | TokenKind::Colon
            | TokenKind::Comma
            | TokenKind::RParen
            | TokenKind::RBracket
    )
}

/// Returns `true` when `token` is a recoverable lexer error token.
#[must_use]
pub const fn is_lexer_error_token(token: Token) -> bool {
    matches!(token.kind, TokenKind::Error)
}
