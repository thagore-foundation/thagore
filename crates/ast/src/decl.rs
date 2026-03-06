//! Declaration nodes for the Thagore AST.

use crate::expr::ExprRef;
use crate::node::{AstSlice, InternedStr, NodeId, Span};
use crate::stmt::BlockRef;
use crate::types::{TypeExprRef, TypeParam};

/// Borrowed arena reference to a declaration node.
pub type DeclRef<'ast> = &'ast Decl<'ast>;

/// A top-level declaration in a Thagore source file.
#[derive(Debug, Clone, PartialEq)]
pub enum Decl<'ast> {
    /// A function declaration.
    Func(FuncDecl<'ast>),
    /// A generic function declaration.
    GenericFunc(GenericFuncDecl<'ast>),
    /// A `let` declaration.
    Let(LetDecl<'ast>),
    /// A top-level constant declaration.
    Const(ConstDecl<'ast>),
    /// A struct declaration.
    Struct(StructDecl<'ast>),
    /// A generic struct declaration.
    GenericStruct(GenericStructDecl<'ast>),
    /// An implementation block.
    Impl(ImplBlock<'ast>),
    /// A generic implementation block.
    GenericImpl(GenericImplBlock<'ast>),
    /// An import declaration.
    Import(ImportDecl<'ast>),
    /// An external function declaration.
    Extern(ExternDecl<'ast>),
    /// A Thagore intent declaration.
    Intent(IntentDecl<'ast>),
    /// A Thagore flow declaration.
    Flow(FlowDecl<'ast>),
}

impl<'ast> Decl<'ast> {
    /// Returns the stable node id for the declaration.
    #[must_use]
    pub const fn id(&self) -> NodeId {
        match self {
            Self::Func(node) => node.id,
            Self::GenericFunc(node) => node.id,
            Self::Let(node) => node.id,
            Self::Const(node) => node.id,
            Self::Struct(node) => node.id,
            Self::GenericStruct(node) => node.id,
            Self::Impl(node) => node.id,
            Self::GenericImpl(node) => node.id,
            Self::Import(node) => node.id,
            Self::Extern(node) => node.id,
            Self::Intent(node) => node.id,
            Self::Flow(node) => node.id,
        }
    }

    /// Returns the source span covered by the declaration.
    #[must_use]
    pub const fn span(&self) -> Span {
        match self {
            Self::Func(node) => node.span,
            Self::GenericFunc(node) => node.span,
            Self::Let(node) => node.span,
            Self::Const(node) => node.span,
            Self::Struct(node) => node.span,
            Self::GenericStruct(node) => node.span,
            Self::Impl(node) => node.span,
            Self::GenericImpl(node) => node.span,
            Self::Import(node) => node.span,
            Self::Extern(node) => node.span,
            Self::Intent(node) => node.span,
            Self::Flow(node) => node.span,
        }
    }
}

/// A typed function parameter.
#[derive(Debug, Clone, PartialEq)]
pub struct Param<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the parameter.
    pub span: Span,
    /// Interned parameter name.
    pub name: InternedStr,
    /// Parameter type annotation.
    pub ty: TypeExprRef<'ast>,
}

/// A field definition inside a struct declaration.
#[derive(Debug, Clone, PartialEq)]
pub struct FieldDef<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the field definition.
    pub span: Span,
    /// Interned field name.
    pub name: InternedStr,
    /// Declared field type.
    pub ty: TypeExprRef<'ast>,
}

/// A function declaration.
#[derive(Debug, Clone, PartialEq)]
pub struct FuncDecl<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full declaration.
    pub span: Span,
    /// Interned function name.
    pub name: InternedStr,
    /// Parameters in source order.
    pub params: AstSlice<'ast, Param<'ast>>,
    /// Optional return type annotation.
    pub return_type: Option<TypeExprRef<'ast>>,
    /// Function body block.
    pub body: BlockRef<'ast>,
}

/// A generic function declaration with one or more type parameters.
#[derive(Debug, Clone, PartialEq)]
pub struct GenericFuncDecl<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full declaration.
    pub span: Span,
    /// Interned function name.
    pub name: InternedStr,
    /// Declared type parameters in source order.
    pub type_params: AstSlice<'ast, TypeParam<'ast>>,
    /// Runtime parameters in source order.
    pub params: AstSlice<'ast, Param<'ast>>,
    /// Optional return type annotation.
    pub return_type: Option<TypeExprRef<'ast>>,
    /// Function body block.
    pub body: BlockRef<'ast>,
}

/// A `let` declaration.
#[derive(Debug, Clone, PartialEq)]
pub struct LetDecl<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full declaration.
    pub span: Span,
    /// Interned binding name.
    pub name: InternedStr,
    /// Optional explicit type annotation.
    pub ty: Option<TypeExprRef<'ast>>,
    /// Initializer expression.
    pub initializer: ExprRef<'ast>,
}

/// A top-level `const` declaration.
#[derive(Debug, Clone, PartialEq)]
pub struct ConstDecl<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full declaration.
    pub span: Span,
    /// Interned constant name.
    pub name: InternedStr,
    /// Required type annotation.
    pub type_ann: TypeExprRef<'ast>,
    /// Constant initializer expression.
    pub value: ExprRef<'ast>,
}

/// A struct declaration.
#[derive(Debug, Clone, PartialEq)]
pub struct StructDecl<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full declaration.
    pub span: Span,
    /// Interned struct name.
    pub name: InternedStr,
    /// Field definitions.
    pub fields: AstSlice<'ast, FieldDef<'ast>>,
}

/// A generic struct declaration.
#[derive(Debug, Clone, PartialEq)]
pub struct GenericStructDecl<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full declaration.
    pub span: Span,
    /// Interned struct name.
    pub name: InternedStr,
    /// Declared type parameters.
    pub type_params: AstSlice<'ast, TypeParam<'ast>>,
    /// Field definitions.
    pub fields: AstSlice<'ast, FieldDef<'ast>>,
}

/// An `impl` block containing methods for a struct.
#[derive(Debug, Clone, PartialEq)]
pub struct ImplBlock<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full declaration.
    pub span: Span,
    /// Target struct name.
    pub target: InternedStr,
    /// Method declarations.
    pub methods: AstSlice<'ast, FuncDecl<'ast>>,
}

/// A generic `impl` block containing methods specialized by type parameters.
#[derive(Debug, Clone, PartialEq)]
pub struct GenericImplBlock<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full declaration.
    pub span: Span,
    /// Target struct name.
    pub target: InternedStr,
    /// Declared type parameters.
    pub type_params: AstSlice<'ast, TypeParam<'ast>>,
    /// Method declarations.
    pub methods: AstSlice<'ast, FuncDecl<'ast>>,
}

/// An import declaration composed of interned path segments.
#[derive(Debug, Clone, PartialEq)]
pub struct ImportDecl<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full declaration.
    pub span: Span,
    /// Number of leading relative path levels. `0` means a non-relative import.
    pub relative_level: u8,
    /// Imported module path.
    pub path_segments: AstSlice<'ast, InternedStr>,
    /// Imported symbols for `from ... import ...` declarations.
    pub symbols: AstSlice<'ast, ImportSymbol>,
    /// `true` when the declaration uses `from ... import ...` syntax.
    pub is_from: bool,
    /// `true` when the declaration uses `include all`.
    pub include_all: bool,
    /// Optional local alias bound for the imported module.
    pub alias: Option<InternedStr>,
}

/// A symbol entry imported from a module, optionally under an alias.
#[derive(Debug, Clone, PartialEq)]
pub struct ImportSymbol {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the symbol entry.
    pub span: Span,
    /// Imported symbol name.
    pub name: InternedStr,
    /// Optional local alias for the symbol.
    pub alias: Option<InternedStr>,
}

/// An external function declaration for FFI bindings.
#[derive(Debug, Clone, PartialEq)]
pub struct ExternDecl<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full declaration.
    pub span: Span,
    /// Interned external symbol name.
    pub name: InternedStr,
    /// Extern function parameters.
    pub params: AstSlice<'ast, Param<'ast>>,
    /// Required return type annotation.
    pub return_type: TypeExprRef<'ast>,
}

/// A Thagore intent declaration.
#[derive(Debug, Clone, PartialEq)]
pub struct IntentDecl<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full declaration.
    pub span: Span,
    /// Interned intent name.
    pub name: InternedStr,
    /// Constraint expressions attached to the intent.
    pub constraints: AstSlice<'ast, ExprRef<'ast>>,
    /// Intent body block.
    pub body: BlockRef<'ast>,
}

/// A Thagore flow declaration.
#[derive(Debug, Clone, PartialEq)]
pub struct FlowDecl<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full declaration.
    pub span: Span,
    /// Interned flow name.
    pub name: InternedStr,
    /// Flow stages in execution order.
    pub stages: AstSlice<'ast, FlowStage<'ast>>,
    /// Optional compensation block.
    pub compensation: Option<BlockRef<'ast>>,
}

/// A named stage within a flow declaration.
#[derive(Debug, Clone, PartialEq)]
pub struct FlowStage<'ast> {
    /// Stable identity for this AST node.
    pub id: NodeId,
    /// Source span for the full stage.
    pub span: Span,
    /// Interned stage name.
    pub name: InternedStr,
    /// Stage body block.
    pub body: BlockRef<'ast>,
}
