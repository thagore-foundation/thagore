//! Generic type-checking support primitives.

extern crate alloc;

use alloc::collections::BTreeMap;
use alloc::string::{String, ToString};
use alloc::vec::Vec;
use thagore_ast::{ConstraintKind, InternedStr, Span};

use crate::types::{TypeArena, TypeId, TypeKind};

/// A requested concrete instantiation of a generic declaration.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MonomorphRequest {
    /// Generic declaration symbol.
    pub generic_name: InternedStr,
    /// Concrete type arguments for the instantiation.
    pub type_args: Vec<TypeId>,
    /// Source span of the call or use site that triggered the request.
    pub call_span: Span,
}

/// A completed concrete instantiation of a generic declaration.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MonomorphResult {
    /// Mangled symbol emitted for the concrete instance.
    pub mangled_name: InternedStr,
    /// Concrete function or type identifier associated with the instance.
    pub type_id: TypeId,
}

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord)]
struct MonomorphKey {
    generic_name: InternedStr,
    type_args: Vec<TypeId>,
}

/// Deduplicated work list of generic instantiations discovered during type checking.
#[derive(Debug, Clone, Default)]
pub struct MonomorphWorkList {
    pending: Vec<MonomorphRequest>,
    done: BTreeMap<MonomorphKey, MonomorphResult>,
}

impl MonomorphWorkList {
    /// Creates an empty work list.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Removes all pending and completed instantiations.
    pub fn clear(&mut self) {
        self.pending.clear();
        self.done.clear();
    }

    /// Registers a concrete instantiation result, deduplicating by `(name, args)`.
    pub fn record(
        &mut self,
        request: MonomorphRequest,
        result: MonomorphResult,
    ) -> MonomorphResult {
        let key = MonomorphKey {
            generic_name: request.generic_name,
            type_args: request.type_args.clone(),
        };
        if let Some(existing) = self.done.get(&key) {
            return existing.clone();
        }

        self.pending.push(request);
        self.done.insert(key, result.clone());
        result
    }

    /// Returns the completed instantiation for `(name, args)`, if present.
    #[must_use]
    pub fn get(&self, generic_name: InternedStr, type_args: &[TypeId]) -> Option<&MonomorphResult> {
        self.done.get(&MonomorphKey {
            generic_name,
            type_args: type_args.to_vec(),
        })
    }

    /// Returns the pending instantiations in discovery order.
    #[must_use]
    pub fn pending(&self) -> &[MonomorphRequest] {
        &self.pending
    }
}

/// A declared generic parameter and its built-in constraints.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct GenericParamSpec {
    /// Type parameter symbol.
    pub name: InternedStr,
    /// Built-in constraints declared for the parameter.
    pub constraints: Vec<ConstraintKind>,
}

/// Template type syntax extracted from a generic declaration signature.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum TemplateType {
    /// The unit type `()`.
    Unit,
    /// A named concrete type symbol such as `i32` or `Point`.
    Named(InternedStr),
    /// A reference to a type parameter by name.
    TypeParam(InternedStr),
    /// A generic type application such as `Vec<T>`.
    Generic {
        /// Base type symbol.
        name: InternedStr,
        /// Template arguments.
        args: Vec<TemplateType>,
    },
    /// An inferred placeholder `_`.
    Infer,
}

/// A generic function signature extracted from the AST.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct GenericFunctionTemplate {
    /// Function symbol.
    pub name: InternedStr,
    /// Declared type parameters.
    pub type_params: Vec<GenericParamSpec>,
    /// Parameter type templates.
    pub params: Vec<TemplateType>,
    /// Return type template.
    pub return_type: TemplateType,
}

/// Returns whether `ty` satisfies a built-in generic constraint.
#[must_use]
pub fn check_constraint(arena: &TypeArena, ty: TypeId, constraint: ConstraintKind) -> bool {
    match constraint {
        ConstraintKind::Ordered => matches!(
            arena.kind(ty),
            TypeKind::I32 | TypeKind::I64 | TypeKind::F64 | TypeKind::Str
        ),
        ConstraintKind::Eq => matches!(
            arena.kind(ty),
            TypeKind::I32 | TypeKind::I64 | TypeKind::F64 | TypeKind::Bool | TypeKind::Str
        ),
        ConstraintKind::Numeric => matches!(arena.kind(ty), TypeKind::I32 | TypeKind::I64 | TypeKind::F64),
    }
}

/// Builds a stable mangled suffix for a monomorphized type argument list.
#[must_use]
pub fn mangle_type_args(
    arena: &TypeArena,
    names: &impl Fn(InternedStr) -> Option<String>,
    type_args: &[TypeId],
) -> String {
    let mut mangled = String::new();
    for type_arg in type_args {
        if !mangled.is_empty() {
            mangled.push('_');
        }
        mangle_one(arena, names, *type_arg, &mut mangled);
    }
    mangled
}

fn mangle_one(
    arena: &TypeArena,
    names: &impl Fn(InternedStr) -> Option<String>,
    type_id: TypeId,
    output: &mut String,
) {
    match arena.kind(type_id) {
        TypeKind::Unit => output.push_str("unit"),
        TypeKind::Unknown => output.push_str("unknown"),
        TypeKind::I32 => output.push_str("i32"),
        TypeKind::I64 => output.push_str("i64"),
        TypeKind::F64 => output.push_str("f64"),
        TypeKind::Bool => output.push_str("bool"),
        TypeKind::Str => output.push_str("str"),
        TypeKind::Struct(struct_ty) => {
            output.push_str(&names(struct_ty.name).unwrap_or_else(|| String::from("struct")));
        }
        TypeKind::Array(element) => {
            output.push_str("array_");
            mangle_one(arena, names, *element, output);
        }
        TypeKind::Function(_) => output.push_str("fn"),
        TypeKind::Infer(id) => {
            output.push('t');
            output.push_str(&id.to_string());
        }
        TypeKind::IntInfer(id) => {
            output.push_str("int");
            output.push_str(&id.to_string());
        }
    }
}
