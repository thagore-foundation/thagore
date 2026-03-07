//! Per-module IR lowering entry points.

extern crate alloc;

use alloc::vec::Vec;

use thagore_ast::{Decl, InternedStr};
use thagore_typeck::{MonomorphInstance, TypeArena, TypeTable};

use crate::error::LoweringError;
use crate::lower::IrLowerer;
use crate::module::IrModule;

/// Lowers one fully type-checked module into IR.
pub fn lower_module<'ast>(
    module_name: InternedStr,
    types: &TypeArena,
    table: &TypeTable,
    monomorphs: &[MonomorphInstance],
    decls: &'ast [Decl<'ast>],
) -> Result<IrModule, Vec<LoweringError>> {
    let mut lowerer = IrLowerer::new(module_name, types, table, monomorphs);
    lowerer.lower_module(decls)
}
