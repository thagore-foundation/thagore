//! LLVM terminator emission for Thagore IR.

use thagore_ir::Terminator;

use crate::context::{CodegenContext, FunctionContext};
use crate::error::CodegenError;

/// Emits a terminator into the current LLVM basic block.
pub fn emit_terminator<'ctx>(
    context: &mut CodegenContext<'ctx>,
    function: &mut FunctionContext<'ctx>,
    terminator: &Terminator,
) -> Result<(), CodegenError> {
    match terminator {
        Terminator::Return(Some(value)) => {
            let runtime =
                function
                    .values
                    .get(value)
                    .copied()
                    .ok_or(CodegenError::UnknownValue {
                        value: *value,
                        function: function.name,
                    })?;
            context
                .builder
                .build_return(Some(&runtime))
                .map_err(|error| CodegenError::OutputFailed {
                    artifact: "return".into(),
                    message: error.to_string(),
                })?;
        }
        Terminator::Return(None) => {
            context
                .builder
                .build_return(None)
                .map_err(|error| CodegenError::OutputFailed {
                    artifact: "return".into(),
                    message: error.to_string(),
                })?;
        }
        Terminator::Jump(block) => {
            let target = function
                .blocks
                .get(block)
                .copied()
                .ok_or(CodegenError::UnknownBlock {
                    block: *block,
                    function: function.name,
                })?;
            context
                .builder
                .build_unconditional_branch(target)
                .map_err(|error| CodegenError::OutputFailed {
                    artifact: "branch".into(),
                    message: error.to_string(),
                })?;
        }
        Terminator::Branch(_, then_block, else_block) => {
            let then_target =
                function
                    .blocks
                    .get(then_block)
                    .copied()
                    .ok_or(CodegenError::UnknownBlock {
                        block: *then_block,
                        function: function.name,
                    })?;
            let else_target =
                function
                    .blocks
                    .get(else_block)
                    .copied()
                    .ok_or(CodegenError::UnknownBlock {
                        block: *else_block,
                        function: function.name,
                    })?;
            let cond = function
                .values
                .get(match terminator {
                    Terminator::Branch(value, _, _) => value,
                    _ => unreachable!(),
                })
                .copied()
                .ok_or(CodegenError::UnknownValue {
                    value: match terminator {
                        Terminator::Branch(value, _, _) => *value,
                        _ => unreachable!(),
                    },
                    function: function.name,
                })?
                .into_int_value();
            context
                .builder
                .build_conditional_branch(cond, then_target, else_target)
                .map_err(|error| CodegenError::OutputFailed {
                    artifact: "cond-branch".into(),
                    message: error.to_string(),
                })?;
        }
        Terminator::Unreachable => {
            context
                .builder
                .build_unreachable()
                .map_err(|error| CodegenError::OutputFailed {
                    artifact: "unreachable".into(),
                    message: error.to_string(),
                })?;
        }
    }
    Ok(())
}
