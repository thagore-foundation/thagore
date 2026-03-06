//! Visitor support for walking the Thagore AST.

use crate::decl::{
    ConstDecl, Decl, DeclRef, ExternDecl, FieldDef, FlowDecl, FlowStage, FuncDecl,
    GenericFuncDecl, GenericImplBlock, GenericStructDecl, ImplBlock, ImportDecl, ImportSymbol,
    IntentDecl, LetDecl, Param, StructDecl,
};
use crate::expr::{
    AssignExpr, BinaryExpr, CallExpr, Expr, ExprRef, FieldAccessExpr, IdentExpr, IndexExpr,
    LitExpr, UnaryExpr,
};
use crate::stmt::{
    BlockRef, BreakStmt, ContinueStmt, ExprStmt, ForStmt, IfStmt, ReturnStmt, Stmt, StmtRef,
    WhileStmt,
};
use crate::types::{
    Constraint, GenericTypeExpr, InferTypeExpr, NamedTypeExpr, TypeExpr, TypeExprRef, TypeParam,
};

/// A visitor over arena-allocated AST nodes.
///
/// All methods default to no-op. Use the `walk_*` helpers to perform recursive
/// traversal while dispatching to the matching visit hook for each node.
pub trait Visitor<'ast> {
    /// Visits an abstract declaration node.
    fn visit_decl(&mut self, _decl: DeclRef<'ast>) {}
    /// Visits a function declaration.
    fn visit_func_decl(&mut self, _decl: &'ast FuncDecl<'ast>) {}
    /// Visits a generic function declaration.
    fn visit_generic_func_decl(&mut self, _decl: &'ast GenericFuncDecl<'ast>) {}
    /// Visits a `let` declaration.
    fn visit_let_decl(&mut self, _decl: &'ast LetDecl<'ast>) {}
    /// Visits a `const` declaration.
    fn visit_const_decl(&mut self, _decl: &'ast ConstDecl<'ast>) {}
    /// Visits a struct declaration.
    fn visit_struct_decl(&mut self, _decl: &'ast StructDecl<'ast>) {}
    /// Visits a generic struct declaration.
    fn visit_generic_struct_decl(&mut self, _decl: &'ast GenericStructDecl<'ast>) {}
    /// Visits an `impl` block.
    fn visit_impl_block(&mut self, _decl: &'ast ImplBlock<'ast>) {}
    /// Visits a generic `impl` block.
    fn visit_generic_impl_block(&mut self, _decl: &'ast GenericImplBlock<'ast>) {}
    /// Visits an import declaration.
    fn visit_import_decl(&mut self, _decl: &'ast ImportDecl<'ast>) {}
    /// Visits an imported symbol entry.
    fn visit_import_symbol(&mut self, _symbol: &'ast ImportSymbol) {}
    /// Visits an extern declaration.
    fn visit_extern_decl(&mut self, _decl: &'ast ExternDecl<'ast>) {}
    /// Visits an intent declaration.
    fn visit_intent_decl(&mut self, _decl: &'ast IntentDecl<'ast>) {}
    /// Visits a flow declaration.
    fn visit_flow_decl(&mut self, _decl: &'ast FlowDecl<'ast>) {}
    /// Visits a parameter node.
    fn visit_param(&mut self, _param: &'ast Param<'ast>) {}
    /// Visits a field definition node.
    fn visit_field_def(&mut self, _field: &'ast FieldDef<'ast>) {}
    /// Visits a type parameter node.
    fn visit_type_param(&mut self, _param: &'ast TypeParam<'ast>) {}
    /// Visits a built-in constraint node.
    fn visit_constraint(&mut self, _constraint: &'ast Constraint) {}
    /// Visits a flow stage node.
    fn visit_flow_stage(&mut self, _stage: &'ast FlowStage<'ast>) {}

    /// Visits an abstract statement node.
    fn visit_stmt(&mut self, _stmt: StmtRef<'ast>) {}
    /// Visits a block node.
    fn visit_block(&mut self, _block: BlockRef<'ast>) {}
    /// Visits an expression statement.
    fn visit_expr_stmt(&mut self, _stmt: &'ast ExprStmt<'ast>) {}
    /// Visits a return statement.
    fn visit_return_stmt(&mut self, _stmt: &'ast ReturnStmt<'ast>) {}
    /// Visits an if statement.
    fn visit_if_stmt(&mut self, _stmt: &'ast IfStmt<'ast>) {}
    /// Visits a while statement.
    fn visit_while_stmt(&mut self, _stmt: &'ast WhileStmt<'ast>) {}
    /// Visits a for statement.
    fn visit_for_stmt(&mut self, _stmt: &'ast ForStmt<'ast>) {}
    /// Visits a break statement.
    fn visit_break_stmt(&mut self, _stmt: &'ast BreakStmt) {}
    /// Visits a continue statement.
    fn visit_continue_stmt(&mut self, _stmt: &'ast ContinueStmt) {}

    /// Visits an abstract expression node.
    fn visit_expr(&mut self, _expr: ExprRef<'ast>) {}
    /// Visits a binary expression.
    fn visit_binary_expr(&mut self, _expr: &'ast BinaryExpr<'ast>) {}
    /// Visits a unary expression.
    fn visit_unary_expr(&mut self, _expr: &'ast UnaryExpr<'ast>) {}
    /// Visits a call expression.
    fn visit_call_expr(&mut self, _expr: &'ast CallExpr<'ast>) {}
    /// Visits a field access expression.
    fn visit_field_access_expr(&mut self, _expr: &'ast FieldAccessExpr<'ast>) {}
    /// Visits an index expression.
    fn visit_index_expr(&mut self, _expr: &'ast IndexExpr<'ast>) {}
    /// Visits an identifier expression.
    fn visit_ident_expr(&mut self, _expr: &'ast IdentExpr) {}
    /// Visits a literal expression.
    fn visit_lit_expr(&mut self, _expr: &'ast LitExpr) {}
    /// Visits an assignment expression.
    fn visit_assign_expr(&mut self, _expr: &'ast AssignExpr<'ast>) {}

    /// Visits an abstract type expression node.
    fn visit_type_expr(&mut self, _ty: TypeExprRef<'ast>) {}
    /// Visits a named type expression.
    fn visit_named_type_expr(&mut self, _ty: &'ast NamedTypeExpr) {}
    /// Visits a generic type expression.
    fn visit_generic_type_expr(&mut self, _ty: &'ast GenericTypeExpr<'ast>) {}
    /// Visits an inferred type expression.
    fn visit_infer_type_expr(&mut self, _ty: &'ast InferTypeExpr) {}
}

/// Walks a declaration and all of its descendants.
pub fn walk_decl<'ast, V>(visitor: &mut V, decl: DeclRef<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_decl(decl);
    match decl {
        Decl::Func(node) => walk_func_decl(visitor, node),
        Decl::GenericFunc(node) => walk_generic_func_decl(visitor, node),
        Decl::Let(node) => walk_let_decl(visitor, node),
        Decl::Const(node) => walk_const_decl(visitor, node),
        Decl::Struct(node) => walk_struct_decl(visitor, node),
        Decl::GenericStruct(node) => walk_generic_struct_decl(visitor, node),
        Decl::Impl(node) => walk_impl_block(visitor, node),
        Decl::GenericImpl(node) => walk_generic_impl_block(visitor, node),
        Decl::Import(node) => walk_import_decl(visitor, node),
        Decl::Extern(node) => walk_extern_decl(visitor, node),
        Decl::Intent(node) => walk_intent_decl(visitor, node),
        Decl::Flow(node) => walk_flow_decl(visitor, node),
    }
}

/// Walks an import declaration and its imported symbol entries.
pub fn walk_import_decl<'ast, V>(visitor: &mut V, decl: &'ast ImportDecl<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_import_decl(decl);
    for symbol in decl.symbols {
        walk_import_symbol(visitor, symbol);
    }
}

/// Walks an imported symbol entry.
pub fn walk_import_symbol<'ast, V>(visitor: &mut V, symbol: &'ast ImportSymbol)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_import_symbol(symbol);
}

/// Walks a function declaration and its children.
pub fn walk_func_decl<'ast, V>(visitor: &mut V, decl: &'ast FuncDecl<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_func_decl(decl);
    for param in decl.params {
        walk_param(visitor, param);
    }
    if let Some(return_type) = decl.return_type {
        walk_type_expr(visitor, return_type);
    }
    walk_block(visitor, decl.body);
}

/// Walks a generic function declaration and its children.
pub fn walk_generic_func_decl<'ast, V>(visitor: &mut V, decl: &'ast GenericFuncDecl<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_generic_func_decl(decl);
    for type_param in decl.type_params {
        walk_type_param(visitor, type_param);
    }
    for param in decl.params {
        walk_param(visitor, param);
    }
    if let Some(return_type) = decl.return_type {
        walk_type_expr(visitor, return_type);
    }
    walk_block(visitor, decl.body);
}

/// Walks a `let` declaration and its children.
pub fn walk_let_decl<'ast, V>(visitor: &mut V, decl: &'ast LetDecl<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_let_decl(decl);
    if let Some(ty) = decl.ty {
        walk_type_expr(visitor, ty);
    }
    walk_expr(visitor, decl.initializer);
}

/// Walks a `const` declaration and its children.
pub fn walk_const_decl<'ast, V>(visitor: &mut V, decl: &'ast ConstDecl<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_const_decl(decl);
    walk_type_expr(visitor, decl.type_ann);
    walk_expr(visitor, decl.value);
}

/// Walks a struct declaration and its children.
pub fn walk_struct_decl<'ast, V>(visitor: &mut V, decl: &'ast StructDecl<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_struct_decl(decl);
    for field in decl.fields {
        walk_field_def(visitor, field);
    }
}

/// Walks a generic struct declaration and its children.
pub fn walk_generic_struct_decl<'ast, V>(
    visitor: &mut V,
    decl: &'ast GenericStructDecl<'ast>,
) where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_generic_struct_decl(decl);
    for type_param in decl.type_params {
        walk_type_param(visitor, type_param);
    }
    for field in decl.fields {
        walk_field_def(visitor, field);
    }
}

/// Walks an `impl` block and its children.
pub fn walk_impl_block<'ast, V>(visitor: &mut V, decl: &'ast ImplBlock<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_impl_block(decl);
    for method in decl.methods {
        walk_func_decl(visitor, method);
    }
}

/// Walks a generic `impl` block and its children.
pub fn walk_generic_impl_block<'ast, V>(
    visitor: &mut V,
    decl: &'ast GenericImplBlock<'ast>,
) where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_generic_impl_block(decl);
    for type_param in decl.type_params {
        walk_type_param(visitor, type_param);
    }
    for method in decl.methods {
        walk_func_decl(visitor, method);
    }
}

/// Walks an extern declaration and its children.
pub fn walk_extern_decl<'ast, V>(visitor: &mut V, decl: &'ast ExternDecl<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_extern_decl(decl);
    for param in decl.params {
        walk_param(visitor, param);
    }
    walk_type_expr(visitor, decl.return_type);
}

/// Walks an intent declaration and its children.
pub fn walk_intent_decl<'ast, V>(visitor: &mut V, decl: &'ast IntentDecl<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_intent_decl(decl);
    for constraint in decl.constraints {
        walk_expr(visitor, constraint);
    }
    walk_block(visitor, decl.body);
}

/// Walks a flow declaration and its children.
pub fn walk_flow_decl<'ast, V>(visitor: &mut V, decl: &'ast FlowDecl<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_flow_decl(decl);
    for stage in decl.stages {
        walk_flow_stage(visitor, stage);
    }
    if let Some(compensation) = decl.compensation {
        walk_block(visitor, compensation);
    }
}

/// Walks a parameter node and its children.
pub fn walk_param<'ast, V>(visitor: &mut V, param: &'ast Param<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_param(param);
    walk_type_expr(visitor, param.ty);
}

/// Walks a field definition and its children.
pub fn walk_field_def<'ast, V>(visitor: &mut V, field: &'ast FieldDef<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_field_def(field);
    walk_type_expr(visitor, field.ty);
}

/// Walks a type parameter and its constraints.
pub fn walk_type_param<'ast, V>(visitor: &mut V, param: &'ast TypeParam<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_type_param(param);
    for constraint in param.constraints {
        walk_constraint(visitor, constraint);
    }
}

/// Walks a built-in generic constraint node.
pub fn walk_constraint<'ast, V>(visitor: &mut V, constraint: &'ast Constraint)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_constraint(constraint);
}

/// Walks a flow stage and its children.
pub fn walk_flow_stage<'ast, V>(visitor: &mut V, stage: &'ast FlowStage<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_flow_stage(stage);
    walk_block(visitor, stage.body);
}

/// Walks a statement and all of its descendants.
pub fn walk_stmt<'ast, V>(visitor: &mut V, stmt: StmtRef<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_stmt(stmt);
    match stmt {
        Stmt::Let(node) => walk_let_decl(visitor, node),
        Stmt::Expr(node) => walk_expr_stmt(visitor, node),
        Stmt::Return(node) => walk_return_stmt(visitor, node),
        Stmt::If(node) => walk_if_stmt(visitor, node),
        Stmt::While(node) => walk_while_stmt(visitor, node),
        Stmt::For(node) => walk_for_stmt(visitor, node),
        Stmt::Break(node) => walk_break_stmt(visitor, node),
        Stmt::Continue(node) => walk_continue_stmt(visitor, node),
    }
}

/// Walks a block and all nested statements.
pub fn walk_block<'ast, V>(visitor: &mut V, block: BlockRef<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_block(block);
    for stmt in block.statements {
        walk_stmt(visitor, stmt);
    }
}

/// Walks an expression statement and its children.
pub fn walk_expr_stmt<'ast, V>(visitor: &mut V, stmt: &'ast ExprStmt<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_expr_stmt(stmt);
    walk_expr(visitor, stmt.expr);
}

/// Walks a return statement and its children.
pub fn walk_return_stmt<'ast, V>(visitor: &mut V, stmt: &'ast ReturnStmt<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_return_stmt(stmt);
    if let Some(value) = stmt.value {
        walk_expr(visitor, value);
    }
}

/// Walks an if statement and its children.
pub fn walk_if_stmt<'ast, V>(visitor: &mut V, stmt: &'ast IfStmt<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_if_stmt(stmt);
    walk_expr(visitor, stmt.condition);
    walk_block(visitor, stmt.then_block);
    if let Some(else_block) = stmt.else_block {
        walk_block(visitor, else_block);
    }
}

/// Walks a while statement and its children.
pub fn walk_while_stmt<'ast, V>(visitor: &mut V, stmt: &'ast WhileStmt<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_while_stmt(stmt);
    walk_expr(visitor, stmt.condition);
    walk_block(visitor, stmt.body);
}

/// Walks a for statement and its children.
pub fn walk_for_stmt<'ast, V>(visitor: &mut V, stmt: &'ast ForStmt<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_for_stmt(stmt);
    walk_expr(visitor, stmt.iterator);
    walk_block(visitor, stmt.body);
}

/// Walks a break statement.
pub fn walk_break_stmt<'ast, V>(visitor: &mut V, stmt: &'ast BreakStmt)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_break_stmt(stmt);
}

/// Walks a continue statement.
pub fn walk_continue_stmt<'ast, V>(visitor: &mut V, stmt: &'ast ContinueStmt)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_continue_stmt(stmt);
}

/// Walks an expression and all of its descendants.
pub fn walk_expr<'ast, V>(visitor: &mut V, expr: ExprRef<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_expr(expr);
    match expr {
        Expr::Binary(node) => walk_binary_expr(visitor, node),
        Expr::Unary(node) => walk_unary_expr(visitor, node),
        Expr::Call(node) => walk_call_expr(visitor, node),
        Expr::FieldAccess(node) => walk_field_access_expr(visitor, node),
        Expr::Index(node) => walk_index_expr(visitor, node),
        Expr::Ident(node) => visitor.visit_ident_expr(node),
        Expr::Literal(node) => visitor.visit_lit_expr(node),
        Expr::Assign(node) => walk_assign_expr(visitor, node),
    }
}

/// Walks a binary expression and its children.
pub fn walk_binary_expr<'ast, V>(visitor: &mut V, expr: &'ast BinaryExpr<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_binary_expr(expr);
    walk_expr(visitor, expr.left);
    walk_expr(visitor, expr.right);
}

/// Walks a unary expression and its child.
pub fn walk_unary_expr<'ast, V>(visitor: &mut V, expr: &'ast UnaryExpr<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_unary_expr(expr);
    walk_expr(visitor, expr.operand);
}

/// Walks a call expression and its children.
pub fn walk_call_expr<'ast, V>(visitor: &mut V, expr: &'ast CallExpr<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_call_expr(expr);
    walk_expr(visitor, expr.callee);
    for arg in expr.args {
        walk_expr(visitor, arg);
    }
}

/// Walks a field access expression and its child.
pub fn walk_field_access_expr<'ast, V>(visitor: &mut V, expr: &'ast FieldAccessExpr<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_field_access_expr(expr);
    walk_expr(visitor, expr.object);
}

/// Walks an index expression and its children.
pub fn walk_index_expr<'ast, V>(visitor: &mut V, expr: &'ast IndexExpr<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_index_expr(expr);
    walk_expr(visitor, expr.object);
    walk_expr(visitor, expr.index);
}

/// Walks an assignment expression and its children.
pub fn walk_assign_expr<'ast, V>(visitor: &mut V, expr: &'ast AssignExpr<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_assign_expr(expr);
    walk_expr(visitor, expr.target);
    walk_expr(visitor, expr.value);
}

/// Walks a type expression and all of its descendants.
pub fn walk_type_expr<'ast, V>(visitor: &mut V, ty: TypeExprRef<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_type_expr(ty);
    match ty {
        TypeExpr::Named(node) => visitor.visit_named_type_expr(node),
        TypeExpr::Generic(node) => walk_generic_type_expr(visitor, node),
        TypeExpr::Infer(node) => visitor.visit_infer_type_expr(node),
    }
}

/// Walks a generic type expression and its children.
pub fn walk_generic_type_expr<'ast, V>(visitor: &mut V, ty: &'ast GenericTypeExpr<'ast>)
where
    V: Visitor<'ast> + ?Sized,
{
    visitor.visit_generic_type_expr(ty);
    for arg in ty.args {
        walk_type_expr(visitor, arg);
    }
}
