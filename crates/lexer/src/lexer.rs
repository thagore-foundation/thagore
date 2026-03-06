//! Main lexer implementation for the Thagore language.

extern crate alloc;

use alloc::collections::VecDeque;
use alloc::vec::Vec;
use bumpalo::Bump;
use bumpalo::collections::Vec as BumpVec;
use phf::phf_map;

use crate::error::{
    INCONSISTENT_DEDENT_ERROR, INVALID_BANG_ERROR, INVALID_INDENTATION_ERROR, INVALID_TOKEN_ERROR,
    LexError, TAB_INDENTATION_ERROR, UNTERMINATED_STRING_ERROR,
};
use crate::intern::Interner;
use crate::table::{ByteClass, ClassifiedByte, DfaState, TokenAction, dispatch, transition};
use crate::token::{SliceRef, Span, Token, TokenKind};

static KEYWORDS: phf::Map<&'static str, TokenKind> = phf_map! {
    "let" => TokenKind::Let,
    "func" => TokenKind::Func,
    "if" => TokenKind::If,
    "else" => TokenKind::Else,
    "while" => TokenKind::While,
    "for" => TokenKind::For,
    "return" => TokenKind::Return,
    "const" => TokenKind::Const,
    "from" => TokenKind::From,
    "import" => TokenKind::Import,
    "include" => TokenKind::Include,
    "extern" => TokenKind::Extern,
    "struct" => TokenKind::Struct,
    "impl" => TokenKind::Impl,
    "intent" => TokenKind::Intent,
    "flow" => TokenKind::Flow,
    "i32" => TokenKind::I32,
    "f32" => TokenKind::F32,
    "bool" => TokenKind::Bool,
    "str" => TokenKind::Str,
};

/// Arena-backed token stream produced by `Lexer::lex_all_in`.
pub type TokenStream<'arena> = BumpVec<'arena, Token>;

/// Streaming lexer for Thagore source text.
pub struct Lexer<'src> {
    source: &'src str,
    bytes: &'src [u8],
    offset: usize,
    line_start: bool,
    pending: VecDeque<Token>,
    indent_stack: Vec<u32>,
    interner: Interner<'src>,
    reached_eof: bool,
    emitted_eof: bool,
}

impl<'src> Lexer<'src> {
    /// Creates a new lexer over `source`.
    #[must_use]
    pub fn new(source: &'src str) -> Self {
        let mut indent_stack = Vec::with_capacity(8);
        indent_stack.push(0);

        Self {
            source,
            bytes: source.as_bytes(),
            offset: 0,
            line_start: true,
            pending: VecDeque::new(),
            indent_stack,
            interner: Interner::new(source),
            reached_eof: false,
            emitted_eof: false,
        }
    }

    /// Returns the backing source.
    #[must_use]
    pub const fn source(&self) -> &'src str {
        self.source
    }

    /// Returns the zero-copy interner associated with the lexer.
    #[must_use]
    pub const fn interner(&self) -> &Interner<'src> {
        &self.interner
    }

    /// Lexes the entire input into a bump-allocated token stream.
    #[must_use]
    pub fn lex_all_in<'arena>(&mut self, arena: &'arena Bump) -> TokenStream<'arena> {
        let mut tokens = BumpVec::new_in(arena);

        loop {
            let token = self.next_token();
            let done = token.kind == TokenKind::Eof;
            tokens.push(token);
            if done {
                break;
            }
        }

        tokens
    }

    /// Returns the next token from the stream.
    #[must_use]
    pub fn next_token(&mut self) -> Token {
        loop {
            if let Some(token) = self.pending.pop_front() {
                return token;
            }

            if self.line_start && !self.reached_eof {
                self.prepare_logical_line();
                if let Some(token) = self.pending.pop_front() {
                    return token;
                }
            }

            if self.offset >= self.bytes.len() {
                return self.finish_eof();
            }

            let byte = self.bytes[self.offset];
            match dispatch(byte) {
                TokenAction::SkipSpace => {
                    self.offset += 1;
                }
                TokenAction::TabError => return self.consume_tab_error(),
                TokenAction::Newline => return self.consume_newline(),
                TokenAction::Comment => {
                    self.skip_comment();
                }
                TokenAction::Identifier | TokenAction::UnicodeIdentifier => {
                    return self.scan_identifier_or_keyword();
                }
                TokenAction::Number => return self.scan_number(),
                TokenAction::String => return self.scan_string(),
                TokenAction::Plus => return self.single_byte_token(TokenKind::Plus),
                TokenAction::Minus => return self.scan_minus_or_arrow(),
                TokenAction::Star => return self.single_byte_token(TokenKind::Star),
                TokenAction::Slash => return self.single_byte_token(TokenKind::Slash),
                TokenAction::Percent => return self.single_byte_token(TokenKind::Percent),
                TokenAction::Equal => return self.scan_equal_family(),
                TokenAction::Bang => return self.scan_bang_family(),
                TokenAction::Less => return self.scan_less_family(),
                TokenAction::Greater => return self.scan_greater_family(),
                TokenAction::Colon => return self.single_byte_token(TokenKind::Colon),
                TokenAction::Comma => return self.single_byte_token(TokenKind::Comma),
                TokenAction::Dot => return self.single_byte_token(TokenKind::Dot),
                TokenAction::LParen => return self.single_byte_token(TokenKind::LParen),
                TokenAction::RParen => return self.single_byte_token(TokenKind::RParen),
                TokenAction::LBracket => return self.single_byte_token(TokenKind::LBracket),
                TokenAction::RBracket => return self.single_byte_token(TokenKind::RBracket),
                TokenAction::Invalid => return self.consume_invalid_byte(),
            }
        }
    }

    fn prepare_logical_line(&mut self) {
        loop {
            if self.offset >= self.bytes.len() {
                self.reached_eof = true;
                return;
            }

            let line_offset = self.offset;
            let mut cursor = self.offset;
            let mut spaces = 0u32;
            let mut saw_tab = false;

            while let Some(byte) = self.bytes.get(cursor).copied() {
                match byte {
                    b' ' => {
                        spaces += 1;
                        cursor += 1;
                    }
                    b'\t' => {
                        saw_tab = true;
                        cursor += 1;
                        break;
                    }
                    _ => break,
                }
            }

            if saw_tab {
                self.offset = cursor;
                self.line_start = false;
                self.pending.push_back(
                    LexError::new(
                        Span::new(line_offset as u32, cursor as u32),
                        TAB_INDENTATION_ERROR,
                    )
                    .to_token(),
                );
                return;
            }

            match self.bytes.get(cursor).copied() {
                Some(b'\r') => {
                    cursor += 1;
                    if self.bytes.get(cursor) == Some(&b'\n') {
                        cursor += 1;
                    }
                    self.offset = cursor;
                    self.line_start = true;
                }
                Some(b'\n') => {
                    self.offset = cursor + 1;
                    self.line_start = true;
                }
                Some(b'#') => {
                    self.offset = cursor;
                    self.skip_comment();
                    if self.offset < self.bytes.len() {
                        if self.bytes[self.offset] == b'\r' {
                            self.offset += 1;
                            if self.bytes.get(self.offset) == Some(&b'\n') {
                                self.offset += 1;
                            }
                        } else if self.bytes[self.offset] == b'\n' {
                            self.offset += 1;
                        }
                    }
                    self.line_start = true;
                }
                Some(_) => {
                    self.offset = cursor;
                    self.apply_indentation(line_offset, spaces, cursor as u32);
                    self.line_start = false;
                    return;
                }
                None => {
                    self.reached_eof = true;
                    self.offset = cursor;
                    return;
                }
            }
        }
    }

    fn apply_indentation(&mut self, line_offset: usize, spaces: u32, cursor: u32) {
        let mut effective_spaces = spaces;
        if spaces % 2 != 0 {
            self.pending.push_back(
                LexError::new(
                    Span::new(line_offset as u32, cursor),
                    INVALID_INDENTATION_ERROR,
                )
                .to_token(),
            );
            effective_spaces -= 1;
        }

        let current = *self.indent_stack.last().unwrap_or(&0);
        if effective_spaces > current {
            self.indent_stack.push(effective_spaces);
            self.pending
                .push_back(Token::new(TokenKind::Indent, Span::new(cursor, cursor)));
            return;
        }

        if effective_spaces < current {
            while let Some(&top) = self.indent_stack.last() {
                if top <= effective_spaces {
                    break;
                }
                self.indent_stack.pop();
                self.pending
                    .push_back(Token::new(TokenKind::Dedent, Span::new(cursor, cursor)));
            }

            if self.indent_stack.last().copied().unwrap_or(0) != effective_spaces {
                self.pending.push_back(
                    LexError::new(
                        Span::new(line_offset as u32, cursor),
                        INCONSISTENT_DEDENT_ERROR,
                    )
                    .to_token(),
                );
                self.indent_stack.push(effective_spaces);
            }
        }
    }

    fn finish_eof(&mut self) -> Token {
        if !self.reached_eof {
            self.reached_eof = true;
        }

        if let Some(token) = self.pending.pop_front() {
            return token;
        }

        if self.indent_stack.len() > 1 {
            self.indent_stack.pop();
            return Token::new(
                TokenKind::Dedent,
                Span::new(self.offset as u32, self.offset as u32),
            );
        }

        if self.emitted_eof {
            return Token::new(
                TokenKind::Eof,
                Span::new(self.offset as u32, self.offset as u32),
            );
        }

        self.emitted_eof = true;
        Token::new(
            TokenKind::Eof,
            Span::new(self.offset as u32, self.offset as u32),
        )
    }

    fn consume_newline(&mut self) -> Token {
        let start = self.offset;
        if self.bytes[self.offset] == b'\r' {
            self.offset += 1;
            if self.bytes.get(self.offset) == Some(&b'\n') {
                self.offset += 1;
            }
        } else {
            self.offset += 1;
        }
        self.line_start = true;
        Token::new(
            TokenKind::Newline,
            Span::new(start as u32, self.offset as u32),
        )
    }

    fn consume_tab_error(&mut self) -> Token {
        let start = self.offset;
        self.offset += 1;
        self.line_start = false;
        LexError::new(
            Span::new(start as u32, self.offset as u32),
            TAB_INDENTATION_ERROR,
        )
        .to_token()
    }

    fn consume_invalid_byte(&mut self) -> Token {
        let start = self.offset;
        let width = self.current_width().max(1);
        self.offset += width;
        self.line_start = false;
        LexError::new(
            Span::new(start as u32, self.offset as u32),
            INVALID_TOKEN_ERROR,
        )
        .to_token()
    }

    fn single_byte_token(&mut self, kind: TokenKind) -> Token {
        let start = self.offset;
        self.offset += 1;
        self.line_start = false;
        Token::new(kind, Span::new(start as u32, self.offset as u32))
    }

    fn scan_minus_or_arrow(&mut self) -> Token {
        let start = self.offset;
        self.offset += 1;
        self.line_start = false;

        if self.bytes.get(self.offset) == Some(&b'>') {
            self.offset += 1;
            return Token::new(
                TokenKind::Arrow,
                Span::new(start as u32, self.offset as u32),
            );
        }

        Token::new(
            TokenKind::Minus,
            Span::new(start as u32, self.offset as u32),
        )
    }

    fn scan_equal_family(&mut self) -> Token {
        let start = self.offset;
        self.offset += 1;
        self.line_start = false;

        let kind = if self.bytes.get(self.offset) == Some(&b'=') {
            self.offset += 1;
            TokenKind::EqEq
        } else {
            TokenKind::Assign
        };

        Token::new(kind, Span::new(start as u32, self.offset as u32))
    }

    fn scan_bang_family(&mut self) -> Token {
        let start = self.offset;
        self.offset += 1;
        self.line_start = false;

        if self.bytes.get(self.offset) == Some(&b'=') {
            self.offset += 1;
            return Token::new(
                TokenKind::BangEq,
                Span::new(start as u32, self.offset as u32),
            );
        }

        LexError::new(
            Span::new(start as u32, self.offset as u32),
            INVALID_BANG_ERROR,
        )
        .to_token()
    }

    fn scan_less_family(&mut self) -> Token {
        let start = self.offset;
        self.offset += 1;
        self.line_start = false;

        let kind = if self.bytes.get(self.offset) == Some(&b'=') {
            self.offset += 1;
            TokenKind::LtEq
        } else {
            TokenKind::Lt
        };

        Token::new(kind, Span::new(start as u32, self.offset as u32))
    }

    fn scan_greater_family(&mut self) -> Token {
        let start = self.offset;
        self.offset += 1;
        self.line_start = false;

        let kind = if self.bytes.get(self.offset) == Some(&b'=') {
            self.offset += 1;
            TokenKind::GtEq
        } else {
            TokenKind::Gt
        };

        Token::new(kind, Span::new(start as u32, self.offset as u32))
    }

    fn scan_identifier_or_keyword(&mut self) -> Token {
        let start = self.offset;

        loop {
            let classified = self.classify_identifier_at(self.offset);
            let next = transition(DfaState::Identifier, classified.class);
            if next == DfaState::Reject {
                break;
            }

            self.offset += classified.width as usize;
        }

        self.line_start = false;
        let span = Span::new(start as u32, self.offset as u32);
        let symbol = self.interner.intern(span);
        if let Some(text) = self.interner.resolve(symbol) {
            if let Some(kind) = KEYWORDS.get(text).copied() {
                return Token::with_slice(kind, span, symbol);
            }
        }

        Token::with_slice(TokenKind::Identifier, span, symbol)
    }

    fn scan_number(&mut self) -> Token {
        let start = self.offset;
        let mut state = DfaState::Integer;

        loop {
            let classified = self.classify_number_at(self.offset);
            let next = transition(state, classified.class);
            if next == DfaState::Reject {
                break;
            }

            self.offset += classified.width as usize;
            state = next;
        }

        self.line_start = false;
        let span = Span::new(start as u32, self.offset as u32);
        let kind = if state == DfaState::Float {
            TokenKind::Float
        } else {
            TokenKind::Integer
        };

        Token::with_slice(kind, span, SliceRef::from_span(span))
    }

    fn scan_string(&mut self) -> Token {
        let quote_offset = self.offset;
        self.offset += 1;
        let content_start = self.offset;

        while self.offset < self.bytes.len() {
            match self.bytes[self.offset] {
                b'\\' => {
                    let escape_width = self.current_width_at(self.offset).max(1);
                    self.offset += escape_width;
                    if self.offset >= self.bytes.len()
                        || matches!(self.bytes[self.offset], b'\n' | b'\r')
                    {
                        break;
                    }
                    self.offset += self.current_width_at(self.offset).max(1);
                }
                b'"' => {
                    let content_end = self.offset;
                    self.offset += 1;
                    self.line_start = false;
                    let span = Span::new(quote_offset as u32, self.offset as u32);
                    let slice =
                        SliceRef::new(content_start as u32, (content_end - content_start) as u32);
                    return Token::with_slice(TokenKind::String, span, slice);
                }
                b'\n' | b'\r' => break,
                _ => {
                    let classified = self.classify_string_at(self.offset);
                    self.offset += classified.width as usize;
                }
            }
        }

        self.line_start = false;
        LexError::new(
            Span::new(quote_offset as u32, self.offset as u32),
            UNTERMINATED_STRING_ERROR,
        )
        .to_token()
    }

    fn skip_comment(&mut self) {
        while self.offset < self.bytes.len() {
            match self.bytes[self.offset] {
                b'\n' | b'\r' => break,
                _ => self.offset += self.current_width().max(1),
            }
        }
    }

    fn classify_identifier_at(&self, offset: usize) -> ClassifiedByte {
        if offset >= self.bytes.len() {
            return ClassifiedByte {
                class: ByteClass::Eof,
                width: 0,
            };
        }

        let byte = self.bytes[offset];
        match byte {
            b'a'..=b'z' | b'A'..=b'Z' | b'_' => ClassifiedByte {
                class: ByteClass::IdentStart,
                width: 1,
            },
            b'0'..=b'9' => ClassifiedByte {
                class: ByteClass::Digit,
                width: 1,
            },
            0x80..=0xFF => self.classify_unicode_identifier(offset),
            _ => ClassifiedByte {
                class: ByteClass::Other,
                width: 1,
            },
        }
    }

    fn classify_number_at(&self, offset: usize) -> ClassifiedByte {
        if offset >= self.bytes.len() {
            return ClassifiedByte {
                class: ByteClass::Eof,
                width: 0,
            };
        }

        let byte = self.bytes[offset];
        match byte {
            b'0'..=b'9' => ClassifiedByte {
                class: ByteClass::Digit,
                width: 1,
            },
            b'.' if matches!(self.bytes.get(offset + 1), Some(b'0'..=b'9')) => ClassifiedByte {
                class: ByteClass::Dot,
                width: 1,
            },
            _ => ClassifiedByte {
                class: ByteClass::Other,
                width: 1,
            },
        }
    }

    fn classify_string_at(&self, offset: usize) -> ClassifiedByte {
        if offset >= self.bytes.len() {
            return ClassifiedByte {
                class: ByteClass::Eof,
                width: 0,
            };
        }

        let byte = self.bytes[offset];
        match byte {
            b'"' => ClassifiedByte {
                class: ByteClass::Quote,
                width: 1,
            },
            b'\n' | b'\r' => ClassifiedByte {
                class: ByteClass::Newline,
                width: 1,
            },
            0x80..=0xFF => ClassifiedByte {
                class: ByteClass::Other,
                width: self.current_width_at(offset).max(1) as u8,
            },
            _ => ClassifiedByte {
                class: ByteClass::Other,
                width: 1,
            },
        }
    }

    fn classify_unicode_identifier(&self, offset: usize) -> ClassifiedByte {
        if let Some(ch) = self.source[offset..].chars().next() {
            let class = if ch == '_' || ch.is_alphabetic() {
                ByteClass::UnicodeStart
            } else if ch.is_alphanumeric() {
                ByteClass::UnicodeContinue
            } else {
                ByteClass::Other
            };

            ClassifiedByte {
                class,
                width: ch.len_utf8() as u8,
            }
        } else {
            ClassifiedByte {
                class: ByteClass::Eof,
                width: 0,
            }
        }
    }

    fn current_width(&self) -> usize {
        self.current_width_at(self.offset)
    }

    fn current_width_at(&self, offset: usize) -> usize {
        self.source[offset..]
            .chars()
            .next()
            .map_or(0, char::len_utf8)
    }
}
