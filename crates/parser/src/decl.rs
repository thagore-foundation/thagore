//! Declaration parsing.

use thagore_ast::{
    Decl, ExternDecl, FieldDef, FlowDecl, FlowStage, FuncDecl, ImplBlock, ImportDecl, IntentDecl,
    LetDecl, Param, StructDecl,
};
use thagore_lexer::TokenKind;

use crate::error::{Expectation, ParseError};
use crate::parser::Parser;

impl<'src, 'tok, 'ast> Parser<'src, 'tok, 'ast> {
    pub(crate) fn parse_decl(&mut self) -> Option<Decl<'ast>> {
        if self.consume_lexer_error_at_cursor() {
            return None;
        }

        let _is_public = self.match_contextual("pub").is_some();
        let decl = match self.peek().kind {
            TokenKind::Func => Decl::Func(self.parse_func_decl()),
            TokenKind::Let => Decl::Let(self.parse_let_decl()),
            TokenKind::Struct => Decl::Struct(self.parse_struct_decl()),
            TokenKind::Impl => Decl::Impl(self.parse_impl_block()),
            TokenKind::Import => Decl::Import(self.parse_import_decl()),
            TokenKind::Extern => Decl::Extern(self.parse_extern_decl()),
            TokenKind::Intent => Decl::Intent(self.parse_intent_decl()),
            TokenKind::Flow => Decl::Flow(self.parse_flow_decl()),
            _ => {
                self.emit_statement_error(ParseError::unexpected_token(
                    self.peek().kind,
                    self.current_span(),
                    Expectation::Declaration,
                ));
                return None;
            }
        };
        Some(decl)
    }

    pub(crate) fn parse_func_decl(&mut self) -> FuncDecl<'ast> {
        let func_token = self.advance();
        let name = self.parse_identifier_symbol(Expectation::Identifier);
        let params = self.parse_param_list();
        let return_type = if self.match_kind(TokenKind::Arrow).is_some() {
            Some(self.parse_type_expr())
        } else {
            None
        };
        self.expect_block_colon();
        let body = self.parse_block();

        FuncDecl {
            id: self.new_node_id(),
            span: self.span_of(func_token).join(body.span),
            name,
            params,
            return_type,
            body,
        }
    }

    pub(crate) fn parse_let_decl(&mut self) -> LetDecl<'ast> {
        let let_token = self.advance();
        let name = self.parse_identifier_symbol(Expectation::Identifier);
        let ty = if self.match_kind(TokenKind::Colon).is_some() {
            Some(self.parse_type_expr())
        } else {
            None
        };

        if self.match_kind(TokenKind::Assign).is_none() {
            self.emit_statement_error(ParseError::missing_token(
                self.current_span(),
                TokenKind::Assign,
            ));
        }

        let initializer = self.parse_expr(0);
        LetDecl {
            id: self.new_node_id(),
            span: self.span_of(let_token).join(initializer.span()),
            name,
            ty,
            initializer,
        }
    }

    pub(crate) fn parse_struct_decl(&mut self) -> StructDecl<'ast> {
        let struct_token = self.advance();
        let name = self.parse_identifier_symbol(Expectation::Identifier);
        self.expect_block_colon();

        let start = self.span_of(struct_token);
        self.skip_newlines();
        self.expect_indent();

        let mut fields = self.bump_vec();
        let mut end = start;
        while !self.at(TokenKind::Dedent) && !self.at(TokenKind::Eof) {
            self.skip_newlines();
            if self.at(TokenKind::Dedent) || self.at(TokenKind::Eof) {
                break;
            }

            let field_name = self.parse_identifier_symbol(Expectation::Field);
            if self.match_kind(TokenKind::Colon).is_none() {
                self.emit_statement_error(ParseError::missing_block_colon(self.current_span()));
            }
            let ty = self.parse_type_expr();
            let field = FieldDef {
                id: self.new_node_id(),
                span: ty.span(),
                name: field_name,
                ty,
            };
            end = field.span;
            fields.push(field);
            self.consume_statement_terminator();
        }

        if let Some(dedent) = self.match_kind(TokenKind::Dedent) {
            end = end.join(self.span_of(dedent));
        } else if !self.at(TokenKind::Eof) {
            self.emit_statement_error(ParseError::missing_dedent(self.current_span()));
        }

        StructDecl {
            id: self.new_node_id(),
            span: start.join(end),
            name,
            fields: fields.into_bump_slice(),
        }
    }

    pub(crate) fn parse_impl_block(&mut self) -> ImplBlock<'ast> {
        let impl_token = self.advance();
        let target = self.parse_identifier_symbol(Expectation::Identifier);
        self.expect_block_colon();

        let start = self.span_of(impl_token);
        self.skip_newlines();
        self.expect_indent();

        let mut methods = self.bump_vec();
        let mut end = start;
        while !self.at(TokenKind::Dedent) && !self.at(TokenKind::Eof) {
            self.skip_newlines();
            if self.at(TokenKind::Dedent) || self.at(TokenKind::Eof) {
                break;
            }

            if !self.at(TokenKind::Func) {
                self.emit_statement_error(ParseError::unexpected_token(
                    self.peek().kind,
                    self.current_span(),
                    Expectation::Declaration,
                ));
                self.synchronize_statement();
                continue;
            }

            let method = self.parse_func_decl();
            end = method.span;
            methods.push(method);
            self.consume_statement_terminator();
        }

        if let Some(dedent) = self.match_kind(TokenKind::Dedent) {
            end = end.join(self.span_of(dedent));
        } else if !self.at(TokenKind::Eof) {
            self.emit_statement_error(ParseError::missing_dedent(self.current_span()));
        }

        ImplBlock {
            id: self.new_node_id(),
            span: start.join(end),
            target,
            methods: methods.into_bump_slice(),
        }
    }

    pub(crate) fn parse_import_decl(&mut self) -> ImportDecl<'ast> {
        let import_token = self.advance();
        let mut segments = self.bump_vec();
        segments.push(self.parse_identifier_symbol(Expectation::ImportPathSegment));
        while self.match_kind(TokenKind::Dot).is_some() {
            segments.push(self.parse_identifier_symbol(Expectation::ImportPathSegment));
        }
        let alias = if self.match_contextual("as").is_some() {
            Some(self.parse_identifier_symbol(Expectation::Identifier))
        } else {
            None
        };
        ImportDecl {
            id: self.new_node_id(),
            span: self.span_of(import_token).join(self.current_span()),
            path_segments: segments.into_bump_slice(),
            alias,
        }
    }

    pub(crate) fn parse_extern_decl(&mut self) -> ExternDecl<'ast> {
        let extern_token = self.advance();
        if self.match_kind(TokenKind::Func).is_none() {
            self.emit_statement_error(ParseError::missing_token(
                self.current_span(),
                TokenKind::Func,
            ));
        }

        let name = self.parse_identifier_symbol(Expectation::Identifier);
        let params = self.parse_param_list();
        if self.match_kind(TokenKind::Arrow).is_none() {
            self.emit_statement_error(ParseError::missing_token(
                self.current_span(),
                TokenKind::Arrow,
            ));
        }
        let return_type = self.parse_type_expr();
        ExternDecl {
            id: self.new_node_id(),
            span: self.span_of(extern_token).join(return_type.span()),
            name,
            params,
            return_type,
        }
    }

    pub(crate) fn parse_intent_decl(&mut self) -> IntentDecl<'ast> {
        let intent_token = self.advance();
        let name = self.parse_identifier_symbol(Expectation::Identifier);
        self.expect_block_colon();

        self.skip_newlines();
        self.expect_indent();

        let mut constraints = self.bump_vec();
        let mut body = None;
        let mut end = self.span_of(intent_token);
        while !self.at(TokenKind::Dedent) && !self.at(TokenKind::Eof) {
            self.skip_newlines();
            if self.at(TokenKind::Dedent) || self.at(TokenKind::Eof) {
                break;
            }

            if self.at_contextual("body") {
                self.advance();
                self.expect_block_colon();
                let parsed_body = self.parse_block();
                end = parsed_body.span;
                body = Some(parsed_body);
                break;
            }

            if self.at_contextual("minimize") || self.at_contextual("maximize") {
                self.advance();
            }
            let constraint = self.parse_expr(0);
            end = constraint.span();
            constraints.push(constraint);
            self.consume_statement_terminator();
        }

        if let Some(dedent) = self.match_kind(TokenKind::Dedent) {
            end = end.join(self.span_of(dedent));
        } else if !self.at(TokenKind::Eof) {
            self.emit_statement_error(ParseError::missing_dedent(self.current_span()));
        }

        let body = if let Some(body) = body {
            body
        } else {
            let id = self.new_node_id();
            self.alloc_block(thagore_ast::Block {
                id,
                span: self.current_span(),
                statements: self.bump_vec::<thagore_ast::Stmt<'ast>>().into_bump_slice(),
            })
        };

        IntentDecl {
            id: self.new_node_id(),
            span: self.span_of(intent_token).join(end),
            name,
            constraints: constraints.into_bump_slice(),
            body,
        }
    }

    pub(crate) fn parse_flow_decl(&mut self) -> FlowDecl<'ast> {
        let flow_token = self.advance();
        let name = self.parse_identifier_symbol(Expectation::Identifier);
        self.expect_block_colon();

        self.skip_newlines();
        self.expect_indent();

        let mut stages = self.bump_vec();
        let mut compensation = None;
        let mut end = self.span_of(flow_token);

        while !self.at(TokenKind::Dedent) && !self.at(TokenKind::Eof) {
            self.skip_newlines();
            if self.at(TokenKind::Dedent) || self.at(TokenKind::Eof) {
                break;
            }

            if self.at_contextual("stage") {
                let stage_token = self.advance();
                let stage_name = self.parse_identifier_symbol(Expectation::FlowStage);
                self.expect_block_colon();
                let body = self.parse_block();
                let stage = FlowStage {
                    id: self.new_node_id(),
                    span: self.span_of(stage_token).join(body.span),
                    name: stage_name,
                    body,
                };
                end = stage.span;
                stages.push(stage);
                continue;
            }

            if self.at_contextual("compensate") {
                self.advance();
                self.expect_block_colon();
                let body = self.parse_block();
                end = body.span;
                compensation = Some(body);
                continue;
            }

            self.emit_statement_error(ParseError::unexpected_token(
                self.peek().kind,
                self.current_span(),
                Expectation::FlowStage,
            ));
            self.synchronize_statement();
        }

        if let Some(dedent) = self.match_kind(TokenKind::Dedent) {
            end = end.join(self.span_of(dedent));
        } else if !self.at(TokenKind::Eof) {
            self.emit_statement_error(ParseError::missing_dedent(self.current_span()));
        }

        FlowDecl {
            id: self.new_node_id(),
            span: self.span_of(flow_token).join(end),
            name,
            stages: stages.into_bump_slice(),
            compensation,
        }
    }

    pub(crate) fn parse_param_list(&mut self) -> &'ast [Param<'ast>] {
        if self.match_kind(TokenKind::LParen).is_none() {
            self.emit_statement_error(ParseError::missing_token(
                self.current_span(),
                TokenKind::LParen,
            ));
            return self.bump_vec::<Param<'ast>>().into_bump_slice();
        }

        let mut params = self.bump_vec();
        while !self.at(TokenKind::RParen) && !self.at(TokenKind::Eof) {
            let name_token = self.peek();
            let name = self.parse_identifier_symbol(Expectation::Parameter);
            let ty = if self.match_kind(TokenKind::Colon).is_some() {
                self.parse_type_expr()
            } else if self.token_text(name_token) == Some("self") {
                self.synthetic_infer_type(self.span_of(name_token))
            } else {
                self.emit_statement_error(ParseError::missing_token(
                    self.current_span(),
                    TokenKind::Colon,
                ));
                self.synthetic_infer_type(self.current_span())
            };

            params.push(Param {
                id: self.new_node_id(),
                span: self.span_of(name_token).join(ty.span()),
                name,
                ty,
            });

            if self.match_kind(TokenKind::Comma).is_none() {
                break;
            }
        }

        if self.match_kind(TokenKind::RParen).is_none() {
            self.emit_statement_error(ParseError::missing_token(
                self.current_span(),
                TokenKind::RParen,
            ));
        }

        params.into_bump_slice()
    }
}
