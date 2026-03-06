//! Type expression nodes for the Thagore AST.

use crate::node::{AstSlice, InternedStr, NodeId, Span};

/// Borrowed arena reference to a type expression node.
pub type TypeExprRef<'ast> = &'ast TypeExpr<'ast>;

/// A type expression in Thagore source.
#[derive(Debug, Clone, PartialEq)]
pub enum TypeExpr<'ast> {
    /// A named type such as `i32` or `Result`.
    Named(NamedTypeExpr),
    /// A generic type application such as `Option[i32]`.
    Generic(GenericTypeExpr<'ast>),
    /// An inferred type placeholder.
    Infer(InferTypeExpr),
}

impl<'ast> TypeExpr<'ast> {
    /// Returns the stable node id for the type expression.
    #[must_use]
    pub const fn id(&self) -> NodeId {
        match self {
            Self::Named(node) => node.id,
            Self::Generic(node) => node.id,
            Self::Infer(node) => node.id,
        }
    }

    /// Returns the source span covered by the type expression.
    #[must_use]
    pub const fn span(&self) -> Span {
        match self {
            Self::Named(node) => node.span,
            Self::Generic(node) => node.span,
            Self::Infer(node) => node.span,
        }
    }
}

/// A named type reference such as `str` or `User`.
#[derive(Debug, Clone, PartialEq)]
pub struct NamedTypeExpr {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the type name.
    pub span: Span,
    /// Interned symbol for the type name.
    pub name: InternedStr,
}

/// A generic type application such as `Map[str, i32]`.
#[derive(Debug, Clone, PartialEq)]
pub struct GenericTypeExpr<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full generic type expression.
    pub span: Span,
    /// Interned base type name.
    pub name: InternedStr,
    /// Generic argument list stored in the arena.
    pub args: AstSlice<'ast, TypeExprRef<'ast>>,
}

/// An inferred type placeholder represented by `_`.
#[derive(Debug, Clone, PartialEq)]
pub struct InferTypeExpr {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the placeholder token.
    pub span: Span,
}
