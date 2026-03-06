//! Pratt expression parsing.

use thagore_ast::{
    AssignExpr, BinOp, BinaryExpr, CallExpr, Expr, ExprRef, FieldAccessExpr, IdentExpr, IndexExpr,
    LitExpr, Literal, UnaryExpr, UnaryOp,
};
use thagore_lexer::TokenKind;

use crate::error::{is_expr_terminator, Expectation, ParseError};
use crate::parser::Parser;

/// Pratt binding powers for infix operators.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct BindingPower {
    /// Left binding power.
    pub left: u8,
    /// Right binding power.
    pub right: u8,
}

impl BindingPower {
    const fn left_assoc(level: u8) -> Self {
        Self {
            left: level,
            right: level + 1,
        }
    }

    const fn right_assoc(level: u8) -> Self {
        Self {
            left: level,
            right: level,
        }
    }
}

/// Static precedence table for symbolic infix operators.
pub static INFIX_BINDING_TABLE: &[(TokenKind, BindingPower)] = &[
    (TokenKind::Assign, BindingPower::right_assoc(1)),
    (TokenKind::EqEq, BindingPower::left_assoc(4)),
    (TokenKind::BangEq, BindingPower::left_assoc(4)),
    (TokenKind::Lt, BindingPower::left_assoc(5)),
    (TokenKind::Gt, BindingPower::left_assoc(5)),
    (TokenKind::LtEq, BindingPower::left_assoc(5)),
    (TokenKind::GtEq, BindingPower::left_assoc(5)),
    (TokenKind::Plus, BindingPower::left_assoc(6)),
    (TokenKind::Minus, BindingPower::left_assoc(6)),
    (TokenKind::Star, BindingPower::left_assoc(7)),
    (TokenKind::Slash, BindingPower::left_assoc(7)),
    (TokenKind::Percent, BindingPower::left_assoc(7)),
    (TokenKind::Dot, BindingPower::left_assoc(10)),
    (TokenKind::LBracket, BindingPower::left_assoc(10)),
    (TokenKind::LParen, BindingPower::left_assoc(10)),
];

const PREFIX_NOT_BP: u8 = 4;
const PREFIX_NEG_BP: u8 = 9;
const POSTFIX_BP: u8 = 10;
const CONTEXTUAL_OR_BP: BindingPower = BindingPower::left_assoc(2);
const CONTEXTUAL_AND_BP: BindingPower = BindingPower::left_assoc(3);

impl<'src, 'tok, 'ast> Parser<'src, 'tok, 'ast> {
    pub(crate) fn parse_expr(&mut self, min_bp: u8) -> ExprRef<'ast> {
        let mut lhs = self.parse_prefix();

        loop {
            if is_expr_terminator(self.peek().kind) {
                break;
            }

            if POSTFIX_BP >= min_bp && self.is_postfix_start() {
                lhs = self.parse_postfix(lhs);
                continue;
            }

            let Some((binding, op_kind)) = self.current_infix_operator() else {
                break;
            };
            if binding.left < min_bp {
                break;
            }

            let op_token = self.advance();
            let rhs = self.parse_expr(binding.right);
            let span = lhs.span().join(rhs.span());
            lhs = match op_kind {
                InfixOperator::Assign => {
                    let id = self.new_node_id();
                    self.alloc_expr(Expr::Assign(AssignExpr {
                        id,
                        span,
                        target: lhs,
                        value: rhs,
                    }))
                }
                InfixOperator::Binary(op) => {
                    let id = self.new_node_id();
                    self.alloc_expr(Expr::Binary(BinaryExpr {
                        id,
                        span,
                        left: lhs,
                        op,
                        right: rhs,
                    }))
                }
                InfixOperator::Postfix => {
                    self.emit_statement_error(ParseError::unexpected_token(
                        op_token.kind,
                        self.span_of(op_token),
                        Expectation::Expression,
                    ));
                    lhs
                }
            };
        }

        lhs
    }

    fn parse_prefix(&mut self) -> ExprRef<'ast> {
        if self.consume_lexer_error_at_cursor() {
            return self.synthetic_zero_expr(self.current_span());
        }

        let token = self.peek();
        let span = self.current_span();
        match token.kind {
            TokenKind::Identifier => self.parse_identifier_like_prefix(),
            TokenKind::Integer => {
                let token = self.advance();
                let value = self
                    .token_text(token)
                    .and_then(|text| text.parse::<i64>().ok())
                    .unwrap_or(0);
                let id = self.new_node_id();
                self.alloc_expr(Expr::Literal(LitExpr {
                    id,
                    span: self.span_of(token),
                    literal: Literal::Int(value),
                }))
            }
            TokenKind::Float => {
                let token = self.advance();
                let value = self
                    .token_text(token)
                    .and_then(|text| text.parse::<f64>().ok())
                    .unwrap_or(0.0);
                let id = self.new_node_id();
                self.alloc_expr(Expr::Literal(LitExpr {
                    id,
                    span: self.span_of(token),
                    literal: Literal::Float(value),
                }))
            }
            TokenKind::String => {
                let token = self.advance();
                let symbol = self.intern_token_symbol(token);
                let id = self.new_node_id();
                self.alloc_expr(Expr::Literal(LitExpr {
                    id,
                    span: self.span_of(token),
                    literal: Literal::Str(symbol),
                }))
            }
            TokenKind::Minus => {
                let token = self.advance();
                let operand = self.parse_expr(PREFIX_NEG_BP);
                let span = self.span_of(token).join(operand.span());
                let id = self.new_node_id();
                self.alloc_expr(Expr::Unary(UnaryExpr {
                    id,
                    span,
                    op: UnaryOp::Neg,
                    operand,
                }))
            }
            TokenKind::LParen => {
                let lparen = self.advance();
                let expr = self.parse_expr(0);
                if self.match_kind(TokenKind::RParen).is_none() {
                    self.emit_statement_error(ParseError::missing_token(
                        self.current_span(),
                        TokenKind::RParen,
                    ));
                }
                let _span = self.span_of(lparen).join(expr.span());
                expr
            }
            _ => {
                self.emit_statement_error(ParseError::expected_expression(span, token.kind));
                if !self.at(TokenKind::Eof) {
                    self.advance();
                }
                self.synthetic_zero_expr(span)
            }
        }
    }

    fn parse_identifier_like_prefix(&mut self) -> ExprRef<'ast> {
        let token = self.peek();
        let is_true = self.token_text(token) == Some("true");
        let is_false = self.token_text(token) == Some("false");
        let is_not = self.token_text(token) == Some("not");
        if is_true || is_false {
            let token = self.advance();
            let id = self.new_node_id();
            return self.alloc_expr(Expr::Literal(LitExpr {
                id,
                span: self.span_of(token),
                literal: Literal::Bool(is_true),
            }));
        }
        if is_not {
            let token = self.advance();
            let operand = self.parse_expr(PREFIX_NOT_BP);
            let span = self.span_of(token).join(operand.span());
            let id = self.new_node_id();
            return self.alloc_expr(Expr::Unary(UnaryExpr {
                id,
                span,
                op: UnaryOp::Not,
                operand,
            }));
        }

        let token = self.advance();
        let name = self.intern_token_symbol(token);
        let id = self.new_node_id();
        self.alloc_expr(Expr::Ident(IdentExpr {
            id,
            span: self.span_of(token),
            name,
        }))
    }

    fn is_postfix_start(&self) -> bool {
        matches!(
            self.peek().kind,
            TokenKind::Dot | TokenKind::LBracket | TokenKind::LParen
        )
    }

    fn parse_postfix(&mut self, lhs: ExprRef<'ast>) -> ExprRef<'ast> {
        match self.peek().kind {
            TokenKind::Dot => {
                let dot = self.advance();
                let field_token = self.peek();
                let field = self.parse_identifier_symbol(Expectation::Identifier);
                let span = lhs
                    .span()
                    .join(self.span_of(dot))
                    .join(self.span_of(field_token));
                let id = self.new_node_id();
                self.alloc_expr(Expr::FieldAccess(FieldAccessExpr {
                    id,
                    span,
                    object: lhs,
                    field,
                }))
            }
            TokenKind::LBracket => {
                self.advance();
                let index = self.parse_expr(0);
                let end = if let Some(rbracket) = self.match_kind(TokenKind::RBracket) {
                    self.span_of(rbracket)
                } else {
                    self.emit_statement_error(ParseError::missing_token(
                        self.current_span(),
                        TokenKind::RBracket,
                    ));
                    index.span()
                };
                let id = self.new_node_id();
                self.alloc_expr(Expr::Index(IndexExpr {
                    id,
                    span: lhs.span().join(end),
                    object: lhs,
                    index,
                }))
            }
            TokenKind::LParen => {
                self.advance();
                let mut args = self.bump_vec();
                while !self.at(TokenKind::RParen) && !self.at(TokenKind::Eof) {
                    args.push(self.parse_expr(0));
                    if self.match_kind(TokenKind::Comma).is_none() {
                        break;
                    }
                }
                let end = if let Some(rparen) = self.match_kind(TokenKind::RParen) {
                    self.span_of(rparen)
                } else {
                    self.emit_statement_error(ParseError::missing_token(
                        self.current_span(),
                        TokenKind::RParen,
                    ));
                    lhs.span()
                };
                let id = self.new_node_id();
                self.alloc_expr(Expr::Call(CallExpr {
                    id,
                    span: lhs.span().join(end),
                    callee: lhs,
                    args: args.into_bump_slice(),
                }))
            }
            _ => lhs,
        }
    }

    fn current_infix_operator(&self) -> Option<(BindingPower, InfixOperator)> {
        let token = self.peek();
        if self.token_is_contextual(token, "or") {
            return Some((CONTEXTUAL_OR_BP, InfixOperator::Binary(BinOp::Or)));
        }
        if self.token_is_contextual(token, "and") {
            return Some((CONTEXTUAL_AND_BP, InfixOperator::Binary(BinOp::And)));
        }

        let binding = INFIX_BINDING_TABLE
            .iter()
            .find(|(kind, _)| *kind == token.kind)
            .map(|(_, binding)| *binding)?;
        let operator = match token.kind {
            TokenKind::Assign => InfixOperator::Assign,
            TokenKind::EqEq => InfixOperator::Binary(BinOp::Eq),
            TokenKind::BangEq => InfixOperator::Binary(BinOp::NotEq),
            TokenKind::Lt => InfixOperator::Binary(BinOp::Lt),
            TokenKind::Gt => InfixOperator::Binary(BinOp::Gt),
            TokenKind::LtEq => InfixOperator::Binary(BinOp::LtEq),
            TokenKind::GtEq => InfixOperator::Binary(BinOp::GtEq),
            TokenKind::Plus => InfixOperator::Binary(BinOp::Add),
            TokenKind::Minus => InfixOperator::Binary(BinOp::Sub),
            TokenKind::Star => InfixOperator::Binary(BinOp::Mul),
            TokenKind::Slash => InfixOperator::Binary(BinOp::Div),
            TokenKind::Percent => InfixOperator::Binary(BinOp::Rem),
            TokenKind::Dot | TokenKind::LBracket | TokenKind::LParen => InfixOperator::Postfix,
            _ => return None,
        };
        Some((binding, operator))
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum InfixOperator {
    Assign,
    Binary(BinOp),
    Postfix,
}
