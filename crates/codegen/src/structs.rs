//! LLVM struct declarations for Thagore IR structs.

use thagore_ir::IrStruct;

use crate::context::CodegenContext;
use crate::error::CodegenError;

/// Declares all nominal structs in the LLVM module.
pub fn declare_structs<'ctx>(
    context: &mut CodegenContext<'ctx>,
    structs: &[IrStruct],
) -> Result<(), Vec<CodegenError>> {
    let mut errors = Vec::new();
    for ir_struct in structs {
        if context.type_map.struct_type(ir_struct.name).is_none() {
            errors.push(CodegenError::UnknownStruct {
                name: ir_struct.name,
                span: None,
            });
        }
    }
    if errors.is_empty() {
        Ok(())
    } else {
        Err(errors)
    }
}
