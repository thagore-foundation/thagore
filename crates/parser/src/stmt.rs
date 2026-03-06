//! Statement and block parsing.

use thagore_ast::{Block, ExprStmt, ForStmt, IfStmt, ReturnStmt, Stmt, WhileStmt};
use thagore_lexer::TokenKind;

use crate::error::{Expectation, ParseError};
use crate::parser::Parser;

impl<'src, 'tok, 'ast> Parser<'src, 'tok, 'ast> {
    pub(crate) fn parse_stmt(&mut self) -> Option<Stmt<'ast>> {
        if self.consume_lexer_error_at_cursor() {
            return None;
        }

        match self.peek().kind {
            TokenKind::Let => Some(Stmt::Let(self.parse_let_decl())),
            TokenKind::If => Some(Stmt::If(self.parse_if_stmt())),
            TokenKind::While => Some(Stmt::While(self.parse_while_stmt())),
            TokenKind::For => Some(Stmt::For(self.parse_for_stmt())),
            TokenKind::Return => Some(Stmt::Return(self.parse_return_stmt())),
            _ => {
                if !self.is_expression_start() {
                    self.emit_statement_error(ParseError::unexpected_token(
                        self.peek().kind,
                        self.current_span(),
                        Expectation::Statement,
                    ));
                    return None;
                }
                Some(Stmt::Expr(self.parse_expr_stmt()))
            }
        }
    }

    pub(crate) fn parse_block(&mut self) -> thagore_ast::BlockRef<'ast> {
        let block_start = self.current_span();
        self.skip_newlines();

        if self.expect_indent().is_none() {
            let id = self.new_node_id();
            return self.alloc_block(Block {
                id,
                span: block_start,
                statements: self.bump_vec::<Stmt<'ast>>().into_bump_slice(),
            });
        }

        let mut statements = self.bump_vec();
        let mut end_span = block_start;

        loop {
            self.skip_newlines();
            if self.at(TokenKind::Dedent) || self.at(TokenKind::Eof) {
                break;
            }

            self.begin_statement();
            if let Some(stmt) = self.parse_stmt() {
                end_span = stmt.span();
                statements.push(stmt);
            } else {
                self.synchronize_statement();
            }
            self.end_statement();
            self.consume_statement_terminator();
        }

        if let Some(dedent) = self.match_kind(TokenKind::Dedent) {
            end_span = end_span.join(self.span_of(dedent));
        } else if !self.at(TokenKind::Eof) {
            self.emit_statement_error(ParseError::missing_dedent(self.current_span()));
        }

        let id = self.new_node_id();
        self.alloc_block(Block {
            id,
            span: block_start.join(end_span),
            statements: statements.into_bump_slice(),
        })
    }

    pub(crate) fn parse_if_stmt(&mut self) -> IfStmt<'ast> {
        let if_token = self.advance();
        let condition = self.parse_condition_expr();
        self.expect_block_colon();
        let then_block = self.parse_block();

        let else_block = if self.match_kind(TokenKind::Else).is_some() {
            self.expect_block_colon();
            Some(self.parse_block())
        } else {
            None
        };

        let end = else_block
            .map(|block| block.span)
            .unwrap_or(then_block.span);
        IfStmt {
            id: self.new_node_id(),
            span: self.span_of(if_token).join(end),
            condition,
            then_block,
            else_block,
        }
    }

    pub(crate) fn parse_while_stmt(&mut self) -> WhileStmt<'ast> {
        let while_token = self.advance();
        let condition = self.parse_condition_expr();
        self.expect_block_colon();
        let body = self.parse_block();
        WhileStmt {
            id: self.new_node_id(),
            span: self.span_of(while_token).join(body.span),
            condition,
            body,
        }
    }

    pub(crate) fn parse_for_stmt(&mut self) -> ForStmt<'ast> {
        let for_token = self.advance();
        let binding = self.parse_identifier_symbol(Expectation::Identifier);
        if self.match_contextual("in").is_none() {
            self.emit_statement_error(ParseError::unexpected_token(
                self.peek().kind,
                self.current_span(),
                Expectation::Token(TokenKind::Identifier),
            ));
        }
        let iterator = self.parse_expr(0);
        self.expect_block_colon();
        let body = self.parse_block();
        ForStmt {
            id: self.new_node_id(),
            span: self.span_of(for_token).join(body.span),
            binding,
            iterator,
            body,
        }
    }

    pub(crate) fn parse_return_stmt(&mut self) -> ReturnStmt<'ast> {
        let return_token = self.advance();
        let value =
            if self.at(TokenKind::Newline) || self.at(TokenKind::Dedent) || self.at(TokenKind::Eof)
            {
                None
            } else {
                Some(self.parse_expr(0))
            };
        let end = value
            .map(|expr| expr.span())
            .unwrap_or(self.span_of(return_token));
        ReturnStmt {
            id: self.new_node_id(),
            span: self.span_of(return_token).join(end),
            value,
        }
    }

    pub(crate) fn parse_expr_stmt(&mut self) -> ExprStmt<'ast> {
        let expr = self.parse_expr(0);
        ExprStmt {
            id: self.new_node_id(),
            span: expr.span(),
            expr,
        }
    }

    pub(crate) fn parse_condition_expr(&mut self) -> thagore_ast::ExprRef<'ast> {
        let had_lparen = self.match_kind(TokenKind::LParen).is_some();
        if !had_lparen {
            self.emit_statement_error(ParseError::missing_condition_lparen(self.current_span()));
        }

        let condition = self.parse_expr(0);
        if self.match_kind(TokenKind::RParen).is_none() {
            self.emit_statement_error(ParseError::missing_condition_rparen(self.current_span()));
        }

        condition
    }

    pub(crate) fn expect_block_colon(&mut self) {
        if self.match_kind(TokenKind::Colon).is_none() {
            self.emit_statement_error(ParseError::missing_block_colon(self.current_span()));
        }
    }

    pub(crate) fn is_expression_start(&self) -> bool {
        matches!(
            self.peek().kind,
            TokenKind::Identifier
                | TokenKind::Integer
                | TokenKind::Float
                | TokenKind::String
                | TokenKind::Minus
                | TokenKind::LParen
        )
    }
}
