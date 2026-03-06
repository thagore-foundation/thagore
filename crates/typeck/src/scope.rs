//! Lexical scope tracking for the Thagore type checker.

extern crate alloc;

use alloc::vec::Vec;
use core::hash::{BuildHasherDefault, Hash, Hasher};
use indexmap::IndexMap;

/// FNV-1a hasher used for deterministic `no_std` index maps.
#[derive(Debug, Clone, Default)]
pub struct FnvHasher(u64);

impl Hasher for FnvHasher {
    fn finish(&self) -> u64 {
        self.0
    }

    fn write(&mut self, bytes: &[u8]) {
        const OFFSET_BASIS: u64 = 0xcbf29ce484222325;
        const PRIME: u64 = 0x100000001b3;

        let mut hash = if self.0 == 0 { OFFSET_BASIS } else { self.0 };
        for byte in bytes {
            hash ^= u64::from(*byte);
            hash = hash.wrapping_mul(PRIME);
        }
        self.0 = hash;
    }
}

/// Deterministic build hasher for `IndexMap`.
pub type FnvBuildHasher = BuildHasherDefault<FnvHasher>;

/// Ordered hash map used by scope tables.
pub type ScopeMap<K, V> = IndexMap<K, V, FnvBuildHasher>;

/// Lexically nested scope stack with shadowing.
#[derive(Debug, Clone)]
pub struct ScopeStack<K, V> {
    scopes: Vec<ScopeMap<K, V>>,
}

impl<K, V> Default for ScopeStack<K, V>
where
    K: Eq + Hash,
{
    fn default() -> Self {
        Self::new()
    }
}

impl<K, V> ScopeStack<K, V>
where
    K: Eq + Hash,
{
    /// Creates a new scope stack with a single global scope.
    #[must_use]
    pub fn new() -> Self {
        let mut scopes = Vec::new();
        scopes.push(ScopeMap::with_hasher(FnvBuildHasher::default()));
        Self { scopes }
    }

    /// Clears the stack back to a single global scope.
    pub fn clear(&mut self) {
        self.scopes.clear();
        self.scopes
            .push(ScopeMap::with_hasher(FnvBuildHasher::default()));
    }

    /// Pushes a new lexical scope.
    pub fn push_scope(&mut self) {
        self.scopes
            .push(ScopeMap::with_hasher(FnvBuildHasher::default()));
    }

    /// Pops the current lexical scope.
    ///
    /// The global scope is never removed.
    pub fn pop_scope(&mut self) {
        if self.scopes.len() > 1 {
            self.scopes.pop();
        }
    }

    /// Returns the current nesting depth.
    #[must_use]
    pub fn depth(&self) -> usize {
        self.scopes.len()
    }

    /// Inserts or shadows a symbol in the current scope.
    pub fn insert(&mut self, key: K, value: V) -> Option<V> {
        self.scopes
            .last_mut()
            .expect("scope stack always contains a global scope")
            .insert(key, value)
    }

    /// Returns a reference to a symbol from the nearest visible scope.
    #[must_use]
    pub fn get(&self, key: &K) -> Option<&V> {
        self.scopes.iter().rev().find_map(|scope| scope.get(key))
    }

    /// Returns a mutable reference to a symbol from the nearest visible scope.
    #[must_use]
    pub fn get_mut(&mut self, key: &K) -> Option<&mut V> {
        self.scopes
            .iter_mut()
            .rev()
            .find_map(|scope| scope.get_mut(key))
    }

    /// Returns `true` when the current scope contains `key`.
    #[must_use]
    pub fn contains_in_current_scope(&self, key: &K) -> bool {
        self.scopes
            .last()
            .map(|scope| scope.contains_key(key))
            .unwrap_or(false)
    }
}
