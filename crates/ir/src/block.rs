//! Basic blocks and terminators for Thagore IR.

extern crate alloc;

use alloc::vec::Vec;
use core::fmt;

use crate::instr::{Instr, Value};

/// Strongly typed identifier for a basic block.
#[repr(transparent)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct BlockId(u32);

impl BlockId {
    /// Sentinel value used for missing block ids.
    pub const INVALID: Self = Self(u32::MAX);

    /// Creates a new block identifier.
    #[must_use]
    pub const fn new(raw: u32) -> Self {
        Self(raw)
    }

    /// Returns the raw numeric identifier.
    #[must_use]
    pub const fn as_u32(self) -> u32 {
        self.0
    }

    /// Returns `true` when the block id is not the invalid sentinel.
    #[must_use]
    pub const fn is_valid(self) -> bool {
        self.0 != Self::INVALID.0
    }
}

impl fmt::Display for BlockId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.is_valid() {
            write!(f, "bb{}", self.0)
        } else {
            f.write_str("<invalid-block>")
        }
    }
}

impl From<u32> for BlockId {
    fn from(value: u32) -> Self {
        Self::new(value)
    }
}

/// Control-flow terminator for a basic block.
#[derive(Debug, Clone, PartialEq)]
pub enum Terminator {
    /// Returns from the current function.
    Return(Option<Value>),
    /// Unconditional jump to another block.
    Jump(BlockId),
    /// Conditional branch to two successor blocks.
    Branch(Value, BlockId, BlockId),
    /// Marks unreachable control flow after a fatal lowering failure.
    Unreachable,
}

/// A flat basic block in the lowered IR.
#[derive(Debug, Clone, PartialEq)]
pub struct BasicBlock {
    /// Stable block identifier.
    pub id: BlockId,
    /// Linear instruction sequence for the block.
    pub instructions: Vec<Instr>,
    /// Optional block terminator.
    pub terminator: Option<Terminator>,
    /// Outgoing control-flow edges.
    pub successors: Vec<BlockId>,
    /// Incoming control-flow edges.
    pub predecessors: Vec<BlockId>,
}

impl BasicBlock {
    /// Creates an empty basic block with no instructions or terminator.
    #[must_use]
    pub fn new(id: BlockId) -> Self {
        Self {
            id,
            instructions: Vec::new(),
            terminator: None,
            successors: Vec::new(),
            predecessors: Vec::new(),
        }
    }

    /// Returns `true` when the block already has a terminator.
    #[must_use]
    pub fn is_terminated(&self) -> bool {
        self.terminator.is_some()
    }
}
