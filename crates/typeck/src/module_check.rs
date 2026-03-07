//! Per-module type-checking entry points.

extern crate alloc;

use alloc::vec::Vec;
use thagore_ast::Decl;

use crate::check::TypeChecker;
use crate::error::TypeError;
use crate::table::TypeTable;

/// Type-checks one compilation unit without requiring a merged whole-program AST.
pub fn check_module<'ast>(
    checker: &mut TypeChecker,
    decls: &'ast [Decl<'ast>],
) -> Result<TypeTable, Vec<TypeError>> {
    checker.check(decls)
}
