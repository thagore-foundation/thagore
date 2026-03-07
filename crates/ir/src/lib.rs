#![no_std]
//! Typed intermediate representation and lowering pipeline for the Thagore
//! compiler.

extern crate alloc;

pub mod block;
pub mod error;
pub mod instr;
pub mod lower;
pub mod module_lower;
pub mod module;
pub mod validate;

pub use crate::block::{BasicBlock, BlockId, Terminator};
pub use crate::error::LoweringError;
pub use crate::instr::{BinOp, Const, Instr, UnOp, Value};
pub use crate::lower::{IrLowerer, LoweringEnv, ARRAY_LEN_INTRINSIC, FLOW_COMPENSATE_SALT};
pub use crate::module_lower::lower_module;
pub use crate::module::{IrFunction, IrModule, IrStruct};
pub use crate::validate::{validate_function, validate_module};
