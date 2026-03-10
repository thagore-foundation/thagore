//! Core parser cursor and arena allocation utilities.

extern crate alloc;

use alloc::vec::Vec;
use bumpalo::collections::Vec as BumpVec;
use bumpalo::Bump;
use core::mem;
use thagore_ast::{
    Block, BlockRef, Expr, ExprRef, InferTypeExpr, InternedStr, LitExpr, Literal, NodeId, Span,
    TypeExpr, TypeExprRef,
};
use thagore_lexer::{Interner, Token, TokenKind};

use crate::error::{
    is_lexer_error_token, is_statement_boundary, span_from_lexer, Expectation, ParseError,
};

/// Hand-written recursive-descent parser for Thagore source.
pub struct Parser<'src, 'tok, 'ast> {
    pub(crate) tokens: &'tok [Token],
    pub(crate) cursor: usize,
    pub(crate) arena: &'ast Bump,
    pub(crate) interner: &'src Interner<'src>,
    pub(crate) symbols: BumpVec<'ast, &'src str>,
    pub(crate) errors: Vec<ParseError>,
    pub(crate) next_node_id: u32,
    pub(crate) statement_has_error: bool,
}

impl<'src, 'tok, 'ast> Parser<'src, 'tok, 'ast> {
    /// Creates a parser over a lexed token stream.
    #[must_use]
    pub fn new(tokens: &'tok [Token], arena: &'ast Bump, interner: &'src Interner<'src>) -> Self {
        Self {
            tokens,
            cursor: 0,
            arena,
            interner,
            symbols: BumpVec::new_in(arena),
            errors: Vec::new(),
            next_node_id: 0,
            statement_has_error: false,
        }
    }

    /// Parses a complete source file into top-level declarations.
    #[must_use]
    pub fn parse_program(&mut self) -> Vec<thagore_ast::Decl<'ast>> {
        let mut decls = Vec::new();
        self.skip_newlines();

        while !self.at(TokenKind::Eof) {
            if self.consume_lexer_error_at_cursor() {
                self.skip_newlines();
                continue;
            }

            self.begin_statement();
            match self.parse_decl() {
                Some(decl) => decls.push(decl),
                None => self.synchronize_statement(),
            }
            self.end_statement();
            self.skip_newlines();
        }

        decls
    }

    /// Returns all parser errors collected so far.
    #[must_use]
    pub fn errors(&self) -> &[ParseError] {
        &self.errors
    }

    /// Drains the collected parser errors.
    #[must_use]
    pub fn take_errors(&mut self) -> Vec<ParseError> {
        mem::take(&mut self.errors)
    }

    /// Resolves an AST interned string handle back to source text when the
    /// symbol was interned by this parser instance.
    #[must_use]
    pub fn resolve_symbol(&self, symbol: InternedStr) -> Option<&'src str> {
        self.symbols.get(symbol.as_u32() as usize).copied()
    }

    /// Returns a stable snapshot of all parser-local interned symbol texts.
    #[must_use]
    pub fn symbols_snapshot(&self) -> &[&'src str] {
        self.symbols.as_slice()
    }

    /// Returns the current token without consuming it.
    #[must_use]
    pub fn peek(&self) -> Token {
        self.tokens.get(self.cursor).copied().unwrap_or_default()
    }

    /// Returns the next token after the current one without consuming it.
    #[must_use]
    pub fn peek2(&self) -> Token {
        self.tokens
            .get(self.cursor + 1)
            .copied()
            .unwrap_or_default()
    }

    /// Consumes and returns the current token.
    pub fn advance(&mut self) -> Token {
        let token = self.peek();
        if self.cursor < self.tokens.len() {
            self.cursor += 1;
        }
        token
    }

    /// Consumes `kind` if it is the current token.
    pub fn match_kind(&mut self, kind: TokenKind) -> Option<Token> {
        if self.at(kind) {
            Some(self.advance())
        } else {
            None
        }
    }

    /// Consumes a required token or records a parse error.
    pub fn expect(&mut self, kind: TokenKind) -> Option<Token> {
        if self.at(kind) {
            return Some(self.advance());
        }

        let token = self.peek();
        self.emit_statement_error(ParseError::unexpected_token(
            token.kind,
            self.current_span(),
            Expectation::Token(kind),
        ));
        None
    }

    /// Consumes a required `INDENT` token or records a parse error.
    pub fn expect_indent(&mut self) -> Option<Token> {
        self.expect(TokenKind::Indent)
    }

    /// Consumes a required `DEDENT` token or records a parse error.
    pub fn expect_dedent(&mut self) -> Option<Token> {
        if self.at(TokenKind::Dedent) {
            return Some(self.advance());
        }

        self.emit_statement_error(ParseError::missing_dedent(self.current_span()));
        None
    }

    pub(crate) fn at(&self, kind: TokenKind) -> bool {
        self.peek().kind == kind
    }

    pub(crate) fn begin_statement(&mut self) {
        self.statement_has_error = false;
    }

    pub(crate) fn end_statement(&mut self) {
        self.statement_has_error = false;
    }

    pub(crate) fn current_span(&self) -> Span {
        span_from_lexer(self.peek().span)
    }

    pub(crate) fn span_of(&self, token: Token) -> Span {
        span_from_lexer(token.span)
    }

    pub(crate) fn new_node_id(&mut self) -> NodeId {
        let id = NodeId::new(self.next_node_id);
        self.next_node_id = self.next_node_id.saturating_add(1);
        id
    }

    pub(crate) fn alloc_expr(&self, expr: Expr<'ast>) -> ExprRef<'ast> {
        let arena = self.arena;
        arena.alloc(expr)
    }

    pub(crate) fn alloc_type(&self, ty: TypeExpr<'ast>) -> TypeExprRef<'ast> {
        let arena = self.arena;
        arena.alloc(ty)
    }

    pub(crate) fn alloc_block(&self, block: Block<'ast>) -> BlockRef<'ast> {
        let arena = self.arena;
        arena.alloc(block)
    }

    pub(crate) fn bump_vec<T>(&self) -> BumpVec<'ast, T> {
        BumpVec::new_in(self.arena)
    }

    pub(crate) fn skip_newlines(&mut self) {
        while self.at(TokenKind::Newline) {
            self.advance();
        }
    }

    pub(crate) fn skip_soft_layout(&mut self) {
        while matches!(
            self.peek().kind,
            TokenKind::Newline | TokenKind::Indent | TokenKind::Dedent
        ) {
            self.advance();
        }
    }

    pub(crate) fn consume_statement_terminator(&mut self) {
        if self.at(TokenKind::Newline) {
            self.skip_newlines();
        }
    }

    pub(crate) fn synchronize_statement(&mut self) {
        if self.at(TokenKind::Eof) {
            return;
        }

        if !is_statement_boundary(self.peek().kind) {
            self.advance();
        }

        while !is_statement_boundary(self.peek().kind) {
            self.advance();
        }

        if self.at(TokenKind::Newline) {
            self.skip_newlines();
        } else if self.at(TokenKind::Dedent) {
            self.advance();
        }
    }

    pub(crate) fn emit_statement_error(&mut self, error: ParseError) {
        if !self.statement_has_error {
            self.errors.push(error);
            self.statement_has_error = true;
        }
    }

    pub(crate) fn consume_lexer_error_at_cursor(&mut self) -> bool {
        let token = self.peek();
        if !is_lexer_error_token(token) {
            return false;
        }

        let message = token.error_message().unwrap_or("lexer error");
        self.emit_statement_error(ParseError::lexer_error(self.span_of(token), message));
        self.advance();
        while !is_statement_boundary(self.peek().kind) && !self.at(TokenKind::Eof) {
            self.advance();
        }
        if self.at(TokenKind::Newline) {
            self.advance();
        }
        true
    }

    pub(crate) fn token_text(&self, token: Token) -> Option<&'src str> {
        token.slice().and_then(|slice| self.interner.resolve(slice))
    }

    pub(crate) fn token_is_contextual(&self, token: Token, keyword: &str) -> bool {
        token.kind == TokenKind::Identifier && self.token_text(token) == Some(keyword)
    }

    pub(crate) fn at_contextual(&self, keyword: &str) -> bool {
        self.token_is_contextual(self.peek(), keyword)
    }

    pub(crate) fn match_contextual(&mut self, keyword: &str) -> Option<Token> {
        if self.at_contextual(keyword) {
            Some(self.advance())
        } else {
            None
        }
    }

    /// Interns arbitrary source text into the parser-local symbol table.
    pub fn intern_text(&mut self, text: &'src str) -> InternedStr {
        if let Some((index, _)) = self
            .symbols
            .iter()
            .enumerate()
            .find(|(_, item)| **item == text)
        {
            return InternedStr::new(index as u32);
        }

        self.symbols.push(text);
        InternedStr::new((self.symbols.len() - 1) as u32)
    }

    pub(crate) fn intern_token_symbol(&mut self, token: Token) -> InternedStr {
        let text = self.token_text(token).unwrap_or_else(|| match token.kind {
            TokenKind::I32 => "i32",
            TokenKind::F32 => "f32",
            TokenKind::Bool => "bool",
            TokenKind::Str => "str",
            _ => "__missing__",
        });
        self.intern_text(text)
    }

    pub(crate) fn parse_identifier_symbol(&mut self, expected: Expectation) -> InternedStr {
        let token = self.peek();
        if token_can_name_value(token.kind) {
            let token = self.advance();
            return self.intern_token_symbol(token);
        }

        self.emit_statement_error(ParseError::unexpected_token(
            token.kind,
            self.current_span(),
            expected,
        ));
        if !self.at(TokenKind::Eof) {
            self.advance();
        }
        self.intern_text("__error__")
    }

    pub(crate) fn parse_name_symbol(&mut self, expected: Expectation) -> InternedStr {
        let token = self.peek();
        if token_can_name_value(token.kind) || token.kind.is_builtin_type() {
            let token = self.advance();
            return self.intern_token_symbol(token);
        }

        self.emit_statement_error(ParseError::unexpected_token(
            token.kind,
            self.current_span(),
            expected,
        ));
        if !self.at(TokenKind::Eof) {
            self.advance();
        }
        self.intern_text("__error__")
    }

    pub(crate) fn synthetic_zero_expr(&mut self, span: Span) -> ExprRef<'ast> {
        let id = self.new_node_id();
        self.alloc_expr(Expr::Literal(LitExpr {
            id,
            span,
            literal: Literal::Int(0),
        }))
    }

    pub(crate) fn synthetic_infer_type(&mut self, span: Span) -> TypeExprRef<'ast> {
        let id = self.new_node_id();
        self.alloc_type(TypeExpr::Infer(InferTypeExpr { id, span }))
    }
}

fn token_can_name_value(kind: TokenKind) -> bool {
    matches!(kind, TokenKind::Identifier | TokenKind::From | TokenKind::Include)
}
