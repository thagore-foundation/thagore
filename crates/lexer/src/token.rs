//! Core token model for the Thagore lexer.
//!
//! The lexer is fully byte-oriented: all spans and zero-copy references use
//! byte offsets into the original source buffer.

use core::fmt;

/// A half-open byte span into the original source buffer.
///
/// `start` is inclusive and `end` is exclusive.
#[derive(Clone, Copy, Debug, Default, Eq, Hash, PartialEq)]
pub struct Span {
    /// Inclusive byte offset of the first byte in the span.
    pub start: u32,
    /// Exclusive byte offset one past the end of the span.
    pub end: u32,
}

impl Span {
    /// Creates a new half-open span.
    #[must_use]
    pub const fn new(start: u32, end: u32) -> Self {
        Self { start, end }
    }

    /// Returns the byte length of the span.
    #[must_use]
    pub const fn len(self) -> u32 {
        self.end.saturating_sub(self.start)
    }

    /// Returns `true` when the span is empty.
    #[must_use]
    pub const fn is_empty(self) -> bool {
        self.start >= self.end
    }

    /// Returns a span that covers both input spans.
    #[must_use]
    pub const fn join(self, other: Self) -> Self {
        let start = if self.start <= other.start {
            self.start
        } else {
            other.start
        };
        let end = if self.end >= other.end {
            self.end
        } else {
            other.end
        };
        Self { start, end }
    }
}

/// A zero-copy reference into the source buffer.
///
/// This is used for identifier and literal text without allocating per token.
#[derive(Clone, Copy, Debug, Default, Eq, Hash, PartialEq)]
pub struct SliceRef {
    /// Byte offset of the referenced slice.
    pub offset: u32,
    /// Byte length of the referenced slice.
    pub len: u32,
}

impl SliceRef {
    /// Creates a new zero-copy source slice reference.
    #[must_use]
    pub const fn new(offset: u32, len: u32) -> Self {
        Self { offset, len }
    }

    /// Builds a slice reference from a span.
    #[must_use]
    pub const fn from_span(span: Span) -> Self {
        Self {
            offset: span.start,
            len: span.len(),
        }
    }

    /// Returns the exclusive end byte offset of the slice.
    #[must_use]
    pub const fn end(self) -> u32 {
        self.offset.saturating_add(self.len)
    }

    /// Converts the slice reference back into a span.
    #[must_use]
    pub const fn span(self) -> Span {
        Span::new(self.offset, self.end())
    }

    /// Returns `true` when the slice reference is empty.
    #[must_use]
    pub const fn is_empty(self) -> bool {
        self.len == 0
    }
}

/// All token categories produced by the Thagore lexer.
#[repr(u16)]
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum TokenKind {
    /// An identifier that was not recognized as a reserved word.
    Identifier,
    /// An integer literal.
    Integer,
    /// A floating-point literal.
    Float,
    /// A string literal.
    String,

    /// `let`
    Let,
    /// `func`
    Func,
    /// `if`
    If,
    /// `else`
    Else,
    /// `while`
    While,
    /// `for`
    For,
    /// `return`
    Return,
    /// `const`
    Const,
    /// `from`
    From,
    /// `import`
    Import,
    /// `include`
    Include,
    /// `extern`
    Extern,
    /// `struct`
    Struct,
    /// `impl`
    Impl,
    /// `intent`
    Intent,
    /// `flow`
    Flow,

    /// Built-in primitive type `i32`.
    I32,
    /// Built-in primitive type `f32`.
    F32,
    /// Built-in primitive type `bool`.
    Bool,
    /// Built-in primitive type `str`.
    Str,

    /// `+`
    Plus,
    /// `-`
    Minus,
    /// `*`
    Star,
    /// `/`
    Slash,
    /// `%`
    Percent,
    /// `==`
    EqEq,
    /// `!=`
    BangEq,
    /// `<`
    Lt,
    /// `>`
    Gt,
    /// `<=`
    LtEq,
    /// `>=`
    GtEq,
    /// `=`
    Assign,
    /// `->`
    Arrow,
    /// `:`
    Colon,
    /// `,`
    Comma,
    /// `.`
    Dot,
    /// `(`
    LParen,
    /// `)`
    RParen,
    /// `[`
    LBracket,
    /// `]`
    RBracket,

    /// Logical line terminator emitted by the lexer.
    Newline,
    /// Synthetic indentation-open token.
    Indent,
    /// Synthetic indentation-close token.
    Dedent,
    /// End-of-file marker.
    Eof,
    /// Recoverable lexer error token.
    Error,
}

impl TokenKind {
    /// Returns `true` when this token is a reserved word.
    #[must_use]
    pub const fn is_keyword(self) -> bool {
        matches!(
            self,
            Self::Let
                | Self::Func
                | Self::If
                | Self::Else
                | Self::While
                | Self::For
                | Self::Return
                | Self::Const
                | Self::From
                | Self::Import
                | Self::Include
                | Self::Extern
                | Self::Struct
                | Self::Impl
                | Self::Intent
                | Self::Flow
                | Self::I32
                | Self::F32
                | Self::Bool
                | Self::Str
        )
    }

    /// Returns `true` when this token is one of the built-in type names.
    #[must_use]
    pub const fn is_builtin_type(self) -> bool {
        matches!(self, Self::I32 | Self::F32 | Self::Bool | Self::Str)
    }

    /// Returns `true` when this token is a literal token.
    #[must_use]
    pub const fn is_literal(self) -> bool {
        matches!(self, Self::Integer | Self::Float | Self::String)
    }

    /// Returns `true` when this token is emitted synthetically by the lexer.
    #[must_use]
    pub const fn is_synthetic(self) -> bool {
        matches!(
            self,
            Self::Newline | Self::Indent | Self::Dedent | Self::Eof
        )
    }

    /// Returns `true` when this token represents a recoverable lexer error.
    #[must_use]
    pub const fn is_error(self) -> bool {
        matches!(self, Self::Error)
    }
}

impl fmt::Display for TokenKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let name = match self {
            Self::Identifier => "identifier",
            Self::Integer => "integer",
            Self::Float => "float",
            Self::String => "string",
            Self::Let => "let",
            Self::Func => "func",
            Self::If => "if",
            Self::Else => "else",
            Self::While => "while",
            Self::For => "for",
            Self::Return => "return",
            Self::Const => "const",
            Self::From => "from",
            Self::Import => "import",
            Self::Include => "include",
            Self::Extern => "extern",
            Self::Struct => "struct",
            Self::Impl => "impl",
            Self::Intent => "intent",
            Self::Flow => "flow",
            Self::I32 => "i32",
            Self::F32 => "f32",
            Self::Bool => "bool",
            Self::Str => "str",
            Self::Plus => "+",
            Self::Minus => "-",
            Self::Star => "*",
            Self::Slash => "/",
            Self::Percent => "%",
            Self::EqEq => "==",
            Self::BangEq => "!=",
            Self::Lt => "<",
            Self::Gt => ">",
            Self::LtEq => "<=",
            Self::GtEq => ">=",
            Self::Assign => "=",
            Self::Arrow => "->",
            Self::Colon => ":",
            Self::Comma => ",",
            Self::Dot => ".",
            Self::LParen => "(",
            Self::RParen => ")",
            Self::LBracket => "[",
            Self::RBracket => "]",
            Self::Newline => "newline",
            Self::Indent => "indent",
            Self::Dedent => "dedent",
            Self::Eof => "eof",
            Self::Error => "error",
        };

        f.write_str(name)
    }
}

/// Allocation-free token payload data.
#[derive(Clone, Copy, Debug, Default, Eq, Hash, PartialEq)]
pub enum TokenData {
    /// No extra payload is attached to the token.
    #[default]
    None,
    /// Zero-copy source text for identifiers and literals.
    Slice(SliceRef),
    /// Static recoverable error message for `TokenKind::Error`.
    Error(&'static str),
}

/// A single lexed token.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct Token {
    /// The token category.
    pub kind: TokenKind,
    /// The byte span covered by this token.
    pub span: Span,
    /// Optional zero-copy payload or error message.
    pub data: TokenData,
}

impl Token {
    /// Creates a token without auxiliary payload.
    #[must_use]
    pub const fn new(kind: TokenKind, span: Span) -> Self {
        Self {
            kind,
            span,
            data: TokenData::None,
        }
    }

    /// Creates a token with a zero-copy source slice payload.
    #[must_use]
    pub const fn with_slice(kind: TokenKind, span: Span, slice: SliceRef) -> Self {
        Self {
            kind,
            span,
            data: TokenData::Slice(slice),
        }
    }

    /// Creates a recoverable lexer error token.
    #[must_use]
    pub const fn with_error(span: Span, message: &'static str) -> Self {
        Self {
            kind: TokenKind::Error,
            span,
            data: TokenData::Error(message),
        }
    }

    /// Returns the zero-copy source slice attached to this token, if present.
    #[must_use]
    pub const fn slice(self) -> Option<SliceRef> {
        match self.data {
            TokenData::Slice(slice) => Some(slice),
            TokenData::None | TokenData::Error(_) => None,
        }
    }

    /// Returns the static error message attached to this token, if present.
    #[must_use]
    pub const fn error_message(self) -> Option<&'static str> {
        match self.data {
            TokenData::Error(message) => Some(message),
            TokenData::None | TokenData::Slice(_) => None,
        }
    }
}

impl Default for Token {
    fn default() -> Self {
        Self::new(TokenKind::Eof, Span::default())
    }
}
