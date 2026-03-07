#![no_std]
//! Static type checking for the Thagore compiler.

extern crate alloc;

pub mod check;
pub mod error;
pub mod generics;
pub mod infer;
pub mod module_check;
pub mod scope;
pub mod table;
pub mod types;

pub use crate::check::TypeChecker;
pub use crate::error::TypeError;
pub use crate::generics::{
    check_constraint, mangle_type_args, GenericFunctionTemplate, GenericParamSpec,
    MonomorphInstance, MonomorphRequest, MonomorphResult, MonomorphWorkList, TemplateType,
};
pub use crate::infer::{InferenceSolver, TypeConstraint};
pub use crate::module_check::check_module;
pub use crate::scope::{FnvBuildHasher, FnvHasher, ScopeMap, ScopeStack};
pub use crate::table::TypeTable;
pub use crate::types::{FunctionType, StructField, StructType, TypeArena, TypeId, TypeKind};
