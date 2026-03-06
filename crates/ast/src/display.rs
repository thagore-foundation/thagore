//! `Display` implementations for Thagore AST nodes.

use core::fmt;

use crate::decl::{
    Decl, ExternDecl, FieldDef, FlowDecl, FlowStage, FuncDecl, ImplBlock, ImportDecl,
    ImportSymbol, IntentDecl, LetDecl, Param, StructDecl,
};
use crate::expr::{
    AssignExpr, BinOp, BinaryExpr, CallExpr, Expr, FieldAccessExpr, IdentExpr, IndexExpr, LitExpr,
    Literal, UnaryExpr, UnaryOp,
};
use crate::node::InternedStr;
use crate::stmt::{
    Block, BreakStmt, ContinueStmt, ExprStmt, ForStmt, IfStmt, ReturnStmt, Stmt, WhileStmt,
};
use crate::types::{GenericTypeExpr, InferTypeExpr, NamedTypeExpr, TypeExpr};

const INDENT: &str = "    ";

fn write_indent(f: &mut fmt::Formatter<'_>, level: usize) -> fmt::Result {
    for _ in 0..level {
        f.write_str(INDENT)?;
    }
    Ok(())
}

fn write_symbol(f: &mut fmt::Formatter<'_>, symbol: InternedStr) -> fmt::Result {
    write!(f, "sym_{}", symbol.as_u32())
}

fn write_string_symbol(f: &mut fmt::Formatter<'_>, symbol: InternedStr) -> fmt::Result {
    write!(f, "\"str_{}\"", symbol.as_u32())
}

fn expr_precedence(expr: &Expr<'_>) -> u8 {
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
        Expr::Call(_) | Expr::FieldAccess(_) | Expr::Index(_) => 9,
        Expr::Ident(_) | Expr::Literal(_) => 10,
    }
}

fn fmt_expr(f: &mut fmt::Formatter<'_>, expr: &Expr<'_>, parent_precedence: u8) -> fmt::Result {
    let precedence = expr_precedence(expr);
    let needs_parens = precedence < parent_precedence;
    if needs_parens {
        f.write_str("(")?;
    }

    match expr {
        Expr::Binary(node) => fmt_binary_expr(f, node)?,
        Expr::Unary(node) => fmt_unary_expr(f, node)?,
        Expr::Call(node) => fmt_call_expr(f, node)?,
        Expr::FieldAccess(node) => fmt_field_access_expr(f, node)?,
        Expr::Index(node) => fmt_index_expr(f, node)?,
        Expr::Ident(node) => fmt_ident_expr(f, node)?,
        Expr::Literal(node) => fmt_lit_expr(f, node)?,
        Expr::Assign(node) => fmt_assign_expr(f, node)?,
    }

    if needs_parens {
        f.write_str(")")?;
    }
    Ok(())
}

fn fmt_binary_expr(f: &mut fmt::Formatter<'_>, expr: &BinaryExpr<'_>) -> fmt::Result {
    let precedence = match expr.op {
        BinOp::Or => 2,
        BinOp::And => 3,
        BinOp::Eq | BinOp::NotEq => 4,
        BinOp::Lt | BinOp::LtEq | BinOp::Gt | BinOp::GtEq => 5,
        BinOp::Add | BinOp::Sub => 6,
        BinOp::Mul | BinOp::Div | BinOp::Rem => 7,
    };

    fmt_expr(f, expr.left, precedence)?;
    write!(f, " {} ", expr.op)?;
    fmt_expr(f, expr.right, precedence + 1)
}

fn fmt_unary_expr(f: &mut fmt::Formatter<'_>, expr: &UnaryExpr<'_>) -> fmt::Result {
    write!(f, "{}", expr.op)?;
    fmt_expr(f, expr.operand, 5)
}

fn fmt_call_expr(f: &mut fmt::Formatter<'_>, expr: &CallExpr<'_>) -> fmt::Result {
    fmt_expr(f, expr.callee, 6)?;
    f.write_str("(")?;
    for (index, arg) in expr.args.iter().enumerate() {
        if index > 0 {
            f.write_str(", ")?;
        }
        fmt_expr(f, arg, 0)?;
    }
    f.write_str(")")
}

fn fmt_field_access_expr(f: &mut fmt::Formatter<'_>, expr: &FieldAccessExpr<'_>) -> fmt::Result {
    fmt_expr(f, expr.object, 6)?;
    f.write_str(".")?;
    write_symbol(f, expr.field)
}

fn fmt_index_expr(f: &mut fmt::Formatter<'_>, expr: &IndexExpr<'_>) -> fmt::Result {
    fmt_expr(f, expr.object, 6)?;
    f.write_str("[")?;
    fmt_expr(f, expr.index, 0)?;
    f.write_str("]")
}

fn fmt_ident_expr(f: &mut fmt::Formatter<'_>, expr: &IdentExpr) -> fmt::Result {
    write_symbol(f, expr.name)
}

fn fmt_lit_expr(f: &mut fmt::Formatter<'_>, expr: &LitExpr) -> fmt::Result {
    write!(f, "{}", expr.literal)
}

fn fmt_assign_expr(f: &mut fmt::Formatter<'_>, expr: &AssignExpr<'_>) -> fmt::Result {
    fmt_expr(f, expr.target, 2)?;
    f.write_str(" = ")?;
    fmt_expr(f, expr.value, 1)
}

fn fmt_type_expr(f: &mut fmt::Formatter<'_>, ty: &TypeExpr<'_>) -> fmt::Result {
    match ty {
        TypeExpr::Named(node) => fmt_named_type_expr(f, node),
        TypeExpr::Generic(node) => fmt_generic_type_expr(f, node),
        TypeExpr::Infer(node) => fmt_infer_type_expr(f, node),
    }
}

fn fmt_named_type_expr(f: &mut fmt::Formatter<'_>, ty: &NamedTypeExpr) -> fmt::Result {
    write_symbol(f, ty.name)
}

fn fmt_generic_type_expr(f: &mut fmt::Formatter<'_>, ty: &GenericTypeExpr<'_>) -> fmt::Result {
    write_symbol(f, ty.name)?;
    f.write_str("[")?;
    for (index, arg) in ty.args.iter().enumerate() {
        if index > 0 {
            f.write_str(", ")?;
        }
        fmt_type_expr(f, arg)?;
    }
    f.write_str("]")
}

fn fmt_infer_type_expr(f: &mut fmt::Formatter<'_>, _ty: &InferTypeExpr) -> fmt::Result {
    f.write_str("_")
}

fn fmt_param(f: &mut fmt::Formatter<'_>, param: &Param<'_>) -> fmt::Result {
    write_symbol(f, param.name)?;
    f.write_str(": ")?;
    fmt_type_expr(f, param.ty)
}

fn fmt_field_def(f: &mut fmt::Formatter<'_>, field: &FieldDef<'_>, indent: usize) -> fmt::Result {
    write_indent(f, indent)?;
    write_symbol(f, field.name)?;
    f.write_str(": ")?;
    fmt_type_expr(f, field.ty)
}

fn fmt_stmt(f: &mut fmt::Formatter<'_>, stmt: &Stmt<'_>, indent: usize) -> fmt::Result {
    match stmt {
        Stmt::Let(node) => fmt_let_decl(f, node, indent),
        Stmt::Expr(node) => fmt_expr_stmt(f, node, indent),
        Stmt::Return(node) => fmt_return_stmt(f, node, indent),
        Stmt::If(node) => fmt_if_stmt(f, node, indent),
        Stmt::While(node) => fmt_while_stmt(f, node, indent),
        Stmt::For(node) => fmt_for_stmt(f, node, indent),
        Stmt::Break(node) => fmt_break_stmt(f, node, indent),
        Stmt::Continue(node) => fmt_continue_stmt(f, node, indent),
    }
}

fn fmt_block(f: &mut fmt::Formatter<'_>, block: &Block<'_>, indent: usize) -> fmt::Result {
    if block.statements.is_empty() {
        write_indent(f, indent)?;
        return f.write_str("# empty");
    }

    for (index, stmt) in block.statements.iter().enumerate() {
        if index > 0 {
            f.write_str("\n")?;
        }
        fmt_stmt(f, stmt, indent)?;
    }
    Ok(())
}

fn fmt_expr_stmt(f: &mut fmt::Formatter<'_>, stmt: &ExprStmt<'_>, indent: usize) -> fmt::Result {
    write_indent(f, indent)?;
    fmt_expr(f, stmt.expr, 0)
}

fn fmt_return_stmt(
    f: &mut fmt::Formatter<'_>,
    stmt: &ReturnStmt<'_>,
    indent: usize,
) -> fmt::Result {
    write_indent(f, indent)?;
    f.write_str("return")?;
    if let Some(value) = stmt.value {
        f.write_str(" ")?;
        fmt_expr(f, value, 0)?;
    }
    Ok(())
}

fn fmt_if_stmt(f: &mut fmt::Formatter<'_>, stmt: &IfStmt<'_>, indent: usize) -> fmt::Result {
    write_indent(f, indent)?;
    f.write_str("if (")?;
    fmt_expr(f, stmt.condition, 0)?;
    f.write_str("):\n")?;
    fmt_block(f, stmt.then_block, indent + 1)?;
    if let Some(else_block) = stmt.else_block {
        f.write_str("\n")?;
        write_indent(f, indent)?;
        f.write_str("else:\n")?;
        fmt_block(f, else_block, indent + 1)?;
    }
    Ok(())
}

fn fmt_while_stmt(f: &mut fmt::Formatter<'_>, stmt: &WhileStmt<'_>, indent: usize) -> fmt::Result {
    write_indent(f, indent)?;
    f.write_str("while (")?;
    fmt_expr(f, stmt.condition, 0)?;
    f.write_str("):\n")?;
    fmt_block(f, stmt.body, indent + 1)
}

fn fmt_for_stmt(f: &mut fmt::Formatter<'_>, stmt: &ForStmt<'_>, indent: usize) -> fmt::Result {
    write_indent(f, indent)?;
    f.write_str("for ")?;
    write_symbol(f, stmt.binding)?;
    f.write_str(" in ")?;
    fmt_expr(f, stmt.iterator, 0)?;
    f.write_str(":\n")?;
    fmt_block(f, stmt.body, indent + 1)
}

fn fmt_break_stmt(f: &mut fmt::Formatter<'_>, _stmt: &BreakStmt, indent: usize) -> fmt::Result {
    write_indent(f, indent)?;
    f.write_str("break")
}

fn fmt_continue_stmt(
    f: &mut fmt::Formatter<'_>,
    _stmt: &ContinueStmt,
    indent: usize,
) -> fmt::Result {
    write_indent(f, indent)?;
    f.write_str("continue")
}

fn fmt_decl(f: &mut fmt::Formatter<'_>, decl: &Decl<'_>, indent: usize) -> fmt::Result {
    match decl {
        Decl::Func(node) => fmt_func_decl(f, node, indent),
        Decl::Let(node) => fmt_let_decl(f, node, indent),
        Decl::Struct(node) => fmt_struct_decl(f, node, indent),
        Decl::Impl(node) => fmt_impl_block(f, node, indent),
        Decl::Import(node) => fmt_import_decl(f, node, indent),
        Decl::Extern(node) => fmt_extern_decl(f, node, indent),
        Decl::Intent(node) => fmt_intent_decl(f, node, indent),
        Decl::Flow(node) => fmt_flow_decl(f, node, indent),
    }
}

fn fmt_func_decl(f: &mut fmt::Formatter<'_>, decl: &FuncDecl<'_>, indent: usize) -> fmt::Result {
    write_indent(f, indent)?;
    f.write_str("func ")?;
    write_symbol(f, decl.name)?;
    f.write_str("(")?;
    for (index, param) in decl.params.iter().enumerate() {
        if index > 0 {
            f.write_str(", ")?;
        }
        fmt_param(f, param)?;
    }
    f.write_str(")")?;
    if let Some(return_type) = decl.return_type {
        f.write_str(" -> ")?;
        fmt_type_expr(f, return_type)?;
    }
    f.write_str(":\n")?;
    fmt_block(f, decl.body, indent + 1)
}

fn fmt_let_decl(f: &mut fmt::Formatter<'_>, decl: &LetDecl<'_>, indent: usize) -> fmt::Result {
    write_indent(f, indent)?;
    f.write_str("let ")?;
    write_symbol(f, decl.name)?;
    if let Some(ty) = decl.ty {
        f.write_str(": ")?;
        fmt_type_expr(f, ty)?;
    }
    f.write_str(" = ")?;
    fmt_expr(f, decl.initializer, 0)
}

fn fmt_struct_decl(
    f: &mut fmt::Formatter<'_>,
    decl: &StructDecl<'_>,
    indent: usize,
) -> fmt::Result {
    write_indent(f, indent)?;
    f.write_str("struct ")?;
    write_symbol(f, decl.name)?;
    f.write_str(":\n")?;
    if decl.fields.is_empty() {
        write_indent(f, indent + 1)?;
        f.write_str("# empty")
    } else {
        for (index, field) in decl.fields.iter().enumerate() {
            if index > 0 {
                f.write_str("\n")?;
            }
            fmt_field_def(f, field, indent + 1)?;
        }
        Ok(())
    }
}

fn fmt_impl_block(f: &mut fmt::Formatter<'_>, decl: &ImplBlock<'_>, indent: usize) -> fmt::Result {
    write_indent(f, indent)?;
    f.write_str("impl ")?;
    write_symbol(f, decl.target)?;
    f.write_str(":\n")?;
    if decl.methods.is_empty() {
        write_indent(f, indent + 1)?;
        f.write_str("# empty")
    } else {
        for (index, method) in decl.methods.iter().enumerate() {
            if index > 0 {
                f.write_str("\n\n")?;
            }
            fmt_func_decl(f, method, indent + 1)?;
        }
        Ok(())
    }
}

fn fmt_import_decl(
    f: &mut fmt::Formatter<'_>,
    decl: &ImportDecl<'_>,
    indent: usize,
) -> fmt::Result {
    write_indent(f, indent)?;
    if decl.is_from {
        f.write_str("from ")?;
    } else {
        f.write_str("import ")?;
    }
    for _ in 0..decl.relative_level {
        f.write_str(".")?;
    }
    if decl.relative_level > 0 && !decl.path_segments.is_empty() {
        f.write_str(".")?;
    }
    for (index, segment) in decl.path_segments.iter().enumerate() {
        if index > 0 {
            f.write_str(".")?;
        }
        write_symbol(f, *segment)?;
    }
    if decl.is_from {
        f.write_str(" import ")?;
        for (index, symbol) in decl.symbols.iter().enumerate() {
            if index > 0 {
                f.write_str(", ")?;
            }
            fmt_import_symbol(f, symbol)?;
        }
    } else {
        if let Some(alias) = decl.alias {
            f.write_str(" as ")?;
            write_symbol(f, alias)?;
        }
        if decl.include_all {
            f.write_str(" include all")?;
        }
    }
    if decl.is_from && decl.include_all {
        f.write_str(" include all")?;
    }
    Ok(())
}

fn fmt_import_symbol(f: &mut fmt::Formatter<'_>, symbol: &ImportSymbol) -> fmt::Result {
    write_symbol(f, symbol.name)?;
    if let Some(alias) = symbol.alias {
        f.write_str(" as ")?;
        write_symbol(f, alias)?;
    }
    Ok(())
}

fn fmt_extern_decl(
    f: &mut fmt::Formatter<'_>,
    decl: &ExternDecl<'_>,
    indent: usize,
) -> fmt::Result {
    write_indent(f, indent)?;
    f.write_str("extern func ")?;
    write_symbol(f, decl.name)?;
    f.write_str("(")?;
    for (index, param) in decl.params.iter().enumerate() {
        if index > 0 {
            f.write_str(", ")?;
        }
        fmt_param(f, param)?;
    }
    f.write_str(") -> ")?;
    fmt_type_expr(f, decl.return_type)
}

fn fmt_intent_decl(
    f: &mut fmt::Formatter<'_>,
    decl: &IntentDecl<'_>,
    indent: usize,
) -> fmt::Result {
    write_indent(f, indent)?;
    f.write_str("intent ")?;
    write_symbol(f, decl.name)?;
    f.write_str(":\n")?;

    for constraint in decl.constraints {
        write_indent(f, indent + 1)?;
        fmt_expr(f, constraint, 0)?;
        f.write_str("\n")?;
    }

    write_indent(f, indent + 1)?;
    f.write_str("body:\n")?;
    fmt_block(f, decl.body, indent + 2)
}

fn fmt_flow_decl(f: &mut fmt::Formatter<'_>, decl: &FlowDecl<'_>, indent: usize) -> fmt::Result {
    write_indent(f, indent)?;
    f.write_str("flow ")?;
    write_symbol(f, decl.name)?;
    f.write_str(":\n")?;

    let mut wrote_body = false;
    for stage in decl.stages {
        if wrote_body {
            f.write_str("\n")?;
        }
        fmt_flow_stage(f, stage, indent + 1)?;
        wrote_body = true;
    }

    if let Some(compensation) = decl.compensation {
        if wrote_body {
            f.write_str("\n")?;
        }
        write_indent(f, indent + 1)?;
        f.write_str("compensate:\n")?;
        fmt_block(f, compensation, indent + 2)?;
        wrote_body = true;
    }

    if !wrote_body {
        write_indent(f, indent + 1)?;
        f.write_str("# empty")?;
    }

    Ok(())
}

fn fmt_flow_stage(f: &mut fmt::Formatter<'_>, stage: &FlowStage<'_>, indent: usize) -> fmt::Result {
    write_indent(f, indent)?;
    f.write_str("stage ")?;
    write_symbol(f, stage.name)?;
    f.write_str(":\n")?;
    fmt_block(f, stage.body, indent + 1)
}

impl fmt::Display for Decl<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_decl(f, self, 0)
    }
}

impl fmt::Display for FuncDecl<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_func_decl(f, self, 0)
    }
}

impl fmt::Display for LetDecl<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_let_decl(f, self, 0)
    }
}

impl fmt::Display for StructDecl<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_struct_decl(f, self, 0)
    }
}

impl fmt::Display for ImplBlock<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_impl_block(f, self, 0)
    }
}

impl fmt::Display for ImportDecl<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_import_decl(f, self, 0)
    }
}

impl fmt::Display for ExternDecl<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_extern_decl(f, self, 0)
    }
}

impl fmt::Display for IntentDecl<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_intent_decl(f, self, 0)
    }
}

impl fmt::Display for FlowDecl<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_flow_decl(f, self, 0)
    }
}

impl fmt::Display for Param<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_param(f, self)
    }
}

impl fmt::Display for FieldDef<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_field_def(f, self, 0)
    }
}

impl fmt::Display for FlowStage<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_flow_stage(f, self, 0)
    }
}

impl fmt::Display for Stmt<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_stmt(f, self, 0)
    }
}

impl fmt::Display for Block<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_block(f, self, 0)
    }
}

impl fmt::Display for ExprStmt<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_expr_stmt(f, self, 0)
    }
}

impl fmt::Display for ReturnStmt<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_return_stmt(f, self, 0)
    }
}

impl fmt::Display for IfStmt<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_if_stmt(f, self, 0)
    }
}

impl fmt::Display for WhileStmt<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_while_stmt(f, self, 0)
    }
}

impl fmt::Display for ForStmt<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_for_stmt(f, self, 0)
    }
}

impl fmt::Display for Expr<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_expr(f, self, 0)
    }
}

impl fmt::Display for BinaryExpr<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_binary_expr(f, self)
    }
}

impl fmt::Display for UnaryExpr<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_unary_expr(f, self)
    }
}

impl fmt::Display for CallExpr<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_call_expr(f, self)
    }
}

impl fmt::Display for FieldAccessExpr<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_field_access_expr(f, self)
    }
}

impl fmt::Display for IndexExpr<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_index_expr(f, self)
    }
}

impl fmt::Display for IdentExpr {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_ident_expr(f, self)
    }
}

impl fmt::Display for LitExpr {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_lit_expr(f, self)
    }
}

impl fmt::Display for AssignExpr<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_assign_expr(f, self)
    }
}

impl fmt::Display for Literal {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Int(value) => write!(f, "{value}"),
            Self::Float(value) => write!(f, "{value:?}"),
            Self::Bool(value) => write!(f, "{value}"),
            Self::Str(value) => write_string_symbol(f, *value),
        }
    }
}

impl fmt::Display for BinOp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            Self::Or => "or",
            Self::And => "and",
            Self::Add => "+",
            Self::Sub => "-",
            Self::Mul => "*",
            Self::Div => "/",
            Self::Rem => "%",
            Self::Eq => "==",
            Self::NotEq => "!=",
            Self::Lt => "<",
            Self::LtEq => "<=",
            Self::Gt => ">",
            Self::GtEq => ">=",
        })
    }
}

impl fmt::Display for UnaryOp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            Self::Plus => "+",
            Self::Neg => "-",
            Self::Not => "!",
        })
    }
}

impl fmt::Display for TypeExpr<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_type_expr(f, self)
    }
}

impl fmt::Display for NamedTypeExpr {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_named_type_expr(f, self)
    }
}

impl fmt::Display for GenericTypeExpr<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_generic_type_expr(f, self)
    }
}

impl fmt::Display for InferTypeExpr {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt_infer_type_expr(f, self)
    }
}
