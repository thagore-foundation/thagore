//! Zero-copy source interning utilities.

use crate::token::{SliceRef, Span};

/// Zero-copy symbol handle into the original source buffer.
pub type Symbol = SliceRef;

/// A view-based interner that stores identifiers as source slices.
///
/// No allocation occurs during interning. Symbols are `(offset, len)` pairs
/// into the original source text.
#[derive(Clone, Copy, Debug)]
pub struct Interner<'src> {
    source: &'src str,
}

impl<'src> Interner<'src> {
    /// Creates a new zero-copy interner over `source`.
    #[must_use]
    pub const fn new(source: &'src str) -> Self {
        Self { source }
    }

    /// Returns the backing source buffer.
    #[must_use]
    pub const fn source(&self) -> &'src str {
        self.source
    }

    /// Interns a source span as a symbol.
    #[must_use]
    pub const fn intern(&self, span: Span) -> Symbol {
        SliceRef::from_span(span)
    }

    /// Interns a byte range as a symbol.
    #[must_use]
    pub const fn intern_range(&self, offset: u32, len: u32) -> Symbol {
        SliceRef::new(offset, len)
    }

    /// Resolves a symbol back to the original source slice.
    #[must_use]
    pub fn resolve(&self, symbol: Symbol) -> Option<&'src str> {
        let start = symbol.offset as usize;
        let end = symbol.end() as usize;
        self.source.get(start..end)
    }

    /// Resolves a span directly to the original source slice.
    #[must_use]
    pub fn resolve_span(&self, span: Span) -> Option<&'src str> {
        self.resolve(self.intern(span))
    }
}
