//! Top-level IR module, function, and struct definitions for Thagore.

extern crate alloc;

use alloc::vec::Vec;

use thagore_ast::InternedStr;
use thagore_typeck::TypeId;

use crate::block::{BasicBlock, BlockId};
use crate::instr::Value;

/// A lowered IR module.
#[derive(Debug, Clone, PartialEq)]
pub struct IrModule {
    /// Interned module name.
    pub name: InternedStr,
    /// Lowered functions in the module.
    pub functions: Vec<IrFunction>,
    /// Lowered nominal struct definitions.
    pub structs: Vec<IrStruct>,
}

impl IrModule {
    /// Creates an empty IR module.
    #[must_use]
    pub fn new(name: InternedStr) -> Self {
        Self {
            name,
            functions: Vec::new(),
            structs: Vec::new(),
        }
    }
}

/// A lowered IR function.
#[derive(Debug, Clone, PartialEq)]
pub struct IrFunction {
    /// Function symbol.
    pub name: InternedStr,
    /// Parameter SSA values and their types.
    pub params: Vec<(Value, TypeId)>,
    /// Function return type.
    pub return_type: TypeId,
    /// Flat basic blocks for the function body.
    pub blocks: Vec<BasicBlock>,
    /// Entry block id.
    pub entry: BlockId,
    /// Per-value type side table indexed by `Value`.
    pub value_types: Vec<TypeId>,
    /// Marks an external declaration that has no lowered body.
    pub is_extern: bool,
}

impl IrFunction {
    /// Creates a new empty IR function.
    #[must_use]
    pub fn new(name: InternedStr, return_type: TypeId) -> Self {
        Self {
            name,
            params: Vec::new(),
            return_type,
            blocks: Vec::new(),
            entry: BlockId::INVALID,
            value_types: Vec::new(),
            is_extern: false,
        }
    }

    /// Returns the type of a value if it exists in the function side table.
    #[must_use]
    pub fn value_type(&self, value: Value) -> Option<TypeId> {
        self.value_types.get(value.as_u32() as usize).copied()
    }

    /// Returns a reference to a block by id.
    #[must_use]
    pub fn block(&self, id: BlockId) -> Option<&BasicBlock> {
        self.blocks.get(id.as_u32() as usize)
    }

    /// Returns a mutable reference to a block by id.
    #[must_use]
    pub fn block_mut(&mut self, id: BlockId) -> Option<&mut BasicBlock> {
        self.blocks.get_mut(id.as_u32() as usize)
    }
}

/// A lowered nominal struct definition.
#[derive(Debug, Clone, PartialEq)]
pub struct IrStruct {
    /// Struct symbol.
    pub name: InternedStr,
    /// Fields in declaration order.
    pub fields: Vec<(InternedStr, TypeId)>,
}
