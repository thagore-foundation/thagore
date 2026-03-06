//! Canonical type storage for Thagore type checking.

extern crate alloc;

use alloc::vec::Vec;
use core::fmt;
use thagore_ast::InternedStr;

/// Canonical identifier for an interned type.
#[repr(transparent)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct TypeId(u32);

impl TypeId {
    /// Creates a new type identifier from its raw numeric value.
    #[must_use]
    pub const fn new(raw: u32) -> Self {
        Self(raw)
    }

    /// Returns the raw numeric identifier.
    #[must_use]
    pub const fn as_u32(self) -> u32 {
        self.0
    }
}

impl fmt::Display for TypeId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "t{}", self.0)
    }
}

impl From<u32> for TypeId {
    fn from(value: u32) -> Self {
        Self::new(value)
    }
}

/// A field inside an interned nominal struct type.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct StructField {
    /// Field name symbol.
    pub name: InternedStr,
    /// Canonical field type.
    pub ty: TypeId,
}

/// Nominal struct type metadata.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct StructType {
    /// Struct name symbol.
    pub name: InternedStr,
    /// Field layout in declaration order.
    pub fields: Vec<StructField>,
}

/// Interned function type metadata.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FunctionType {
    /// Parameter types in declaration order.
    pub params: Vec<TypeId>,
    /// Return type.
    pub return_type: TypeId,
}

/// Canonical type variants used by the Thagore type checker.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum TypeKind {
    /// Unit type `()`.
    Unit,
    /// Sentinel type used after an earlier error.
    Unknown,
    /// Primitive `i32`.
    I32,
    /// Primitive `f64`.
    F64,
    /// Primitive `bool`.
    Bool,
    /// Primitive `str`.
    Str,
    /// Nominal struct type.
    Struct(StructType),
    /// Array element type.
    Array(TypeId),
    /// Function type.
    Function(FunctionType),
    /// Local inference variable.
    Infer(u32),
}

/// Canonical type storage with structural interning.
#[derive(Debug, Clone)]
pub struct TypeArena {
    kinds: Vec<TypeKind>,
    unit: TypeId,
    unknown: TypeId,
    i32: TypeId,
    f64: TypeId,
    bool: TypeId,
    str: TypeId,
    next_infer: u32,
}

impl Default for TypeArena {
    fn default() -> Self {
        Self::new()
    }
}

impl TypeArena {
    /// Creates a new type arena seeded with built-in types.
    #[must_use]
    pub fn new() -> Self {
        let mut kinds = Vec::new();
        let unit = Self::push_builtin(&mut kinds, TypeKind::Unit);
        let unknown = Self::push_builtin(&mut kinds, TypeKind::Unknown);
        let i32 = Self::push_builtin(&mut kinds, TypeKind::I32);
        let f64 = Self::push_builtin(&mut kinds, TypeKind::F64);
        let bool = Self::push_builtin(&mut kinds, TypeKind::Bool);
        let str = Self::push_builtin(&mut kinds, TypeKind::Str);

        Self {
            kinds,
            unit,
            unknown,
            i32,
            f64,
            bool,
            str,
            next_infer: 0,
        }
    }

    /// Resets the arena to its built-in state.
    pub fn clear(&mut self) {
        *self = Self::new();
    }

    /// Returns the number of interned types.
    #[must_use]
    pub fn len(&self) -> usize {
        self.kinds.len()
    }

    /// Returns `true` when no types are interned.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.kinds.is_empty()
    }

    /// Returns the built-in unit type.
    #[must_use]
    pub const fn unit(&self) -> TypeId {
        self.unit
    }

    /// Returns the sentinel unknown type.
    #[must_use]
    pub const fn unknown(&self) -> TypeId {
        self.unknown
    }

    /// Returns the built-in `i32` type.
    #[must_use]
    pub const fn i32(&self) -> TypeId {
        self.i32
    }

    /// Returns the built-in `f64` type.
    #[must_use]
    pub const fn f64(&self) -> TypeId {
        self.f64
    }

    /// Returns the built-in `bool` type.
    #[must_use]
    pub const fn bool(&self) -> TypeId {
        self.bool
    }

    /// Returns the built-in `str` type.
    #[must_use]
    pub const fn str(&self) -> TypeId {
        self.str
    }

    /// Returns the canonical kind for `id`.
    #[must_use]
    pub fn kind(&self, id: TypeId) -> &TypeKind {
        &self.kinds[id.as_u32() as usize]
    }

    /// Returns a mutable canonical kind for `id`.
    #[must_use]
    pub fn kind_mut(&mut self, id: TypeId) -> &mut TypeKind {
        &mut self.kinds[id.as_u32() as usize]
    }

    /// Returns `true` when `id` is a numeric primitive.
    #[must_use]
    pub fn is_numeric(&self, id: TypeId) -> bool {
        matches!(self.kind(id), TypeKind::I32 | TypeKind::F64)
    }

    /// Returns `true` when `id` is a sentinel unknown type.
    #[must_use]
    pub fn is_unknown(&self, id: TypeId) -> bool {
        matches!(self.kind(id), TypeKind::Unknown)
    }

    /// Returns `true` when `id` is an inference variable.
    #[must_use]
    pub fn is_infer(&self, id: TypeId) -> bool {
        matches!(self.kind(id), TypeKind::Infer(_))
    }

    /// Interns a fresh local inference variable.
    pub fn fresh_infer(&mut self) -> TypeId {
        let infer = self.next_infer;
        self.next_infer = self.next_infer.saturating_add(1);
        self.push(TypeKind::Infer(infer))
    }

    /// Interns or reuses an array type.
    pub fn intern_array(&mut self, element: TypeId) -> TypeId {
        self.intern(TypeKind::Array(element))
    }

    /// Reserves a nominal struct type by name.
    pub fn reserve_struct(&mut self, name: InternedStr) -> TypeId {
        if let Some((index, _)) =
            self.kinds.iter().enumerate().find(
                |(_, kind)| matches!(kind, TypeKind::Struct(struct_ty) if struct_ty.name == name),
            )
        {
            return TypeId::new(index as u32);
        }

        self.push(TypeKind::Struct(StructType {
            name,
            fields: Vec::new(),
        }))
    }

    /// Updates the fields of an already reserved struct type.
    pub fn set_struct_fields(&mut self, id: TypeId, fields: Vec<StructField>) {
        match self.kind_mut(id) {
            TypeKind::Struct(struct_ty) => {
                struct_ty.fields = fields;
            }
            _ => {}
        }
    }

    /// Interns or reuses a function type.
    pub fn intern_function(&mut self, params: Vec<TypeId>, return_type: TypeId) -> TypeId {
        self.intern(TypeKind::Function(FunctionType {
            params,
            return_type,
        }))
    }

    fn push_builtin(kinds: &mut Vec<TypeKind>, kind: TypeKind) -> TypeId {
        let id = TypeId::new(kinds.len() as u32);
        kinds.push(kind);
        id
    }

    fn push(&mut self, kind: TypeKind) -> TypeId {
        let id = TypeId::new(self.kinds.len() as u32);
        self.kinds.push(kind);
        id
    }

    fn intern(&mut self, kind: TypeKind) -> TypeId {
        if let Some((index, _)) = self
            .kinds
            .iter()
            .enumerate()
            .find(|(_, existing)| **existing == kind)
        {
            return TypeId::new(index as u32);
        }

        self.push(kind)
    }
}
