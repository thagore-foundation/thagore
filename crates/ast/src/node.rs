//! Core node identity and source location primitives for the Thagore AST.
//!
//! These types are `no_std` friendly and shared by every AST node. They do not
//! depend on parser or semantic layers.

use core::fmt;

/// Strongly typed identifier assigned to a single AST node.
///
/// `NodeId` prevents raw integer mixing across APIs and keeps node identity
/// explicit throughout traversal, diagnostics, and indexing code.
#[repr(transparent)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct NodeId(u32);

impl NodeId {
    /// Sentinel value used when a node has not been assigned a stable id yet.
    pub const INVALID: Self = Self(u32::MAX);

    /// Creates a new typed node identifier from its raw numeric value.
    #[must_use]
    pub const fn new(raw: u32) -> Self {
        Self(raw)
    }

    /// Returns the raw numeric identifier.
    #[must_use]
    pub const fn as_u32(self) -> u32 {
        self.0
    }

    /// Returns `true` when this value is the invalid sentinel.
    #[must_use]
    pub const fn is_valid(self) -> bool {
        self.0 != Self::INVALID.0
    }
}

impl Default for NodeId {
    fn default() -> Self {
        Self::INVALID
    }
}

impl From<u32> for NodeId {
    fn from(value: u32) -> Self {
        Self::new(value)
    }
}

impl From<NodeId> for u32 {
    fn from(value: NodeId) -> Self {
        value.as_u32()
    }
}

impl fmt::Display for NodeId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.is_valid() {
            write!(f, "{}", self.0)
        } else {
            f.write_str("<invalid-node-id>")
        }
    }
}

/// Half-open byte span in the original source file.
///
/// `start` is inclusive and `end` is exclusive. Every AST node carries a span
/// so diagnostics and editor tooling can report precise source locations.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Hash)]
pub struct Span {
    /// Inclusive byte offset of the first byte in the node.
    pub start: u32,
    /// Exclusive byte offset immediately after the node.
    pub end: u32,
}

impl Span {
    /// Creates a new half-open source span.
    #[must_use]
    pub const fn new(start: u32, end: u32) -> Self {
        Self { start, end }
    }

    /// Returns an empty span at byte offset zero.
    #[must_use]
    pub const fn empty() -> Self {
        Self::new(0, 0)
    }

    /// Returns the byte length covered by the span.
    #[must_use]
    pub const fn len(self) -> u32 {
        self.end.saturating_sub(self.start)
    }

    /// Returns `true` when the span covers zero bytes.
    #[must_use]
    pub const fn is_empty(self) -> bool {
        self.start >= self.end
    }

    /// Returns `true` when `offset` lies within this half-open span.
    #[must_use]
    pub const fn contains(self, offset: u32) -> bool {
        self.start <= offset && offset < self.end
    }

    /// Returns the smallest span that covers both input spans.
    #[must_use]
    pub const fn join(self, other: Self) -> Self {
        let start = if self.start <= other.start {
            self.start
        } else {
            other.start
        };
        let end = if self.end >= other.end {
            self.end
        } else {
            other.end
        };
        Self { start, end }
    }
}

impl fmt::Display for Span {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}..{}", self.start, self.end)
    }
}
