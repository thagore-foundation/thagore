//! Statement nodes for the Thagore AST.

use crate::decl::LetDecl;
use crate::expr::ExprRef;
use crate::node::{AstSlice, InternedStr, NodeId, Span};

/// Borrowed arena reference to a statement node.
pub type StmtRef<'ast> = &'ast Stmt<'ast>;

/// Borrowed arena reference to a block node.
pub type BlockRef<'ast> = &'ast Block<'ast>;

/// A statement inside a block scope.
#[derive(Debug, Clone, PartialEq)]
pub enum Stmt<'ast> {
    /// A local `let` declaration.
    Let(LetDecl<'ast>),
    /// An expression used for side effects.
    Expr(ExprStmt<'ast>),
    /// A `return` statement.
    Return(ReturnStmt<'ast>),
    /// An `if` control-flow statement.
    If(IfStmt<'ast>),
    /// A `while` loop.
    While(WhileStmt<'ast>),
    /// A `for` loop.
    For(ForStmt<'ast>),
}

impl<'ast> Stmt<'ast> {
    /// Returns the stable node id for the statement.
    #[must_use]
    pub const fn id(&self) -> NodeId {
        match self {
            Self::Let(node) => node.id,
            Self::Expr(node) => node.id,
            Self::Return(node) => node.id,
            Self::If(node) => node.id,
            Self::While(node) => node.id,
            Self::For(node) => node.id,
        }
    }

    /// Returns the source span covered by the statement.
    #[must_use]
    pub const fn span(&self) -> Span {
        match self {
            Self::Let(node) => node.span,
            Self::Expr(node) => node.span,
            Self::Return(node) => node.span,
            Self::If(node) => node.span,
            Self::While(node) => node.span,
            Self::For(node) => node.span,
        }
    }
}

/// A lexical block that owns its nested scope.
#[derive(Debug, Clone, PartialEq)]
pub struct Block<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full block.
    pub span: Span,
    /// Statements executed in order.
    pub statements: AstSlice<'ast, Stmt<'ast>>,
}

/// A statement that evaluates an expression for side effects.
#[derive(Debug, Clone, PartialEq)]
pub struct ExprStmt<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full statement.
    pub span: Span,
    /// Expression executed by the statement.
    pub expr: ExprRef<'ast>,
}

/// A return statement with an optional result value.
#[derive(Debug, Clone, PartialEq)]
pub struct ReturnStmt<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full statement.
    pub span: Span,
    /// Returned expression, when present.
    pub value: Option<ExprRef<'ast>>,
}

/// An `if` statement with an optional `else` block.
#[derive(Debug, Clone, PartialEq)]
pub struct IfStmt<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full statement.
    pub span: Span,
    /// Condition expression.
    pub condition: ExprRef<'ast>,
    /// Block executed when the condition is true.
    pub then_block: BlockRef<'ast>,
    /// Block executed when the condition is false.
    pub else_block: Option<BlockRef<'ast>>,
}

/// A `while` loop statement.
#[derive(Debug, Clone, PartialEq)]
pub struct WhileStmt<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full statement.
    pub span: Span,
    /// Loop condition.
    pub condition: ExprRef<'ast>,
    /// Loop body block.
    pub body: BlockRef<'ast>,
}

/// A `for` loop statement.
#[derive(Debug, Clone, PartialEq)]
pub struct ForStmt<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full statement.
    pub span: Span,
    /// Loop binding name.
    pub binding: InternedStr,
    /// Iterator expression.
    pub iterator: ExprRef<'ast>,
    /// Loop body block.
    pub body: BlockRef<'ast>,
}
