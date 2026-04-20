//! Lexical environment stack for interpreter execution.

use std::collections::HashMap;

use crate::value::Value;

#[derive(Debug, Clone, Default)]
struct Scope {
    /// User variables keyed by parser-interned string ID (no allocation on lookup/define).
    interned: HashMap<u32, Value>,
    /// Builtins and module aliases that don't have parser-assigned IDs.
    named: HashMap<String, Value>,
}

/// Stack of lexical scopes used by the tree-walking interpreter.
#[derive(Debug, Clone, Default)]
pub struct EnvStack {
    scopes: Vec<Scope>,
}

impl EnvStack {
    /// Creates an empty environment stack with one root scope.
    #[must_use]
    pub fn new() -> Self {
        Self {
            scopes: vec![Scope::default()],
        }
    }

    /// Pushes a new lexical scope.
    pub fn push(&mut self) {
        self.scopes.push(Scope::default());
    }

    /// Pops the innermost lexical scope.
    pub fn pop(&mut self) {
        if self.scopes.len() > 1 {
            self.scopes.pop();
        }
    }

    // ── String-keyed API (builtins, module aliases) ────────────────────────

    /// Binds `name` in the innermost scope's named map.
    pub fn define(&mut self, name: impl Into<String>, value: Value) {
        if let Some(scope) = self.scopes.last_mut() {
            scope.named.insert(name.into(), value);
        }
    }

    /// Assigns an existing named binding in the nearest containing scope.
    pub fn assign(&mut self, name: &str, value: Value) -> bool {
        for scope in self.scopes.iter_mut().rev() {
            if let Some(slot) = scope.named.get_mut(name) {
                *slot = value;
                return true;
            }
        }
        false
    }

    /// Looks up a named binding in lexical-scope order.
    #[must_use]
    pub fn get(&self, name: &str) -> Option<Value> {
        self.scopes
            .iter()
            .rev()
            .find_map(|scope| scope.named.get(name).cloned())
    }

    // ── ID-keyed API (user variables — hot path, zero String allocation) ───

    /// Binds `id` in the innermost scope's interned map.
    pub fn define_by_id(&mut self, id: u32, value: Value) {
        if let Some(scope) = self.scopes.last_mut() {
            scope.interned.insert(id, value);
        }
    }

    /// Assigns an existing interned binding in the nearest containing scope.
    pub fn assign_by_id(&mut self, id: u32, value: Value) -> bool {
        for scope in self.scopes.iter_mut().rev() {
            if let Some(slot) = scope.interned.get_mut(&id) {
                *slot = value;
                return true;
            }
        }
        false
    }

    /// Looks up an interned binding in lexical-scope order.
    #[must_use]
    pub fn get_by_id(&self, id: u32) -> Option<Value> {
        self.scopes
            .iter()
            .rev()
            .find_map(|scope| scope.interned.get(&id).cloned())
    }
}
