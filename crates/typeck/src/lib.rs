#![no_std]
//! Static type checking for the Thagore compiler.

extern crate alloc;

pub mod check;
pub mod error;
pub mod infer;
pub mod scope;
pub mod table;
pub mod types;

pub use crate::check::TypeChecker;
pub use crate::error::TypeError;
pub use crate::infer::{InferenceSolver, TypeConstraint};
pub use crate::scope::{FnvBuildHasher, FnvHasher, ScopeMap, ScopeStack};
pub use crate::table::TypeTable;
pub use crate::types::{FunctionType, StructField, StructType, TypeArena, TypeId, TypeKind};
