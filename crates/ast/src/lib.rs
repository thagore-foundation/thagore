#![no_std]
//! Arena-allocated abstract syntax tree for the Thagore compiler.

pub mod decl;
pub mod display;
pub mod expr;
pub mod node;
pub mod stmt;
pub mod types;
pub mod visitor;

pub use crate::decl::{
    Decl, DeclRef, ExternDecl, FieldDef, FlowDecl, FlowStage, FuncDecl, ImplBlock, ImportDecl,
    ImportSymbol, IntentDecl, LetDecl, Param, StructDecl,
};
pub use crate::expr::{
    AssignExpr, BinOp, BinaryExpr, CallExpr, Expr, ExprRef, FieldAccessExpr, IdentExpr, IndexExpr,
    LitExpr, Literal, UnaryExpr, UnaryOp,
};
pub use crate::node::{AstSlice, InternedStr, NodeId, Span};
pub use crate::stmt::{
    Block, BlockRef, BreakStmt, ContinueStmt, ExprStmt, ForStmt, IfStmt, ReturnStmt, Stmt,
    StmtRef, WhileStmt,
};
pub use crate::types::{GenericTypeExpr, InferTypeExpr, NamedTypeExpr, TypeExpr, TypeExprRef};
pub use crate::visitor::{
    walk_assign_expr, walk_binary_expr, walk_block, walk_call_expr, walk_decl, walk_expr,
    walk_expr_stmt, walk_extern_decl, walk_field_access_expr, walk_field_def, walk_flow_decl,
    walk_flow_stage, walk_for_stmt, walk_func_decl, walk_generic_type_expr, walk_if_stmt,
    walk_impl_block, walk_import_decl, walk_import_symbol, walk_index_expr, walk_intent_decl, walk_let_decl, walk_param,
    walk_return_stmt, walk_stmt, walk_struct_decl, walk_type_expr, walk_unary_expr,
    walk_while_stmt, walk_break_stmt, walk_continue_stmt, Visitor,
};
