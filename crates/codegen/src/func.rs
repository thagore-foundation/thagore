//! Function declaration and body emission for Thagore IR.

use thagore_ir::{IrFunction, IrModule};
use thagore_typeck::TypeId;

use crate::context::{CodegenContext, FunctionContext};
use crate::error::CodegenError;
use crate::instr::emit_instr;
use crate::term::emit_terminator;

/// Declares every function in the IR module before emitting bodies.
pub fn declare_functions<'ctx>(
    context: &mut CodegenContext<'ctx>,
    ir_module: &IrModule,
) -> Result<(), Vec<CodegenError>> {
    let mut errors = Vec::new();
    for function in &ir_module.functions {
        let param_types = function
            .params
            .iter()
            .map(|(_, ty)| *ty)
            .collect::<Vec<_>>();
        match context
            .type_map
            .direct_function_type(function.return_type, &param_types)
        {
            Ok(function_type) => {
                let name = context.symbol_name(function.name);
                let llvm_function = context.module.add_function(&name, function_type, None);
                context.functions.insert(function.name, llvm_function);
            }
            Err(error) => errors.push(error),
        }
    }
    if errors.is_empty() {
        Ok(())
    } else {
        Err(errors)
    }
}

/// Emits all non-extern function bodies into the LLVM module.
pub fn emit_functions<'ctx>(
    context: &mut CodegenContext<'ctx>,
    ir_module: &IrModule,
) -> Result<(), Vec<CodegenError>> {
    let mut errors = Vec::new();
    for function in &ir_module.functions {
        if function.is_extern {
            continue;
        }
        if let Err(error) = emit_function(context, function) {
            errors.push(error);
        }
    }
    if errors.is_empty() {
        Ok(())
    } else {
        Err(errors)
    }
}

/// Emits a single function body.
pub fn emit_function<'ctx>(
    context: &mut CodegenContext<'ctx>,
    ir_function: &IrFunction,
) -> Result<(), CodegenError> {
    let llvm_function =
        context
            .functions
            .get(&ir_function.name)
            .copied()
            .ok_or(CodegenError::UnknownFunction {
                name: ir_function.name,
                span: None,
            })?;
    let mut value_types = ir_function.value_types.clone();
    if value_types.len() < ir_function.params.len() {
        value_types.resize(ir_function.params.len(), TypeId::new(0));
    }
    let mut function = FunctionContext::new(llvm_function, ir_function.name, value_types);

    for block in &ir_function.blocks {
        let name = format!("bb{}", block.id.as_u32());
        let llvm_block = context.llvm.append_basic_block(llvm_function, &name);
        function.blocks.insert(block.id, llvm_block);
    }

    for (index, (value, _)) in ir_function.params.iter().enumerate() {
        if let Some(param) = llvm_function.get_nth_param(index as u32) {
            function.values.insert(*value, param);
        }
    }

    for block in &ir_function.blocks {
        let llvm_block =
            function
                .blocks
                .get(&block.id)
                .copied()
                .ok_or(CodegenError::UnknownBlock {
                    block: block.id,
                    function: ir_function.name,
                })?;
        context.builder.position_at_end(llvm_block);
        for instr in &block.instructions {
            emit_instr(context, &mut function, instr)?;
        }
        let terminator = block
            .terminator
            .as_ref()
            .ok_or(CodegenError::UnknownBlock {
                block: block.id,
                function: ir_function.name,
            })?;
        emit_terminator(context, &mut function, terminator)?;
    }

    Ok(())
}
