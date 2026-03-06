//! Type expression parsing.

use thagore_ast::{GenericTypeExpr, NamedTypeExpr, TypeExpr, TypeExprRef};
use thagore_lexer::TokenKind;

use crate::error::{is_expr_terminator, Expectation, ParseError};
use crate::parser::Parser;

impl<'src, 'tok, 'ast> Parser<'src, 'tok, 'ast> {
    pub(crate) fn parse_type_expr(&mut self) -> TypeExprRef<'ast> {
        let token = self.peek();
        let start = self.current_span();

        if token.kind == TokenKind::Identifier && self.token_text(token) == Some("_") {
            let token = self.advance();
            let id = self.new_node_id();
            return self.alloc_type(TypeExpr::Infer(thagore_ast::InferTypeExpr {
                id,
                span: self.span_of(token),
            }));
        }

        let name = match token.kind {
            TokenKind::Identifier
            | TokenKind::I32
            | TokenKind::F32
            | TokenKind::Bool
            | TokenKind::Str => {
                let token = self.advance();
                self.intern_token_symbol(token)
            }
            _ => {
                self.emit_statement_error(ParseError::unexpected_token(
                    token.kind,
                    start,
                    Expectation::TypeExpr,
                ));
                return self.synthetic_infer_type(start);
            }
        };

        let closing_delimiter = if self.match_kind(TokenKind::Lt).is_some() {
            TokenKind::Gt
        } else if self.match_kind(TokenKind::LBracket).is_some() {
            TokenKind::RBracket
        } else {
            let span = self.span_of(token);
            let id = self.new_node_id();
            return self.alloc_type(TypeExpr::Named(NamedTypeExpr { id, span, name }));
        };

        let mut args = self.bump_vec();
        while !self.at(closing_delimiter) && !is_expr_terminator(self.peek().kind) {
            args.push(self.parse_type_expr());
            if self.match_kind(TokenKind::Comma).is_none() {
                break;
            }
        }

        let end = if let Some(delimiter) = self.match_kind(closing_delimiter) {
            self.span_of(delimiter)
        } else {
            self.emit_statement_error(ParseError::missing_token(
                self.current_span(),
                closing_delimiter,
            ));
            self.current_span()
        };

        let id = self.new_node_id();
        self.alloc_type(TypeExpr::Generic(GenericTypeExpr {
            id,
            span: start.join(end),
            name,
            args: args.into_bump_slice(),
        }))
    }
}
