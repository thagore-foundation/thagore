//! SSA values, constants, and three-address IR instructions for Thagore.

extern crate alloc;

use alloc::vec::Vec;
use core::fmt;

use thagore_ast::InternedStr;
use thagore_typeck::TypeId;

use crate::block::BlockId;

/// Strongly typed SSA value identifier.
#[repr(transparent)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Value(u32);

impl Value {
    /// Sentinel value used when no real SSA value exists.
    pub const INVALID: Self = Self(u32::MAX);

    /// Creates a new SSA value identifier.
    #[must_use]
    pub const fn new(raw: u32) -> Self {
        Self(raw)
    }

    /// Returns the raw numeric identifier.
    #[must_use]
    pub const fn as_u32(self) -> u32 {
        self.0
    }

    /// Returns `true` when the value id is not the invalid sentinel.
    #[must_use]
    pub const fn is_valid(self) -> bool {
        self.0 != Self::INVALID.0
    }
}

impl fmt::Display for Value {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.is_valid() {
            write!(f, "v{}", self.0)
        } else {
            f.write_str("<invalid-value>")
        }
    }
}

impl From<u32> for Value {
    fn from(value: u32) -> Self {
        Self::new(value)
    }
}

/// Constant literals materialized directly in IR.
#[derive(Debug, Clone, PartialEq)]
pub enum Const {
    /// Integer constant.
    Int(i64),
    /// Floating-point constant.
    Float(f64),
    /// Boolean constant.
    Bool(bool),
    /// Interned string constant.
    Str(InternedStr),
    /// Unit constant.
    Unit,
}

/// Binary operators in the lowered IR.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum BinOp {
    /// Logical `or`.
    Or,
    /// Logical `and`.
    And,
    /// Addition.
    Add,
    /// Subtraction.
    Sub,
    /// Multiplication.
    Mul,
    /// Division.
    Div,
    /// Remainder.
    Rem,
    /// Equality comparison.
    Eq,
    /// Inequality comparison.
    NotEq,
    /// Less-than comparison.
    Lt,
    /// Less-than-or-equal comparison.
    LtEq,
    /// Greater-than comparison.
    Gt,
    /// Greater-than-or-equal comparison.
    GtEq,
}

/// Unary operators in the lowered IR.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum UnOp {
    /// Unary plus.
    Plus,
    /// Arithmetic negation.
    Neg,
    /// Logical negation.
    Not,
}

/// A single three-address IR instruction.
#[derive(Debug, Clone, PartialEq)]
pub enum Instr {
    /// Materializes a constant into a fresh SSA value.
    Const(Value, Const),
    /// Computes a binary operation into a fresh SSA value.
    BinOp(Value, BinOp, Value, Value),
    /// Computes a unary operation into a fresh SSA value.
    UnOp(Value, UnOp, Value),
    /// Calls a named function.
    Call(Value, InternedStr, Vec<Value>),
    /// Reads a field from an aggregate value.
    GetField(Value, Value, InternedStr),
    /// Writes a field into an aggregate storage slot.
    SetField(Value, InternedStr, Value),
    /// Reads an indexed element from an aggregate value.
    Index(Value, Value, Value),
    /// Allocates a stack slot for a local binding.
    Alloc(Value, TypeId),
    /// Loads a value from a storage slot.
    Load(Value, Value),
    /// Stores a value into a storage slot.
    Store(Value, Value),
    /// Merges values across predecessor blocks.
    Phi(Value, Vec<(Value, BlockId)>),
}

impl Instr {
    /// Returns the defined SSA value, when the instruction produces one.
    #[must_use]
    pub const fn defined_value(&self) -> Option<Value> {
        match self {
            Self::Const(value, _)
            | Self::BinOp(value, _, _, _)
            | Self::UnOp(value, _, _)
            | Self::Call(value, _, _)
            | Self::GetField(value, _, _)
            | Self::Index(value, _, _)
            | Self::Alloc(value, _)
            | Self::Load(value, _)
            | Self::Phi(value, _) => Some(*value),
            Self::SetField(..) | Self::Store(..) => None,
        }
    }
}
