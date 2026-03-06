//! Function declaration and body emission for Thagore IR.

use inkwell::attributes::{Attribute, AttributeLoc};
use thagore_ir::{IrFunction, IrModule};
use thagore_typeck::TypeId;

use crate::context::{CodegenContext, FunctionContext};
use crate::error::CodegenError;
use crate::instr::{emit_instr, resolve_pending_phis};
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
                apply_function_attributes(context, llvm_function, function);
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
        if block
            .successors
            .iter()
            .any(|successor| successor.as_u32() <= block.id.as_u32())
        {
            if let Some(instruction) = llvm_block.get_last_instruction() {
                context.annotate_loop_branch(instruction)?;
            }
        }
    }

    resolve_pending_phis(context, &mut function)?;

    Ok(())
}

fn apply_function_attributes<'ctx>(
    context: &CodegenContext<'ctx>,
    llvm_function: inkwell::values::FunctionValue<'ctx>,
    ir_function: &IrFunction,
) {
    add_enum_attribute(context, llvm_function, "nounwind", 0);
    add_enum_attribute(context, llvm_function, "willreturn", 0);
    add_enum_attribute(context, llvm_function, "mustprogress", 0);

    if context.target.fast_math {
        llvm_function.add_attribute(
            AttributeLoc::Function,
            context.llvm.create_string_attribute("unsafe-fp-math", "true"),
        );
        llvm_function.add_attribute(
            AttributeLoc::Function,
            context.llvm.create_string_attribute("no-infs-fp-math", "true"),
        );
        llvm_function.add_attribute(
            AttributeLoc::Function,
            context.llvm.create_string_attribute("no-nans-fp-math", "true"),
        );
    }

    llvm_function.add_attribute(
        AttributeLoc::Function,
        context
            .llvm
            .create_string_attribute("target-cpu", &context.target.cpu),
    );
    if !context.target.features.is_empty() {
        llvm_function.add_attribute(
            AttributeLoc::Function,
            context
                .llvm
                .create_string_attribute("target-features", &context.target.features),
        );
    }

    match purity(ir_function) {
        Purity::ReadNone => add_enum_attribute(context, llvm_function, "readnone", 0),
        Purity::ReadOnly => add_enum_attribute(context, llvm_function, "readonly", 0),
        Purity::Impure => {}
    }
}

fn add_enum_attribute<'ctx>(
    context: &CodegenContext<'ctx>,
    llvm_function: inkwell::values::FunctionValue<'ctx>,
    name: &str,
    value: u64,
) {
    let kind = Attribute::get_named_enum_kind_id(name);
    if kind != 0 {
        llvm_function.add_attribute(
            AttributeLoc::Function,
            context.llvm.create_enum_attribute(kind, value),
        );
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Purity {
    ReadNone,
    ReadOnly,
    Impure,
}

fn purity(function: &IrFunction) -> Purity {
    let mut saw_memory_read = false;

    for block in &function.blocks {
        for instr in &block.instructions {
            match instr {
                thagore_ir::Instr::Const(..)
                | thagore_ir::Instr::BinOp(..)
                | thagore_ir::Instr::UnOp(..)
                | thagore_ir::Instr::Phi(..) => {}
                thagore_ir::Instr::Load(..)
                | thagore_ir::Instr::GetField(..)
                | thagore_ir::Instr::Index(..) => {
                    saw_memory_read = true;
                }
                thagore_ir::Instr::Call(..)
                | thagore_ir::Instr::SetField(..)
                | thagore_ir::Instr::Alloc(..)
                | thagore_ir::Instr::Store(..) => return Purity::Impure,
            }
        }
    }

    if saw_memory_read {
        Purity::ReadOnly
    } else {
        Purity::ReadNone
    }
}
