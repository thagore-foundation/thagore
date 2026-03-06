//! Constraint collection and local type inference for Thagore.

extern crate alloc;

use alloc::vec::Vec;
use thagore_ast::Span;

use crate::error::TypeError;
use crate::types::{TypeArena, TypeId, TypeKind};

/// A local type constraint.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum TypeConstraint {
    /// Two types must be equal.
    Equal {
        /// Left-hand side.
        left: TypeId,
        /// Right-hand side.
        right: TypeId,
        /// Source location of the constraint.
        span: Span,
    },
    /// A subtype relation. Thagore currently treats this as equality.
    Subtype {
        /// Subtype candidate.
        sub: TypeId,
        /// Supertype candidate.
        sup: TypeId,
        /// Source location of the constraint.
        span: Span,
    },
}

/// Union-find based local inference solver.
#[derive(Debug, Clone, Default)]
pub struct InferenceSolver {
    parents: Vec<u32>,
    constraints: Vec<TypeConstraint>,
}

impl InferenceSolver {
    /// Creates an empty inference solver.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Clears all recorded constraints and equivalence classes.
    pub fn clear(&mut self) {
        self.parents.clear();
        self.constraints.clear();
    }

    /// Returns the collected constraints.
    #[must_use]
    pub fn constraints(&self) -> &[TypeConstraint] {
        &self.constraints
    }

    /// Ensures the union-find can address every type interned in `arena`.
    pub fn sync_with_arena(&mut self, arena: &TypeArena) {
        while self.parents.len() < arena.len() {
            self.parents.push(self.parents.len() as u32);
        }
    }

    /// Records and applies an equality constraint.
    pub fn add_equality(
        &mut self,
        left: TypeId,
        right: TypeId,
        span: Span,
        arena: &TypeArena,
        errors: &mut Vec<TypeError>,
    ) -> TypeId {
        self.constraints
            .push(TypeConstraint::Equal { left, right, span });
        self.sync_with_arena(arena);
        self.unify(left, right, span, arena, errors)
    }

    /// Records and applies a subtype constraint.
    pub fn add_subtype(
        &mut self,
        sub: TypeId,
        sup: TypeId,
        span: Span,
        arena: &TypeArena,
        errors: &mut Vec<TypeError>,
    ) -> TypeId {
        self.constraints
            .push(TypeConstraint::Subtype { sub, sup, span });
        self.sync_with_arena(arena);
        self.unify(sub, sup, span, arena, errors)
    }

    /// Returns the current representative for `id`.
    pub fn resolve(&mut self, id: TypeId) -> TypeId {
        self.ensure(id);
        TypeId::new(self.find(id.as_u32()))
    }

    fn unify(
        &mut self,
        left: TypeId,
        right: TypeId,
        span: Span,
        arena: &TypeArena,
        errors: &mut Vec<TypeError>,
    ) -> TypeId {
        let left_root = self.resolve(left);
        let right_root = self.resolve(right);
        if left_root == right_root {
            return left_root;
        }

        match (arena.kind(left_root), arena.kind(right_root)) {
            (TypeKind::Unknown, _) | (_, TypeKind::Unknown) => arena.unknown(),
            (TypeKind::Infer(_), TypeKind::Infer(_)) => {
                self.union(left_root, right_root);
                self.resolve(left_root)
            }
            (TypeKind::IntInfer(_), TypeKind::IntInfer(_)) => {
                self.union(left_root, right_root);
                self.resolve(left_root)
            }
            (TypeKind::Infer(_), _) => {
                self.union(left_root, right_root);
                right_root
            }
            (_, TypeKind::Infer(_)) => {
                self.union(right_root, left_root);
                left_root
            }
            (TypeKind::IntInfer(_), TypeKind::I32 | TypeKind::I64) => {
                self.union(left_root, right_root);
                right_root
            }
            (TypeKind::I32 | TypeKind::I64, TypeKind::IntInfer(_)) => {
                self.union(right_root, left_root);
                left_root
            }
            (TypeKind::IntInfer(_), _) => {
                errors.push(TypeError::TypeMismatch {
                    expected: arena.i32(),
                    found: right_root,
                    span,
                });
                arena.unknown()
            }
            (_, TypeKind::IntInfer(_)) => {
                errors.push(TypeError::TypeMismatch {
                    expected: left_root,
                    found: arena.i32(),
                    span,
                });
                arena.unknown()
            }
            (TypeKind::Array(left_element), TypeKind::Array(right_element)) => {
                self.unify(*left_element, *right_element, span, arena, errors);
                self.union(left_root, right_root);
                self.resolve(left_root)
            }
            (TypeKind::Function(left_fn), TypeKind::Function(right_fn))
                if left_fn.params.len() == right_fn.params.len() =>
            {
                for (left_param, right_param) in left_fn.params.iter().zip(&right_fn.params) {
                    self.unify(*left_param, *right_param, span, arena, errors);
                }
                self.unify(
                    left_fn.return_type,
                    right_fn.return_type,
                    span,
                    arena,
                    errors,
                );
                self.union(left_root, right_root);
                self.resolve(left_root)
            }
            (TypeKind::Struct(left_struct), TypeKind::Struct(right_struct))
                if left_struct.name == right_struct.name =>
            {
                self.union(left_root, right_root);
                self.resolve(left_root)
            }
            _ => {
                errors.push(TypeError::TypeMismatch {
                    expected: left_root,
                    found: right_root,
                    span,
                });
                arena.unknown()
            }
        }
    }

    fn ensure(&mut self, id: TypeId) {
        while self.parents.len() <= id.as_u32() as usize {
            self.parents.push(self.parents.len() as u32);
        }
    }

    fn find(&mut self, id: u32) -> u32 {
        let index = id as usize;
        let parent = self.parents[index];
        if parent == id {
            return id;
        }
        let root = self.find(parent);
        self.parents[index] = root;
        root
    }

    fn union(&mut self, child: TypeId, parent: TypeId) {
        self.ensure(child);
        self.ensure(parent);
        let child_root = self.find(child.as_u32()) as usize;
        let parent_root = self.find(parent.as_u32());
        self.parents[child_root] = parent_root;
    }
}
