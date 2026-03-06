//! Static lexer dispatch and DFA transition tables.

/// First-byte dispatch action for the lexer.
#[repr(u8)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TokenAction {
    /// The byte does not begin any valid token.
    Invalid,
    /// Skip insignificant horizontal whitespace.
    SkipSpace,
    /// Emit an error for a hard tab character.
    TabError,
    /// Begin newline processing.
    Newline,
    /// Skip a single-line comment.
    Comment,
    /// Scan an identifier or keyword.
    Identifier,
    /// Scan a unicode identifier.
    UnicodeIdentifier,
    /// Scan a numeric literal.
    Number,
    /// Scan a string literal.
    String,
    /// Emit `+`.
    Plus,
    /// Emit `-` or `->`.
    Minus,
    /// Emit `*`.
    Star,
    /// Emit `/`.
    Slash,
    /// Emit `%`.
    Percent,
    /// Emit `=` or `==`.
    Equal,
    /// Emit `!=` or an error.
    Bang,
    /// Emit `<` or `<=`.
    Less,
    /// Emit `>` or `>=`.
    Greater,
    /// Emit `:`.
    Colon,
    /// Emit `,`.
    Comma,
    /// Emit `.`.
    Dot,
    /// Emit `(`.
    LParen,
    /// Emit `)`.
    RParen,
    /// Emit `[`.
    LBracket,
    /// Emit `]`.
    RBracket,
}

/// Byte classes used by the DFA transition tables.
#[repr(u8)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ByteClass {
    /// Horizontal space.
    Space,
    /// A hard tab character.
    Tab,
    /// A line break byte.
    Newline,
    /// `#`
    Hash,
    /// `"`
    Quote,
    /// ASCII decimal digit.
    Digit,
    /// `.`
    Dot,
    /// `=`
    Equal,
    /// `-`
    Minus,
    /// `!`
    Bang,
    /// `<`
    Less,
    /// `>`
    Greater,
    /// `+`
    Plus,
    /// `*`
    Star,
    /// `/`
    Slash,
    /// `%`
    Percent,
    /// `:`
    Colon,
    /// `,`
    Comma,
    /// `(`
    LParen,
    /// `)`
    RParen,
    /// `[`
    LBracket,
    /// `]`
    RBracket,
    /// `A-Z`, `a-z`, `_`
    IdentStart,
    /// Identifier continuation byte.
    IdentContinue,
    /// Non-ASCII identifier start.
    UnicodeStart,
    /// Non-ASCII identifier continuation.
    UnicodeContinue,
    /// End of input sentinel.
    Eof,
    /// Any other byte.
    Other,
}

impl ByteClass {
    /// Returns the number of byte classes in the DFA tables.
    pub const COUNT: usize = 28;
}

/// DFA states shared across token scanners.
#[repr(u8)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DfaState {
    /// Reject the current input byte.
    Reject,
    /// Continue scanning an identifier.
    Identifier,
    /// Continue scanning an integer literal.
    Integer,
    /// Expect the first fractional digit after a dot.
    FractionStart,
    /// Continue scanning a floating-point literal.
    Float,
    /// Continue scanning string contents.
    StringBody,
    /// Accept a closed string literal.
    StringDone,
    /// Reject an unterminated string.
    StringError,
}

/// Classified byte information used by the lexer.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ClassifiedByte {
    /// The byte class for DFA lookup.
    pub class: ByteClass,
    /// The UTF-8 width in bytes that should be consumed for this input unit.
    pub width: u8,
}

/// The 256-entry first-byte dispatch table.
pub const DISPATCH_TABLE: [TokenAction; 256] = build_dispatch_table();

/// Shared DFA transition table.
pub const DFA_TRANSITIONS: [[DfaState; ByteClass::COUNT]; 8] = build_dfa_table();

const fn build_dispatch_table() -> [TokenAction; 256] {
    let mut table = [TokenAction::Invalid; 256];
    let mut byte = 0usize;

    while byte < 256 {
        table[byte] = match byte as u8 {
            b' ' | b'\r' => TokenAction::SkipSpace,
            b'\t' => TokenAction::TabError,
            b'\n' => TokenAction::Newline,
            b'#' => TokenAction::Comment,
            b'"' => TokenAction::String,
            b'0'..=b'9' => TokenAction::Number,
            b'a'..=b'z' | b'A'..=b'Z' | b'_' => TokenAction::Identifier,
            b'+' => TokenAction::Plus,
            b'-' => TokenAction::Minus,
            b'*' => TokenAction::Star,
            b'/' => TokenAction::Slash,
            b'%' => TokenAction::Percent,
            b'=' => TokenAction::Equal,
            b'!' => TokenAction::Bang,
            b'<' => TokenAction::Less,
            b'>' => TokenAction::Greater,
            b':' => TokenAction::Colon,
            b',' => TokenAction::Comma,
            b'.' => TokenAction::Dot,
            b'(' => TokenAction::LParen,
            b')' => TokenAction::RParen,
            b'[' => TokenAction::LBracket,
            b']' => TokenAction::RBracket,
            0x80..=0xFF => TokenAction::UnicodeIdentifier,
            _ => TokenAction::Invalid,
        };
        byte += 1;
    }

    table
}

const fn build_dfa_table() -> [[DfaState; ByteClass::COUNT]; 8] {
    let mut table = [[DfaState::Reject; ByteClass::COUNT]; 8];

    table[DfaState::Identifier as usize][ByteClass::IdentStart as usize] = DfaState::Identifier;
    table[DfaState::Identifier as usize][ByteClass::IdentContinue as usize] = DfaState::Identifier;
    table[DfaState::Identifier as usize][ByteClass::UnicodeStart as usize] = DfaState::Identifier;
    table[DfaState::Identifier as usize][ByteClass::UnicodeContinue as usize] =
        DfaState::Identifier;
    table[DfaState::Identifier as usize][ByteClass::Digit as usize] = DfaState::Identifier;

    table[DfaState::Integer as usize][ByteClass::Digit as usize] = DfaState::Integer;
    table[DfaState::Integer as usize][ByteClass::Dot as usize] = DfaState::FractionStart;

    table[DfaState::FractionStart as usize][ByteClass::Digit as usize] = DfaState::Float;

    table[DfaState::Float as usize][ByteClass::Digit as usize] = DfaState::Float;

    let mut class = 0usize;
    while class < ByteClass::COUNT {
        table[DfaState::StringBody as usize][class] = DfaState::StringBody;
        class += 1;
    }
    table[DfaState::StringBody as usize][ByteClass::Quote as usize] = DfaState::StringDone;
    table[DfaState::StringBody as usize][ByteClass::Newline as usize] = DfaState::StringError;
    table[DfaState::StringBody as usize][ByteClass::Eof as usize] = DfaState::StringError;

    table
}

/// Returns the first-byte token action for `byte`.
#[inline]
#[must_use]
pub const fn dispatch(byte: u8) -> TokenAction {
    DISPATCH_TABLE[byte as usize]
}

/// Returns the next DFA state for the current `state` and `class`.
#[inline]
#[must_use]
pub const fn transition(state: DfaState, class: ByteClass) -> DfaState {
    DFA_TRANSITIONS[state as usize][class as usize]
}
