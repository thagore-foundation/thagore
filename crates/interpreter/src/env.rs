//! Lexical environment stack for interpreter execution.

use std::collections::HashMap;

use crate::value::Value;

/// Stack of lexical scopes used by the tree-walking interpreter.
#[derive(Debug, Clone, Default)]
pub struct EnvStack {
    scopes: Vec<HashMap<String, Value>>,
}

impl EnvStack {
    /// Creates an empty environment stack with one root scope.
    #[must_use]
    pub fn new() -> Self {
        Self {
            scopes: vec![HashMap::new()],
        }
    }

    /// Pushes a new lexical scope.
    pub fn push(&mut self) {
        self.scopes.push(HashMap::new());
    }

    /// Pops the innermost lexical scope.
    pub fn pop(&mut self) {
        if self.scopes.len() > 1 {
            self.scopes.pop();
        }
    }

    /// Binds `name` in the innermost scope.
    pub fn define(&mut self, name: impl Into<String>, value: Value) {
        if let Some(scope) = self.scopes.last_mut() {
            scope.insert(name.into(), value);
        }
    }

    /// Assigns an existing binding in the nearest containing scope.
    pub fn assign(&mut self, name: &str, value: Value) -> bool {
        for scope in self.scopes.iter_mut().rev() {
            if let Some(slot) = scope.get_mut(name) {
                *slot = value;
                return true;
            }
        }
        false
    }

    /// Looks up a binding in lexical-scope order.
    #[must_use]
    pub fn get(&self, name: &str) -> Option<Value> {
        self.scopes
            .iter()
            .rev()
            .find_map(|scope| scope.get(name).cloned())
    }
}
