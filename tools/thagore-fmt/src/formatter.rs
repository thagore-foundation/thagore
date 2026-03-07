//! AST-first Thagore pretty-printer.

use std::collections::{BTreeMap, HashMap};
use std::path::Path;

use bumpalo::Bump;
use thagore_ast::{
    AssignExpr, BinOp, BinaryExpr, BlockRef, ConstDecl, Decl, Expr, ExprRef, ExprStmt,
    ExternDecl, FieldAccessExpr, FlowDecl, FlowStage, ForStmt, FuncDecl, GenericFuncDecl,
    GenericImplBlock, GenericStructDecl, GenericTypeExpr, IdentExpr, IfStmt, ImportDecl,
    ImportSymbol, InferTypeExpr, LetDecl, LitExpr, Literal, NamedTypeExpr, Param, ReturnStmt,
    Stmt, StructDecl, TypeExpr, TypeExprRef, TypeParam, UnaryExpr, UnaryOp, WhileStmt,
};
use thagore_lexer::Lexer;
use thagore_parser::{ParseError, Parser};

use crate::config::FmtConfig;
use crate::rules::{
    blank_lines_between_decls, blank_lines_between_statements, classify_import, import_path_text,
    import_sort_key, normalize_comment_text,
};

/// Formatter output for one file.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FormatFileResult {
    /// Formatted source text.
    pub formatted: String,
    /// Parse errors produced while formatting.
    pub parse_errors: Vec<ParseError>,
}

/// Formats `source` as Thagore code using the supplied config.
#[must_use]
pub fn format_source(path: &Path, source: &str, config: &FmtConfig) -> FormatFileResult {
    let (formatted, parse_errors) = {
        let arena = Bump::new();
        let mut lexer = Lexer::new(source);
        let tokens = lexer.lex_all_in(&arena);
        let mut parser = Parser::new(tokens.as_slice(), &arena, lexer.interner());
        let decls = parser.parse_program();
        let parse_errors = parser.take_errors();
        if !parse_errors.is_empty() {
            (source.to_string(), parse_errors)
        } else {
            let layout = SourceLayout::new(source, &decls);
            let mut formatter = Formatter::new(path, source, config, &parser, &layout);
            formatter.fmt_file(&decls);
            (formatter.finish(), parse_errors)
        }
    };

    FormatFileResult {
        formatted,
        parse_errors,
    }
}

#[derive(Debug, Clone)]
struct CommentLine {
    line_index: usize,
    text: String,
}

#[derive(Debug, Clone)]
struct SourceLayout {
    line_starts: Vec<usize>,
    comment_lines: Vec<CommentLine>,
    inline_comments: HashMap<usize, String>,
    public_decl_lines: BTreeMap<usize, bool>,
}

impl SourceLayout {
    fn new<'ast>(source: &str, decls: &'ast [Decl<'ast>]) -> Self {
        let line_starts = line_start_offsets(source);
        let mut comment_lines = Vec::new();
        let mut inline_comments = HashMap::new();
        for (index, line) in source.lines().enumerate() {
            let trimmed = line.trim_start();
            if trimmed.starts_with('#') {
                comment_lines.push(CommentLine {
                    line_index: index,
                    text: normalize_comment_text(trimmed),
                });
                continue;
            }
            if let Some(comment) = inline_comment(line) {
                inline_comments.insert(index, normalize_comment_text(comment));
            }
        }

        let mut public_decl_lines = BTreeMap::new();
        for decl in decls {
            let start_line = line_index_from_offset(&line_starts, decl.span().start as usize);
            let original_line = source.lines().nth(start_line).unwrap_or_default().trim_start();
            public_decl_lines.insert(start_line, original_line.starts_with("pub "));
        }

        Self {
            line_starts,
            comment_lines,
            inline_comments,
            public_decl_lines,
        }
    }

    fn line_of_span(&self, span: thagore_ast::Span) -> usize {
        line_index_from_offset(&self.line_starts, span.start as usize)
    }

    fn original_gap(&self, prev_end: thagore_ast::Span, next_start: thagore_ast::Span) -> usize {
        let prev_line = self.line_of_span(prev_end);
        let next_line = self.line_of_span(next_start);
        next_line.saturating_sub(prev_line + 1)
    }

    fn is_public_decl(&self, span: thagore_ast::Span) -> bool {
        self.public_decl_lines
            .get(&self.line_of_span(span))
            .copied()
            .unwrap_or(false)
    }

    fn inline_comment(&self, span: thagore_ast::Span) -> Option<&str> {
        self.inline_comments
            .get(&self.line_of_span(span))
            .map(String::as_str)
    }
}

struct Formatter<'src, 'tok, 'ast> {
    config: &'src FmtConfig,
    parser: &'src Parser<'src, 'tok, 'ast>,
    layout: &'src SourceLayout,
    output: String,
    indent: usize,
    last_was_blank: bool,
    next_comment: usize,
}

impl<'src, 'tok, 'ast> Formatter<'src, 'tok, 'ast> {
    fn new(
        _path: &'src Path,
        _source: &'src str,
        config: &'src FmtConfig,
        parser: &'src Parser<'src, 'tok, 'ast>,
        layout: &'src SourceLayout,
    ) -> Self {
        Self {
            config,
            parser,
            layout,
            output: String::new(),
            indent: 0,
            last_was_blank: false,
            next_comment: 0,
        }
    }

    fn finish(mut self) -> String {
        self.emit_remaining_comments(0);
        let trimmed = self.output.trim_end_matches('\n');
        let mut out = trimmed.to_string();
        if self.config.trailing_newline {
            out.push('\n');
        }
        out
    }

    fn fmt_file(&mut self, decls: &'ast [Decl<'ast>]) {
        self.fmt_decls(decls);
    }

    fn fmt_decls(&mut self, decls: &'ast [Decl<'ast>]) {
        let mut index = 0;
        let mut previous_non_import: Option<&Decl<'ast>> = None;
        let mut previous_decl: Option<&Decl<'ast>> = None;

        while index < decls.len() {
            if matches!(decls[index], Decl::Import(_)) {
                let start = index;
                while index < decls.len() && matches!(decls[index], Decl::Import(_)) {
                    index += 1;
                }
                let block = &decls[start..index];
                if let Some(first) = block.first() {
                    self.emit_comments_before_line(self.layout.line_of_span(first.span()), 0);
                }
                if let Some(prev) = previous_decl {
                    self.emit_blank_lines(blank_lines_between_decls(
                        self.config,
                        Some(prev),
                        block.first().unwrap(),
                        self.layout.original_gap(prev.span(), block.first().unwrap().span()),
                    ));
                }
                self.fmt_import_block(block);
                previous_decl = block.last();
                continue;
            }

            let decl = &decls[index];
            self.emit_comments_before_line(self.layout.line_of_span(decl.span()), 0);
            let original_gap = previous_decl
                .map(|prev| self.layout.original_gap(prev.span(), decl.span()))
                .unwrap_or(0);
            self.emit_blank_lines(blank_lines_between_decls(
                self.config,
                previous_non_import,
                decl,
                original_gap,
            ));
            self.fmt_decl(decl);
            previous_decl = Some(decl);
            previous_non_import = Some(decl);
            index += 1;
        }
    }

    fn fmt_import_block(&mut self, decls: &'ast [Decl<'ast>]) {
        let mut imports = decls
            .iter()
            .filter_map(|decl| match decl {
                Decl::Import(import) => Some(import),
                _ => None,
            })
            .collect::<Vec<_>>();

        if self.config.sort_imports {
            imports.sort_by_key(|import| import_sort_key(self.parser, import));
        }

        let mut previous_group = None;
        for (index, import) in imports.iter().enumerate() {
            let group = classify_import(&import_path_text(self.parser, import));
            if index > 0 {
                if self.config.sort_imports && previous_group != Some(group) {
                    self.blank_line();
                }
            }
            previous_group = Some(group);
            self.fmt_import(import);
        }
    }

    fn fmt_decl(&mut self, decl: &'ast Decl<'ast>) {
        match decl {
            Decl::Func(node) => self.fmt_func(node, false),
            Decl::GenericFunc(node) => self.fmt_generic_func(node),
            Decl::Let(node) => self.write_line(&self.fmt_let_like(node), self.layout.inline_comment(node.span)),
            Decl::Const(node) => self.fmt_const(node),
            Decl::Struct(node) => self.fmt_struct(node, false),
            Decl::GenericStruct(node) => self.fmt_generic_struct(node),
            Decl::Impl(node) => self.fmt_impl(node),
            Decl::GenericImpl(node) => self.fmt_generic_impl(node),
            Decl::Import(node) => self.fmt_import(node),
            Decl::Extern(node) => self.fmt_extern(node),
            Decl::Intent(node) => self.fmt_intent(node),
            Decl::Flow(node) => self.fmt_flow(node),
        }
    }

    fn fmt_import(&mut self, import: &'ast ImportDecl<'ast>) {
        let mut line = String::new();
        if import.is_from {
            line.push_str("from ");
        } else {
            line.push_str("import ");
        }
        line.push_str(&self.import_path(import));
        if import.is_from {
            line.push_str(" import ");
            let mut symbols = import.symbols.iter().collect::<Vec<_>>();
            if self.config.sort_imports {
                symbols.sort_by_key(|symbol| self.resolve(symbol.name));
            }
            line.push_str(
                &symbols
                    .into_iter()
                    .map(|symbol| self.fmt_import_symbol(symbol))
                    .collect::<Vec<_>>()
                    .join(", "),
            );
            if import.include_all {
                line.push_str(" include all");
            }
        } else {
            if let Some(alias) = import.alias {
                line.push_str(" as ");
                line.push_str(&self.resolve(alias));
            }
            if import.include_all {
                line.push_str(" include all");
            }
        }
        self.write_line(&line, self.layout.inline_comment(import.span));
    }

    fn fmt_func(&mut self, func: &'ast FuncDecl<'ast>, is_method: bool) {
        let mut header = String::new();
        if self.layout.is_public_decl(func.span) && !is_method {
            header.push_str("pub ");
        }
        header.push_str("func ");
        header.push_str(&self.resolve(func.name));
        header.push_str(&self.fmt_params(func.params));
        if let Some(return_type) = func.return_type {
            header.push_str(" -> ");
            header.push_str(&self.fmt_type(return_type));
        }
        header.push(':');
        self.write_line(&header, self.layout.inline_comment(func.span));
        self.indent += 1;
        self.fmt_block(func.body);
        self.indent -= 1;
    }

    fn fmt_generic_func(&mut self, func: &'ast GenericFuncDecl<'ast>) {
        let mut header = String::new();
        if self.layout.is_public_decl(func.span) {
            header.push_str("pub ");
        }
        header.push_str("func ");
        header.push_str(&self.resolve(func.name));
        header.push_str(&self.fmt_type_params(func.type_params));
        header.push_str(&self.fmt_params(func.params));
        if let Some(return_type) = func.return_type {
            header.push_str(" -> ");
            header.push_str(&self.fmt_type(return_type));
        }
        header.push(':');
        self.write_line(&header, self.layout.inline_comment(func.span));
        self.indent += 1;
        self.fmt_block(func.body);
        self.indent -= 1;
    }

    fn fmt_const(&mut self, decl: &'ast ConstDecl<'ast>) {
        let mut line = String::new();
        if self.layout.is_public_decl(decl.span) {
            line.push_str("pub ");
        }
        line.push_str("const ");
        line.push_str(&self.resolve(decl.name));
        line.push_str(": ");
        line.push_str(&self.fmt_type(decl.type_ann));
        line.push_str(" = ");
        line.push_str(&self.fmt_expr(decl.value, 0));
        self.write_line(&line, self.layout.inline_comment(decl.span));
    }

    fn fmt_struct(&mut self, decl: &'ast StructDecl<'ast>, _generic: bool) {
        let mut line = String::new();
        if self.layout.is_public_decl(decl.span) {
            line.push_str("pub ");
        }
        line.push_str("struct ");
        line.push_str(&self.resolve(decl.name));
        line.push(':');
        self.write_line(&line, self.layout.inline_comment(decl.span));
        self.indent += 1;
        for (index, field) in decl.fields.iter().enumerate() {
            if index > 0 {
                self.newline();
            }
            self.emit_comments_before_line(self.layout.line_of_span(field.span), self.indent);
            self.write_line(
                &format!("{}: {}", self.resolve(field.name), self.fmt_type(field.ty)),
                self.layout.inline_comment(field.span),
            );
        }
        self.indent -= 1;
    }

    fn fmt_generic_struct(&mut self, decl: &'ast GenericStructDecl<'ast>) {
        let mut line = String::new();
        if self.layout.is_public_decl(decl.span) {
            line.push_str("pub ");
        }
        line.push_str("struct ");
        line.push_str(&self.resolve(decl.name));
        line.push_str(&self.fmt_type_params(decl.type_params));
        line.push(':');
        self.write_line(&line, self.layout.inline_comment(decl.span));
        self.indent += 1;
        for (index, field) in decl.fields.iter().enumerate() {
            if index > 0 {
                self.newline();
            }
            self.emit_comments_before_line(self.layout.line_of_span(field.span), self.indent);
            self.write_line(
                &format!("{}: {}", self.resolve(field.name), self.fmt_type(field.ty)),
                self.layout.inline_comment(field.span),
            );
        }
        self.indent -= 1;
    }

    fn fmt_impl(&mut self, decl: &'ast thagore_ast::ImplBlock<'ast>) {
        let header = format!("impl {}:", self.resolve(decl.target));
        self.write_line(&header, self.layout.inline_comment(decl.span));
        self.indent += 1;
        for (index, method) in decl.methods.iter().enumerate() {
            if index > 0 {
                self.blank_line();
            }
            self.emit_comments_before_line(self.layout.line_of_span(method.span), self.indent);
            self.fmt_func(method, true);
        }
        self.indent -= 1;
    }

    fn fmt_generic_impl(&mut self, decl: &'ast GenericImplBlock<'ast>) {
        let header = format!(
            "impl {}{}:",
            self.resolve(decl.target),
            self.fmt_type_params(decl.type_params)
        );
        self.write_line(&header, self.layout.inline_comment(decl.span));
        self.indent += 1;
        for (index, method) in decl.methods.iter().enumerate() {
            if index > 0 {
                self.blank_line();
            }
            self.emit_comments_before_line(self.layout.line_of_span(method.span), self.indent);
            self.fmt_func(method, true);
        }
        self.indent -= 1;
    }

    fn fmt_extern(&mut self, decl: &'ast ExternDecl<'ast>) {
        let line = format!(
            "extern func {}{} -> {}",
            self.resolve(decl.name),
            self.fmt_params(decl.params),
            self.fmt_type(decl.return_type)
        );
        self.write_line(&line, self.layout.inline_comment(decl.span));
    }

    fn fmt_intent(&mut self, decl: &'ast thagore_ast::IntentDecl<'ast>) {
        let header = format!("intent {}:", self.resolve(decl.name));
        self.write_line(&header, self.layout.inline_comment(decl.span));
        self.indent += 1;
        for constraint in decl.constraints {
            self.write_line(
                &self.fmt_expr(*constraint, 0),
                self.layout.inline_comment(constraint.span()),
            );
        }
        self.fmt_block(decl.body);
        self.indent -= 1;
    }

    fn fmt_flow(&mut self, decl: &'ast FlowDecl<'ast>) {
        let header = format!("flow {}:", self.resolve(decl.name));
        self.write_line(&header, self.layout.inline_comment(decl.span));
        self.indent += 1;
        for stage in decl.stages {
            self.fmt_flow_stage(stage);
            self.newline();
        }
        if let Some(compensation) = decl.compensation {
            self.write_line("compensate:", None);
            self.indent += 1;
            self.fmt_block(compensation);
            self.indent -= 1;
        } else if !decl.stages.is_empty() {
            self.output = self.output.trim_end_matches('\n').to_string();
            self.newline();
        }
        self.indent -= 1;
    }

    fn fmt_flow_stage(&mut self, stage: &'ast FlowStage<'ast>) {
        let header = format!("stage {}:", self.resolve(stage.name));
        self.write_line(&header, self.layout.inline_comment(stage.span));
        self.indent += 1;
        self.fmt_block(stage.body);
        self.indent -= 1;
    }

    fn fmt_block(&mut self, block: BlockRef<'ast>) {
        let mut previous_stmt: Option<&Stmt<'ast>> = None;
        for stmt in block.statements {
            self.emit_comments_before_line(self.layout.line_of_span(stmt.span()), self.indent);
            let original_gap = previous_stmt
                .map(|prev| self.layout.original_gap(prev.span(), stmt.span()))
                .unwrap_or(0);
            self.emit_blank_lines(blank_lines_between_statements(original_gap));
            self.fmt_stmt(stmt);
            previous_stmt = Some(stmt);
        }
    }

    fn fmt_stmt(&mut self, stmt: &'ast Stmt<'ast>) {
        match stmt {
            Stmt::Let(node) => self.write_line(&self.fmt_let_like(node), self.layout.inline_comment(node.span)),
            Stmt::Expr(node) => self.fmt_expr_stmt(node),
            Stmt::Return(node) => self.fmt_return(node),
            Stmt::If(node) => self.fmt_if(node),
            Stmt::While(node) => self.fmt_while(node),
            Stmt::For(node) => self.fmt_for(node),
            Stmt::Break(node) => self.write_line("break", self.layout.inline_comment(node.span)),
            Stmt::Continue(node) => self.write_line("continue", self.layout.inline_comment(node.span)),
        }
    }

    fn fmt_expr_stmt(&mut self, stmt: &'ast ExprStmt<'ast>) {
        self.write_line(
            &self.fmt_expr(stmt.expr, 0),
            self.layout.inline_comment(stmt.span),
        );
    }

    fn fmt_return(&mut self, stmt: &'ast ReturnStmt<'ast>) {
        let line = if let Some(value) = stmt.value {
            format!("return {}", self.fmt_expr(value, 0))
        } else {
            String::from("return")
        };
        self.write_line(&line, self.layout.inline_comment(stmt.span));
    }

    fn fmt_if(&mut self, stmt: &'ast IfStmt<'ast>) {
        let line = format!("if ({}):", self.fmt_expr(stmt.condition, 0));
        self.write_line(&line, self.layout.inline_comment(stmt.span));
        self.indent += 1;
        self.fmt_block(stmt.then_block);
        self.indent -= 1;
        if let Some(else_block) = stmt.else_block {
            self.write_line("else:", None);
            self.indent += 1;
            self.fmt_block(else_block);
            self.indent -= 1;
        }
    }

    fn fmt_while(&mut self, stmt: &'ast WhileStmt<'ast>) {
        let line = format!("while ({}):", self.fmt_expr(stmt.condition, 0));
        self.write_line(&line, self.layout.inline_comment(stmt.span));
        self.indent += 1;
        self.fmt_block(stmt.body);
        self.indent -= 1;
    }

    fn fmt_for(&mut self, stmt: &'ast ForStmt<'ast>) {
        let line = format!(
            "for {} in {}:",
            self.resolve(stmt.binding),
            self.fmt_expr(stmt.iterator, 0)
        );
        self.write_line(&line, self.layout.inline_comment(stmt.span));
        self.indent += 1;
        self.fmt_block(stmt.body);
        self.indent -= 1;
    }

    fn fmt_let_like(&self, decl: &'ast LetDecl<'ast>) -> String {
        let mut line = format!("let {}", self.resolve(decl.name));
        if let Some(ty) = decl.ty {
            line.push_str(": ");
            line.push_str(&self.fmt_type(ty));
        }
        line.push_str(" = ");
        line.push_str(&self.fmt_expr(decl.initializer, 0));
        line
    }

    fn fmt_expr(&self, expr: ExprRef<'ast>, parent_precedence: u8) -> String {
        let precedence = expr_precedence(expr);
        let mut text = match expr {
            Expr::Binary(node) => self.fmt_binary_expr(node),
            Expr::Unary(node) => self.fmt_unary_expr(node),
            Expr::Call(node) => self.fmt_call_expr(node),
            Expr::FieldAccess(node) => self.fmt_field_access(node),
            Expr::Index(node) => self.fmt_index_expr(node),
            Expr::Ident(node) => self.fmt_ident_expr(node),
            Expr::Literal(node) => self.fmt_lit_expr(node),
            Expr::Assign(node) => self.fmt_assign_expr(node),
        };
        if precedence < parent_precedence {
            text = format!("({text})");
        }
        text
    }

    fn fmt_binary_expr(&self, expr: &'ast BinaryExpr<'ast>) -> String {
        let precedence = match expr.op {
            BinOp::Or => 2,
            BinOp::And => 3,
            BinOp::Eq | BinOp::NotEq => 4,
            BinOp::Lt | BinOp::LtEq | BinOp::Gt | BinOp::GtEq => 5,
            BinOp::Add | BinOp::Sub => 6,
            BinOp::Mul | BinOp::Div | BinOp::Rem => 7,
        };
        let left = self.fmt_expr(expr.left, precedence);
        let right = self.fmt_expr(expr.right, precedence + 1);
        format!("{left} {} {right}", bin_op_text(expr.op))
    }

    fn fmt_unary_expr(&self, expr: &'ast UnaryExpr<'ast>) -> String {
        format!("{}{}", unary_op_text(expr.op), self.fmt_expr(expr.operand, 8))
    }

    fn fmt_call_expr(&self, expr: &'ast thagore_ast::CallExpr<'ast>) -> String {
        let args = expr
            .args
            .iter()
            .map(|arg| self.fmt_expr(*arg, 0))
            .collect::<Vec<_>>()
            .join(", ");
        format!("{}({args})", self.fmt_expr(expr.callee, 10))
    }

    fn fmt_field_access(&self, expr: &'ast FieldAccessExpr<'ast>) -> String {
        format!("{}.{}", self.fmt_expr(expr.object, 10), self.resolve(expr.field))
    }

    fn fmt_index_expr(&self, expr: &'ast thagore_ast::IndexExpr<'ast>) -> String {
        format!("{}[{}]", self.fmt_expr(expr.object, 10), self.fmt_expr(expr.index, 0))
    }

    fn fmt_ident_expr(&self, expr: &'ast IdentExpr) -> String {
        self.resolve(expr.name)
    }

    fn fmt_lit_expr(&self, expr: &'ast LitExpr) -> String {
        match expr.literal {
            Literal::Int(value) => value.to_string(),
            Literal::Float(value) => {
                let text = value.to_string();
                if text.contains('.') { text } else { format!("{text}.0") }
            }
            Literal::Bool(value) => value.to_string(),
            Literal::Str(symbol) => format!("\"{}\"", escape_string(&self.resolve(symbol))),
        }
    }

    fn fmt_assign_expr(&self, expr: &'ast AssignExpr<'ast>) -> String {
        format!(
            "{} = {}",
            self.fmt_expr(expr.target, 1),
            self.fmt_expr(expr.value, 1)
        )
    }

    fn fmt_type(&self, ty: TypeExprRef<'ast>) -> String {
        match ty {
            TypeExpr::Named(node) => self.fmt_named_type(node),
            TypeExpr::Generic(node) => self.fmt_generic_type(node),
            TypeExpr::Infer(node) => self.fmt_infer_type(node),
        }
    }

    fn fmt_named_type(&self, ty: &'ast NamedTypeExpr) -> String {
        self.resolve(ty.name)
    }

    fn fmt_generic_type(&self, ty: &'ast GenericTypeExpr<'ast>) -> String {
        let args = ty
            .args
            .iter()
            .map(|arg| self.fmt_type(*arg))
            .collect::<Vec<_>>()
            .join(", ");
        format!("{}<{args}>", self.resolve(ty.name))
    }

    fn fmt_infer_type(&self, _ty: &'ast InferTypeExpr) -> String {
        String::from("_")
    }

    fn fmt_type_params(&self, type_params: &'ast [TypeParam<'ast>]) -> String {
        if type_params.is_empty() {
            return String::new();
        }
        let body = type_params
            .iter()
            .map(|type_param| {
                let mut text = self.resolve(type_param.name);
                if !type_param.constraints.is_empty() {
                    let constraints = type_param
                        .constraints
                        .iter()
                        .map(|constraint| match constraint.kind {
                            thagore_ast::ConstraintKind::Ordered => "Ordered".to_string(),
                            thagore_ast::ConstraintKind::Eq => "Eq".to_string(),
                            thagore_ast::ConstraintKind::Numeric => "Numeric".to_string(),
                        })
                        .collect::<Vec<_>>()
                        .join(" + ");
                    text.push_str(": ");
                    text.push_str(&constraints);
                }
                text
            })
            .collect::<Vec<_>>()
            .join(", ");
        format!("<{body}>")
    }

    fn fmt_params(&self, params: &'ast [Param<'ast>]) -> String {
        let body = params
            .iter()
            .map(|param| format!("{}: {}", self.resolve(param.name), self.fmt_type(param.ty)))
            .collect::<Vec<_>>()
            .join(", ");
        format!("({body})")
    }

    fn fmt_import_symbol(&self, symbol: &'ast ImportSymbol) -> String {
        let mut text = self.resolve(symbol.name);
        if let Some(alias) = symbol.alias {
            text.push_str(" as ");
            text.push_str(&self.resolve(alias));
        }
        text
    }

    fn import_path(&self, import: &'ast ImportDecl<'ast>) -> String {
        let joined = import
            .path_segments
            .iter()
            .map(|segment| self.resolve(*segment))
            .collect::<Vec<_>>()
            .join(".");
        if import.relative_level == 0 {
            return joined;
        }
        let mut prefix = String::new();
        for _ in 0..import.relative_level {
            prefix.push('.');
        }
        if joined.is_empty() {
            prefix
        } else {
            format!("{prefix}{joined}")
        }
    }

    fn resolve(&self, symbol: thagore_ast::InternedStr) -> String {
        self.parser
            .resolve_symbol(symbol)
            .unwrap_or("__unknown__")
            .to_string()
    }

    fn emit_comments_before_line(&mut self, target_line: usize, indent: usize) {
        while let Some(comment) = self.layout.comment_lines.get(self.next_comment) {
            if comment.line_index >= target_line {
                break;
            }
            if !self.output.is_empty() && !self.last_was_blank {
                self.newline();
            }
            self.output.push_str(&" ".repeat(indent * self.config.indent_size));
            self.output.push_str(&comment.text);
            self.newline();
            self.next_comment += 1;
        }
    }

    fn emit_remaining_comments(&mut self, indent: usize) {
        while let Some(comment) = self.layout.comment_lines.get(self.next_comment) {
            if !self.output.is_empty() && !self.last_was_blank {
                self.newline();
            }
            self.output.push_str(&" ".repeat(indent * self.config.indent_size));
            self.output.push_str(&comment.text);
            self.newline();
            self.next_comment += 1;
        }
    }

    fn emit_blank_lines(&mut self, count: usize) {
        for _ in 0..count {
            self.blank_line();
        }
    }

    fn write_line(&mut self, line: &str, inline_comment: Option<&str>) {
        if !self.output.is_empty() && !self.output.ends_with('\n') {
            self.newline();
        }
        self.output
            .push_str(&" ".repeat(self.indent * self.config.indent_size));
        self.output.push_str(line.trim_end());
        if let Some(comment) = inline_comment {
            self.output.push_str("  ");
            self.output.push_str(comment);
        }
        self.newline();
        self.last_was_blank = false;
    }

    fn newline(&mut self) {
        self.output.push('\n');
        self.last_was_blank = false;
    }

    fn blank_line(&mut self) {
        if self.output.is_empty() {
            return;
        }
        if self.output.ends_with("\n\n") {
            self.last_was_blank = true;
            return;
        }
        if !self.output.ends_with('\n') {
            self.output.push('\n');
        }
        self.output.push('\n');
        self.last_was_blank = true;
    }
}

fn expr_precedence(expr: ExprRef<'_>) -> u8 {
    match expr {
        Expr::Assign(_) => 1,
        Expr::Binary(node) => match node.op {
            BinOp::Or => 2,
            BinOp::And => 3,
            BinOp::Eq | BinOp::NotEq => 4,
            BinOp::Lt | BinOp::LtEq | BinOp::Gt | BinOp::GtEq => 5,
            BinOp::Add | BinOp::Sub => 6,
            BinOp::Mul | BinOp::Div | BinOp::Rem => 7,
        },
        Expr::Unary(_) => 8,
        Expr::Call(_) | Expr::FieldAccess(_) | Expr::Index(_) => 10,
        Expr::Ident(_) | Expr::Literal(_) => 11,
    }
}

fn bin_op_text(op: BinOp) -> &'static str {
    match op {
        BinOp::Or => "or",
        BinOp::And => "and",
        BinOp::Add => "+",
        BinOp::Sub => "-",
        BinOp::Mul => "*",
        BinOp::Div => "/",
        BinOp::Rem => "%",
        BinOp::Eq => "==",
        BinOp::NotEq => "!=",
        BinOp::Lt => "<",
        BinOp::LtEq => "<=",
        BinOp::Gt => ">",
        BinOp::GtEq => ">=",
    }
}

fn unary_op_text(op: UnaryOp) -> &'static str {
    match op {
        UnaryOp::Plus => "+",
        UnaryOp::Neg => "-",
        UnaryOp::Not => "not ",
    }
}

fn line_start_offsets(source: &str) -> Vec<usize> {
    let mut starts = vec![0];
    for (index, byte) in source.bytes().enumerate() {
        if byte == b'\n' && index + 1 < source.len() {
            starts.push(index + 1);
        }
    }
    starts
}

fn line_index_from_offset(line_starts: &[usize], offset: usize) -> usize {
    match line_starts.binary_search(&offset) {
        Ok(index) => index,
        Err(index) => index.saturating_sub(1),
    }
}

fn inline_comment(line: &str) -> Option<&str> {
    let mut in_string = false;
    let mut escaped = false;
    for (index, ch) in line.char_indices() {
        if in_string {
            if escaped {
                escaped = false;
                continue;
            }
            if ch == '\\' {
                escaped = true;
                continue;
            }
            if ch == '"' {
                in_string = false;
            }
            continue;
        }
        match ch {
            '"' => in_string = true,
            '#' => {
                if index > 0 && !line[..index].trim().is_empty() {
                    return Some(&line[index..]);
                }
                return None;
            }
            _ => {}
        }
    }
    None
}

fn escape_string(text: &str) -> String {
    text.to_owned()
}
