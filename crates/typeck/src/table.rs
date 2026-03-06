//! Side table for attaching types to AST node ids.

extern crate alloc;

use alloc::vec::Vec;
use thagore_ast::NodeId;

use crate::types::TypeId;

/// `NodeId -> TypeId` association table produced by the type checker.
#[derive(Debug, Clone, Default)]
pub struct TypeTable {
    entries: Vec<Option<TypeId>>,
}

impl TypeTable {
    /// Creates an empty type table.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Removes all recorded entries.
    pub fn clear(&mut self) {
        self.entries.clear();
    }

    /// Records the type for `node_id`.
    pub fn insert(&mut self, node_id: NodeId, type_id: TypeId) {
        let index = node_id.as_u32() as usize;
        if self.entries.len() <= index {
            self.entries.resize(index + 1, None);
        }
        self.entries[index] = Some(type_id);
    }

    /// Returns the recorded type for `node_id`, if any.
    #[must_use]
    pub fn get(&self, node_id: NodeId) -> Option<TypeId> {
        self.entries
            .get(node_id.as_u32() as usize)
            .copied()
            .flatten()
    }

    /// Returns `true` when `node_id` has an associated type.
    #[must_use]
    pub fn contains(&self, node_id: NodeId) -> bool {
        self.get(node_id).is_some()
    }

    /// Rewrites every recorded type id in place.
    pub fn rewrite_all(&mut self, mut rewrite: impl FnMut(TypeId) -> TypeId) {
        for entry in &mut self.entries {
            if let Some(type_id) = entry {
                *type_id = rewrite(*type_id);
            }
        }
    }
}
