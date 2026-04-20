//! Declaration parsing.

use thagore_ast::{
    ConstDecl, Constraint, ConstraintKind, Decl, ExternDecl, FieldDef, FlowDecl, FlowStage,
    FuncDecl, GenericFuncDecl, GenericImplBlock, GenericStructDecl, ImplBlock, ImportDecl,
    ImportSymbol, IntentDecl, LetDecl, Param, StructDecl, TypeParam,
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
            TokenKind::Func => self.parse_func_like_decl(),
            TokenKind::Let => Decl::Let(self.parse_let_decl()),
            TokenKind::Const => Decl::Const(self.parse_const_decl()),
            TokenKind::Struct => self.parse_struct_like_decl(),
            TokenKind::Impl => self.parse_impl_like_decl(),
            TokenKind::Import | TokenKind::From => Decl::Import(self.parse_import_decl()),
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
        let body = self.parse_block_for(Expectation::FuncBody);

        FuncDecl {
            id: self.new_node_id(),
            span: self.span_of(func_token).join(body.span),
            name,
            params,
            return_type,
            body,
        }
    }

    pub(crate) fn parse_func_like_decl(&mut self) -> Decl<'ast> {
        let func_token = self.advance();
        let name = self.parse_identifier_symbol(Expectation::Identifier);
        let type_params = self.parse_type_param_list();
        let params = self.parse_param_list();
        let return_type = if self.match_kind(TokenKind::Arrow).is_some() {
            Some(self.parse_type_expr())
        } else {
            None
        };
        self.expect_block_colon();
        let body = self.parse_block_for(Expectation::FuncBody);
        let span = self.span_of(func_token).join(body.span);
        let id = self.new_node_id();

        if type_params.is_empty() {
            Decl::Func(FuncDecl {
                id,
                span,
                name,
                params,
                return_type,
                body,
            })
        } else {
            Decl::GenericFunc(GenericFuncDecl {
                id,
                span,
                name,
                type_params,
                params,
                return_type,
                body,
            })
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

    pub(crate) fn parse_const_decl(&mut self) -> ConstDecl<'ast> {
        let const_token = self.advance();
        let name = self.parse_identifier_symbol(Expectation::Identifier);
        let type_ann = if self.match_kind(TokenKind::Colon).is_some() {
            self.parse_type_expr()
        } else {
            self.emit_statement_error(ParseError::missing_token(
                self.current_span(),
                TokenKind::Colon,
            ));
            self.synthetic_infer_type(self.current_span())
        };

        if self.match_kind(TokenKind::Assign).is_none() {
            self.emit_statement_error(ParseError::missing_token(
                self.current_span(),
                TokenKind::Assign,
            ));
        }

        let value = self.parse_expr(0);
        ConstDecl {
            id: self.new_node_id(),
            span: self.span_of(const_token).join(value.span()),
            name,
            type_ann,
            value,
        }
    }

    pub(crate) fn parse_struct_like_decl(&mut self) -> Decl<'ast> {
        let struct_token = self.advance();
        let name = self.parse_identifier_symbol(Expectation::Identifier);
        let type_params = self.parse_type_param_list();
        self.expect_block_colon();

        let start = self.span_of(struct_token);
        let entered_block = self.enter_indented_section_for(Expectation::StructBody);

        let mut fields = self.bump_vec();
        let mut end = start;
        while entered_block && !self.at(TokenKind::Dedent) && !self.at(TokenKind::Eof) {
            self.skip_newlines();
            if self.at(TokenKind::Dedent) || self.at(TokenKind::Eof) {
                break;
            }

            let field_token = self.peek();
            let field_name = self.parse_identifier_symbol(Expectation::Field);
            if self.match_kind(TokenKind::Colon).is_none() {
                self.emit_statement_error(ParseError::missing_block_colon(self.current_span()));
            }
            let ty = self.parse_type_expr();
            let field = FieldDef {
                id: self.new_node_id(),
                span: self.span_of(field_token).join(ty.span()),
                name: field_name,
                ty,
            };
            end = field.span;
            fields.push(field);
            self.consume_statement_terminator();
        }

        if entered_block {
            if let Some(dedent) = self.match_kind(TokenKind::Dedent) {
                end = end.join(self.span_of(dedent));
            } else if !self.at(TokenKind::Eof) {
                self.emit_statement_error(ParseError::missing_dedent(self.current_span()));
            }
        } else if !self.at(TokenKind::Eof) {
            end = end.join(self.current_span());
        }

        let id = self.new_node_id();
        let span = start.join(end);
        let fields = fields.into_bump_slice();
        if type_params.is_empty() {
            Decl::Struct(StructDecl {
                id,
                span,
                name,
                fields,
            })
        } else {
            Decl::GenericStruct(GenericStructDecl {
                id,
                span,
                name,
                type_params,
                fields,
            })
        }
    }

    pub(crate) fn parse_impl_like_decl(&mut self) -> Decl<'ast> {
        let impl_token = self.advance();
        let target = self.parse_identifier_symbol(Expectation::Identifier);
        let type_params = self.parse_type_param_list();
        self.expect_block_colon();

        let start = self.span_of(impl_token);
        let entered_block = self.enter_indented_section_for(Expectation::ImplBody);

        let mut methods = self.bump_vec();
        let mut end = start;
        while entered_block && !self.at(TokenKind::Dedent) && !self.at(TokenKind::Eof) {
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

        if entered_block {
            if let Some(dedent) = self.match_kind(TokenKind::Dedent) {
                end = end.join(self.span_of(dedent));
            } else if !self.at(TokenKind::Eof) {
                self.emit_statement_error(ParseError::missing_dedent(self.current_span()));
            }
        } else if !self.at(TokenKind::Eof) {
            end = end.join(self.current_span());
        }

        let id = self.new_node_id();
        let span = start.join(end);
        let methods = methods.into_bump_slice();
        if type_params.is_empty() {
            Decl::Impl(ImplBlock {
                id,
                span,
                target,
                methods,
            })
        } else {
            Decl::GenericImpl(GenericImplBlock {
                id,
                span,
                target,
                type_params,
                methods,
            })
        }
    }

    pub(crate) fn parse_import_decl(&mut self) -> ImportDecl<'ast> {
        let import_token = self.advance();
        let start = self.span_of(import_token);
        let mut end = start;

        if import_token.kind == TokenKind::From {
            let (relative_level, path_segments) = self.parse_import_path();
            if let Some(import_keyword) = self.expect(TokenKind::Import) {
                end = self.span_of(import_keyword);
            }

            let mut symbols = self.bump_vec();
            symbols.push(self.parse_import_symbol_entry());
            while self.match_kind(TokenKind::Comma).is_some() {
                symbols.push(self.parse_import_symbol_entry());
            }

            let include_all = if let Some(include_token) = self.match_kind(TokenKind::Include) {
                end = self.span_of(include_token);
                self.expect_import_all_keyword();
                true
            } else {
                false
            };

            if let Some(last_symbol) = symbols.last() {
                end = end.join(last_symbol.span);
            }

            return ImportDecl {
                id: self.new_node_id(),
                span: start.join(end),
                relative_level,
                path_segments: path_segments.into_bump_slice(),
                symbols: symbols.into_bump_slice(),
                is_from: true,
                include_all,
                alias: None,
            };
        }

        let (relative_level, path_segments) = self.parse_import_path();
        let alias = if self.match_contextual("as").is_some() {
            Some(self.parse_name_symbol(Expectation::Identifier))
        } else {
            None
        };
        let include_all = if let Some(include_token) = self.match_kind(TokenKind::Include) {
            end = self.span_of(include_token);
            self.expect_import_all_keyword();
            true
        } else {
            false
        };

        ImportDecl {
            id: self.new_node_id(),
            span: start.join(end.join(self.current_span())),
            relative_level,
            path_segments: path_segments.into_bump_slice(),
            symbols: self.bump_vec().into_bump_slice(),
            is_from: false,
            include_all,
            alias,
        }
    }

    fn parse_import_path(
        &mut self,
    ) -> (
        u8,
        bumpalo::collections::Vec<'ast, thagore_ast::InternedStr>,
    ) {
        let mut relative_level = 0_u8;
        while self.at(TokenKind::Dot) {
            self.advance();
            relative_level = relative_level.saturating_add(1);
        }

        let mut segments = self.bump_vec();
        if self.at(TokenKind::Identifier) {
            segments.push(self.parse_identifier_symbol(Expectation::ImportPathSegment));
            while self.match_kind(TokenKind::Dot).is_some() {
                segments.push(self.parse_identifier_symbol(Expectation::ImportPathSegment));
            }
        }

        (relative_level, segments)
    }

    fn parse_import_symbol_entry(&mut self) -> ImportSymbol {
        let start = self.current_span();
        let name = self.parse_identifier_symbol(Expectation::Identifier);
        let alias = if self.match_contextual("as").is_some() {
            Some(self.parse_identifier_symbol(Expectation::Identifier))
        } else {
            None
        };

        ImportSymbol {
            id: self.new_node_id(),
            span: start.join(self.current_span()),
            name,
            alias,
        }
    }

    fn expect_import_all_keyword(&mut self) {
        if self.match_contextual("all").is_none() {
            self.emit_statement_error(ParseError::unexpected_token(
                self.peek().kind,
                self.current_span(),
                Expectation::Identifier,
            ));
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

        let entered_block = self.enter_indented_section_for(Expectation::IntentBody);

        let mut constraints = self.bump_vec();
        let mut body = None;
        let mut end = self.span_of(intent_token);
        while entered_block && !self.at(TokenKind::Dedent) && !self.at(TokenKind::Eof) {
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

        if entered_block {
            if let Some(dedent) = self.match_kind(TokenKind::Dedent) {
                end = end.join(self.span_of(dedent));
            } else if !self.at(TokenKind::Eof) {
                self.emit_statement_error(ParseError::missing_dedent(self.current_span()));
            }
        } else if !self.at(TokenKind::Eof) {
            end = end.join(self.current_span());
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

        let entered_block = self.enter_indented_section_for(Expectation::FlowBody);

        let mut stages = self.bump_vec();
        let mut compensation = None;
        let mut end = self.span_of(flow_token);

        while entered_block && !self.at(TokenKind::Dedent) && !self.at(TokenKind::Eof) {
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

        if entered_block {
            if let Some(dedent) = self.match_kind(TokenKind::Dedent) {
                end = end.join(self.span_of(dedent));
            } else if !self.at(TokenKind::Eof) {
                self.emit_statement_error(ParseError::missing_dedent(self.current_span()));
            }
        } else if !self.at(TokenKind::Eof) {
            end = end.join(self.current_span());
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

    pub(crate) fn parse_type_param_list(&mut self) -> &'ast [TypeParam<'ast>] {
        if self.match_kind(TokenKind::Lt).is_none() {
            return self.bump_vec::<TypeParam<'ast>>().into_bump_slice();
        }

        let mut params = self.bump_vec();
        while !self.at(TokenKind::Gt) && !self.at(TokenKind::Eof) {
            let start = self.current_span();
            let name = self.parse_identifier_symbol(Expectation::Identifier);
            let mut constraints = self.bump_vec();
            let mut end = self.current_span();

            if self.match_kind(TokenKind::Colon).is_some() {
                constraints.push(self.parse_constraint());
                while self.match_kind(TokenKind::Plus).is_some() {
                    constraints.push(self.parse_constraint());
                }
                if let Some(last_constraint) = constraints.last() {
                    end = last_constraint.span;
                }
            }

            params.push(TypeParam {
                id: self.new_node_id(),
                span: start.join(end),
                name,
                constraints: constraints.into_bump_slice(),
            });

            if self.match_kind(TokenKind::Comma).is_none() {
                break;
            }
        }

        if self.match_kind(TokenKind::Gt).is_none() {
            self.emit_statement_error(ParseError::missing_token(
                self.current_span(),
                TokenKind::Gt,
            ));
        }

        params.into_bump_slice()
    }

    fn parse_constraint(&mut self) -> Constraint {
        let token = self.peek();
        let kind = match self.token_text(token) {
            Some("Ordered") => {
                self.advance();
                ConstraintKind::Ordered
            }
            Some("Eq") => {
                self.advance();
                ConstraintKind::Eq
            }
            Some("Numeric") => {
                self.advance();
                ConstraintKind::Numeric
            }
            _ => {
                self.emit_statement_error(ParseError::unexpected_token(
                    token.kind,
                    self.current_span(),
                    Expectation::Identifier,
                ));
                if !self.at(TokenKind::Eof) {
                    self.advance();
                }
                ConstraintKind::Ordered
            }
        };

        Constraint {
            id: self.new_node_id(),
            span: self.span_of(token),
            kind,
        }
    }
}
