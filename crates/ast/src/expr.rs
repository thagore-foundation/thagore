//! Expression nodes for the Thagore AST.

use crate::node::{AstSlice, InternedStr, NodeId, Span};

/// Borrowed arena reference to an expression node.
pub type ExprRef<'ast> = &'ast Expr<'ast>;

/// A Thagore expression.
#[derive(Debug, Clone, PartialEq)]
pub enum Expr<'ast> {
    /// A binary operator expression.
    Binary(BinaryExpr<'ast>),
    /// A unary operator expression.
    Unary(UnaryExpr<'ast>),
    /// A function or callable invocation.
    Call(CallExpr<'ast>),
    /// A field selection expression.
    FieldAccess(FieldAccessExpr<'ast>),
    /// An indexing expression.
    Index(IndexExpr<'ast>),
    /// A bare identifier.
    Ident(IdentExpr),
    /// A literal value.
    Literal(LitExpr),
    /// An assignment expression.
    Assign(AssignExpr<'ast>),
}

impl<'ast> Expr<'ast> {
    /// Returns the stable node id for the expression.
    #[must_use]
    pub const fn id(&self) -> NodeId {
        match self {
            Self::Binary(node) => node.id,
            Self::Unary(node) => node.id,
            Self::Call(node) => node.id,
            Self::FieldAccess(node) => node.id,
            Self::Index(node) => node.id,
            Self::Ident(node) => node.id,
            Self::Literal(node) => node.id,
            Self::Assign(node) => node.id,
        }
    }

    /// Returns the source span covered by the expression.
    #[must_use]
    pub const fn span(&self) -> Span {
        match self {
            Self::Binary(node) => node.span,
            Self::Unary(node) => node.span,
            Self::Call(node) => node.span,
            Self::FieldAccess(node) => node.span,
            Self::Index(node) => node.span,
            Self::Ident(node) => node.span,
            Self::Literal(node) => node.span,
            Self::Assign(node) => node.span,
        }
    }
}

/// A binary operator expression such as `a + b`.
#[derive(Debug, Clone, PartialEq)]
pub struct BinaryExpr<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full binary expression.
    pub span: Span,
    /// Left operand.
    pub left: ExprRef<'ast>,
    /// Operator token.
    pub op: BinOp,
    /// Right operand.
    pub right: ExprRef<'ast>,
}

/// A unary operator expression such as `-value`.
#[derive(Debug, Clone, PartialEq)]
pub struct UnaryExpr<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full unary expression.
    pub span: Span,
    /// Unary operator token.
    pub op: UnaryOp,
    /// Operand expression.
    pub operand: ExprRef<'ast>,
}

/// A call expression such as `f(x, y)`.
#[derive(Debug, Clone, PartialEq)]
pub struct CallExpr<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full call expression.
    pub span: Span,
    /// Callee expression.
    pub callee: ExprRef<'ast>,
    /// Call arguments stored in the arena.
    pub args: AstSlice<'ast, ExprRef<'ast>>,
}

/// A field access expression such as `value.name`.
#[derive(Debug, Clone, PartialEq)]
pub struct FieldAccessExpr<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full field access expression.
    pub span: Span,
    /// Object expression being accessed.
    pub object: ExprRef<'ast>,
    /// Interned field name.
    pub field: InternedStr,
}

/// An indexing expression such as `items[i]`.
#[derive(Debug, Clone, PartialEq)]
pub struct IndexExpr<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full indexing expression.
    pub span: Span,
    /// Indexed object expression.
    pub object: ExprRef<'ast>,
    /// Index expression.
    pub index: ExprRef<'ast>,
}

/// An identifier expression.
#[derive(Debug, Clone, PartialEq)]
pub struct IdentExpr {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the identifier token.
    pub span: Span,
    /// Interned identifier symbol.
    pub name: InternedStr,
}

/// A literal expression node.
#[derive(Debug, Clone, PartialEq)]
pub struct LitExpr {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the literal token.
    pub span: Span,
    /// Literal payload.
    pub literal: Literal,
}

/// An assignment expression such as `target = value`.
#[derive(Debug, Clone, PartialEq)]
pub struct AssignExpr<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full assignment.
    pub span: Span,
    /// Target expression receiving the value.
    pub target: ExprRef<'ast>,
    /// Assigned value expression.
    pub value: ExprRef<'ast>,
}

/// Literal values supported by Thagore.
#[derive(Debug, Clone, PartialEq)]
pub enum Literal {
    /// Signed integer literal.
    Int(i64),
    /// Double-precision floating-point literal.
    Float(f64),
    /// Boolean literal.
    Bool(bool),
    /// Interned string literal contents.
    Str(InternedStr),
}

/// Binary operators supported by expression syntax.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum BinOp {
    /// `+`
    Add,
    /// `-`
    Sub,
    /// `*`
    Mul,
    /// `/`
    Div,
    /// `%`
    Rem,
    /// `==`
    Eq,
    /// `!=`
    NotEq,
    /// `<`
    Lt,
    /// `<=`
    LtEq,
    /// `>`
    Gt,
    /// `>=`
    GtEq,
}

/// Unary operators supported by expression syntax.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum UnaryOp {
    /// Unary plus: `+value`.
    Plus,
    /// Arithmetic negation: `-value`.
    Neg,
    /// Logical negation: `!value`.
    Not,
}
