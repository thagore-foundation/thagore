//! LLVM instruction emission for Thagore IR instructions.

use inkwell::values::{BasicMetadataValueEnum, BasicValue, BasicValueEnum, PointerValue};
use inkwell::{FloatPredicate, IntPredicate};
use thagore_ir::{BinOp, Const, Instr, Value, ARRAY_LEN_INTRINSIC};
use thagore_typeck::TypeKind;

use crate::context::{CodegenContext, FunctionContext};
use crate::error::CodegenError;

/// Emits a single IR instruction into the current LLVM block.
pub fn emit_instr<'ctx>(
    context: &mut CodegenContext<'ctx>,
    function: &mut FunctionContext<'ctx>,
    instr: &Instr,
) -> Result<(), CodegenError> {
    match instr {
        Instr::Const(value, constant) => emit_const(context, function, *value, constant),
        Instr::BinOp(value, op, left, right) => {
            emit_bin_op(context, function, *value, *op, *left, *right)
        }
        Instr::UnOp(value, op, operand) => emit_un_op(context, function, *value, *op, *operand),
        Instr::Call(value, name, args) => emit_call(context, function, *value, *name, args),
        Instr::GetField(value, object, field) => {
            emit_get_field(context, function, *value, *object, *field)
        }
        Instr::SetField(object, field, value) => {
            emit_set_field(context, function, *object, *field, *value)
        }
        Instr::Index(value, object, index) => {
            emit_index(context, function, *value, *object, *index)
        }
        Instr::Alloc(value, ty) => emit_alloc(context, function, *value, *ty),
        Instr::Load(value, ptr) => emit_load(context, function, *value, *ptr),
        Instr::Store(ptr, value) => emit_store(context, function, *ptr, *value),
        Instr::Phi(value, incoming) => emit_phi(context, function, *value, incoming),
    }
}

fn emit_const<'ctx>(
    context: &mut CodegenContext<'ctx>,
    function: &mut FunctionContext<'ctx>,
    value: Value,
    constant: &Const,
) -> Result<(), CodegenError> {
    let type_id = type_of(function, value)?;
    let runtime = match constant {
        Const::Int(number) => match context.types.kind(type_id) {
            TypeKind::I64 => context
                .llvm
                .i64_type()
                .const_int(*number as u64, true)
                .as_basic_value_enum(),
            _ => context
                .llvm
                .i32_type()
                .const_int(*number as u64, true)
                .as_basic_value_enum(),
        },
        Const::Float(number) => context
            .llvm
            .f64_type()
            .const_float(*number)
            .as_basic_value_enum(),
        Const::Bool(value) => context
            .llvm
            .bool_type()
            .const_int(u64::from(*value), false)
            .as_basic_value_enum(),
        Const::Str(symbol) => context
            .builder
            .build_global_string_ptr(&context.symbol_name(*symbol), "str")
            .map_err(|message| CodegenError::OutputFailed {
                artifact: "global string".into(),
                message: message.to_string(),
            })?
            .as_pointer_value()
            .as_basic_value_enum(),
        Const::Unit => {
            if context.types.unit() == type_id {
                return Ok(());
            }
            return Err(CodegenError::ValueHasNoRuntimeRepresentation { value, span: None });
        }
    };
    function.values.insert(value, runtime);
    Ok(())
}

fn emit_bin_op<'ctx>(
    context: &mut CodegenContext<'ctx>,
    function: &mut FunctionContext<'ctx>,
    value: Value,
    op: BinOp,
    left: Value,
    right: Value,
) -> Result<(), CodegenError> {
    let left_value = lookup_value(context, function, left)?;
    let right_value = lookup_value(context, function, right)?;
    let left_ty = type_of(function, left)?;
    let result = match context.types.kind(left_ty) {
        TypeKind::I32 | TypeKind::I64 | TypeKind::Bool => {
            let lhs = left_value.into_int_value();
            let rhs = right_value.into_int_value();
            match op {
                BinOp::Add => context.builder.build_int_nsw_add(lhs, rhs, "add"),
                BinOp::Sub => context.builder.build_int_nsw_sub(lhs, rhs, "sub"),
                BinOp::Mul => context.builder.build_int_nsw_mul(lhs, rhs, "mul"),
                BinOp::Div => {
                    if can_use_exact_signed_div(lhs, rhs) {
                        context.builder.build_int_exact_signed_div(lhs, rhs, "div")
                    } else {
                        context.builder.build_int_signed_div(lhs, rhs, "div")
                    }
                }
                BinOp::Rem => context.builder.build_int_signed_rem(lhs, rhs, "rem"),
                BinOp::Eq => context
                    .builder
                    .build_int_compare(IntPredicate::EQ, lhs, rhs, "eq"),
                BinOp::NotEq => context
                    .builder
                    .build_int_compare(IntPredicate::NE, lhs, rhs, "ne"),
                BinOp::Lt => context
                    .builder
                    .build_int_compare(IntPredicate::SLT, lhs, rhs, "lt"),
                BinOp::LtEq => context
                    .builder
                    .build_int_compare(IntPredicate::SLE, lhs, rhs, "le"),
                BinOp::Gt => context
                    .builder
                    .build_int_compare(IntPredicate::SGT, lhs, rhs, "gt"),
                BinOp::GtEq => context
                    .builder
                    .build_int_compare(IntPredicate::SGE, lhs, rhs, "ge"),
                BinOp::And => context.builder.build_and(lhs, rhs, "and"),
                BinOp::Or => context.builder.build_or(lhs, rhs, "or"),
            }
            .map_err(builder_error)?
            .as_basic_value_enum()
        }
        TypeKind::F64 => {
            let lhs = left_value.into_float_value();
            let rhs = right_value.into_float_value();
            match op {
                BinOp::Add => context
                    .builder
                    .build_float_add(lhs, rhs, "add")
                    .map_err(builder_error)?
                    .as_basic_value_enum(),
                BinOp::Sub => context
                    .builder
                    .build_float_sub(lhs, rhs, "sub")
                    .map_err(builder_error)?
                    .as_basic_value_enum(),
                BinOp::Mul => context
                    .builder
                    .build_float_mul(lhs, rhs, "mul")
                    .map_err(builder_error)?
                    .as_basic_value_enum(),
                BinOp::Div => context
                    .builder
                    .build_float_div(lhs, rhs, "div")
                    .map_err(builder_error)?
                    .as_basic_value_enum(),
                BinOp::Rem => context
                    .builder
                    .build_float_rem(lhs, rhs, "rem")
                    .map_err(builder_error)?
                    .as_basic_value_enum(),
                BinOp::Eq => context
                    .builder
                    .build_float_compare(FloatPredicate::OEQ, lhs, rhs, "eq")
                    .map_err(builder_error)?
                    .as_basic_value_enum(),
                BinOp::NotEq => context
                    .builder
                    .build_float_compare(FloatPredicate::ONE, lhs, rhs, "ne")
                    .map_err(builder_error)?
                    .as_basic_value_enum(),
                BinOp::Lt => context
                    .builder
                    .build_float_compare(FloatPredicate::OLT, lhs, rhs, "lt")
                    .map_err(builder_error)?
                    .as_basic_value_enum(),
                BinOp::LtEq => context
                    .builder
                    .build_float_compare(FloatPredicate::OLE, lhs, rhs, "le")
                    .map_err(builder_error)?
                    .as_basic_value_enum(),
                BinOp::Gt => context
                    .builder
                    .build_float_compare(FloatPredicate::OGT, lhs, rhs, "gt")
                    .map_err(builder_error)?
                    .as_basic_value_enum(),
                BinOp::GtEq => context
                    .builder
                    .build_float_compare(FloatPredicate::OGE, lhs, rhs, "ge")
                    .map_err(builder_error)?
                    .as_basic_value_enum(),
                BinOp::And | BinOp::Or => {
                    return Err(CodegenError::InvalidOperandType {
                        value: left,
                        expected: "integer or bool",
                        found: left_ty,
                        span: None,
                    });
                }
            }
        }
        _ => {
            return Err(CodegenError::InvalidOperandType {
                value: left,
                expected: "numeric or bool",
                found: left_ty,
                span: None,
            })
        }
    };
    function.values.insert(value, result);
    Ok(())
}

fn emit_un_op<'ctx>(
    context: &mut CodegenContext<'ctx>,
    function: &mut FunctionContext<'ctx>,
    value: Value,
    op: thagore_ir::UnOp,
    operand: Value,
) -> Result<(), CodegenError> {
    let runtime = lookup_value(context, function, operand)?;
    let operand_ty = type_of(function, operand)?;
    let lowered = match context.types.kind(operand_ty) {
        TypeKind::I32 | TypeKind::I64 => {
            let operand = runtime.into_int_value();
            match op {
                thagore_ir::UnOp::Plus => operand.as_basic_value_enum(),
                thagore_ir::UnOp::Neg => context
                    .builder
                    .build_int_neg(operand, "neg")
                    .map_err(builder_error)?
                    .as_basic_value_enum(),
                thagore_ir::UnOp::Not => context
                    .builder
                    .build_not(operand, "not")
                    .map_err(builder_error)?
                    .as_basic_value_enum(),
            }
        }
        TypeKind::F64 => {
            let operand = runtime.into_float_value();
            match op {
                thagore_ir::UnOp::Plus => operand.as_basic_value_enum(),
                thagore_ir::UnOp::Neg => context
                    .builder
                    .build_float_neg(operand, "neg")
                    .map_err(builder_error)?
                    .as_basic_value_enum(),
                thagore_ir::UnOp::Not => {
                    return Err(CodegenError::InvalidOperandType {
                        value,
                        expected: "bool or integer",
                        found: operand_ty,
                        span: None,
                    })
                }
            }
        }
        TypeKind::Bool => {
            let operand = runtime.into_int_value();
            match op {
                thagore_ir::UnOp::Not => context
                    .builder
                    .build_not(operand, "not")
                    .map_err(builder_error)?
                    .as_basic_value_enum(),
                thagore_ir::UnOp::Plus | thagore_ir::UnOp::Neg => {
                    return Err(CodegenError::InvalidOperandType {
                        value,
                        expected: "numeric",
                        found: operand_ty,
                        span: None,
                    })
                }
            }
        }
        _ => {
            return Err(CodegenError::InvalidOperandType {
                value: operand,
                expected: "numeric or bool",
                found: operand_ty,
                span: None,
            })
        }
    };
    function.values.insert(value, lowered);
    Ok(())
}

fn emit_call<'ctx>(
    context: &mut CodegenContext<'ctx>,
    function: &mut FunctionContext<'ctx>,
    value: Value,
    name: thagore_ast::InternedStr,
    args: &[Value],
) -> Result<(), CodegenError> {
    if name == ARRAY_LEN_INTRINSIC {
        let result_ty = type_of(function, value)?;
        let array_value = lookup_value(context, function, args[0])?.into_struct_value();
        let len = context
            .builder
            .build_extract_value(array_value, 0, "array.len")
            .map_err(builder_error)?;
        let lowered = match context.types.kind(result_ty) {
            TypeKind::I32 => context
                .builder
                .build_int_truncate(len.into_int_value(), context.llvm.i32_type(), "array.len.i32")
                .map_err(builder_error)?
                .as_basic_value_enum(),
            _ => len,
        };
        function.values.insert(value, lowered);
        return Ok(());
    }

    let Some(target) = context.functions.get(&name).copied() else {
        return Err(CodegenError::UnknownFunction { name, span: None });
    };
    let args = args
        .iter()
        .map(|value| lookup_value(context, function, *value).map(BasicMetadataValueEnum::from))
        .collect::<Result<Vec<_>, _>>()?;
    let call = context
        .builder
        .build_call(target, &args, "call")
        .map_err(builder_error)?;
    if let Some(result) = call.try_as_basic_value().left() {
        function.values.insert(value, result);
    }
    Ok(())
}

fn emit_get_field<'ctx>(
    context: &mut CodegenContext<'ctx>,
    function: &mut FunctionContext<'ctx>,
    value: Value,
    object: Value,
    field: thagore_ast::InternedStr,
) -> Result<(), CodegenError> {
    let object_value = lookup_value(context, function, object)?;
    let object_ty = type_of(function, object)?;
    let (_, field_index) = struct_field_index(context, object_ty, field)?;
    let loaded = context
        .builder
        .build_extract_value(object_value.into_struct_value(), field_index as u32, "field.load")
        .map_err(builder_error)?;
    function.values.insert(value, loaded);
    Ok(())
}

fn emit_set_field<'ctx>(
    context: &mut CodegenContext<'ctx>,
    function: &mut FunctionContext<'ctx>,
    object: Value,
    field: thagore_ast::InternedStr,
    value: Value,
) -> Result<(), CodegenError> {
    let object_value = lookup_value(context, function, object)?;
    let object_ptr = object_value.into_pointer_value();
    let object_ty = type_of(function, object)?;
    let (_, field_index) = struct_field_index(context, object_ty, field)?;
    let gep = context
        .builder
        .build_struct_gep(object_ptr, field_index as u32, "field.store.gep")
        .map_err(builder_error)?;
    let runtime = lookup_value(context, function, value)?;
    context
        .builder
        .build_store(gep, runtime)
        .map_err(builder_error)?;
    Ok(())
}

fn emit_index<'ctx>(
    context: &mut CodegenContext<'ctx>,
    function: &mut FunctionContext<'ctx>,
    value: Value,
    object: Value,
    index: Value,
) -> Result<(), CodegenError> {
    let array_value = lookup_value(context, function, object)?.into_struct_value();
    let idx = lookup_value(context, function, index)?.into_int_value();
    let array_ty = type_of(function, object)?;
    match context.types.kind(array_ty) {
        TypeKind::Array(_) => {}
        _ => {
            return Err(CodegenError::InvalidOperandType {
                value: object,
                expected: "array",
                found: array_ty,
                span: None,
            })
        }
    }

    let data_ptr = context
        .builder
        .build_extract_value(array_value, 1, "array.data")
        .map_err(builder_error)?
        .into_pointer_value();
    let element_ptr = unsafe {
        // SAFETY: `data_ptr` points to the base address of the array element
        // buffer and `idx` is the integer index emitted by validated IR.
        context
            .builder
            .build_gep(data_ptr, &[idx], "array.elem.gep")
            .map_err(builder_error)?
    };
    let loaded = build_load(context, element_ptr, "array.elem")?;
    function.values.insert(value, loaded);
    Ok(())
}

fn emit_alloc<'ctx>(
    context: &mut CodegenContext<'ctx>,
    function: &mut FunctionContext<'ctx>,
    value: Value,
    ty: thagore_typeck::TypeId,
) -> Result<(), CodegenError> {
    let llvm_ty = context.basic_type(ty)?;
    let entry = function
        .function
        .get_first_basic_block()
        .ok_or(CodegenError::UnknownBlock {
            block: thagore_ir::BlockId::new(0),
            function: function.name,
        })?;
    let entry_builder = context.llvm.create_builder();
    if let Some(first) = entry.get_first_instruction() {
        entry_builder.position_before(&first);
    } else {
        entry_builder.position_at_end(entry);
    }
    let alloca = entry_builder
        .build_alloca(llvm_ty, "alloca")
        .map_err(builder_error)?;
    function.values.insert(value, alloca.as_basic_value_enum());
    Ok(())
}

fn emit_load<'ctx>(
    context: &mut CodegenContext<'ctx>,
    function: &mut FunctionContext<'ctx>,
    value: Value,
    ptr: Value,
) -> Result<(), CodegenError> {
    let pointer = lookup_value(context, function, ptr)?.into_pointer_value();
    let loaded = build_load(context, pointer, "load")?;
    function.values.insert(value, loaded);
    Ok(())
}

fn emit_store<'ctx>(
    context: &mut CodegenContext<'ctx>,
    function: &mut FunctionContext<'ctx>,
    ptr: Value,
    value: Value,
) -> Result<(), CodegenError> {
    let pointer = lookup_value(context, function, ptr)?.into_pointer_value();
    let runtime = lookup_value(context, function, value)?;
    context
        .builder
        .build_store(pointer, runtime)
        .map_err(builder_error)?;
    Ok(())
}

fn emit_phi<'ctx>(
    context: &mut CodegenContext<'ctx>,
    function: &mut FunctionContext<'ctx>,
    value: Value,
    incoming: &[(Value, thagore_ir::BlockId)],
) -> Result<(), CodegenError> {
    let ty = context.basic_type(type_of(function, value)?)?;
    let phi = context
        .builder
        .build_phi(ty, "phi")
        .map_err(builder_error)?;
    function.values.insert(value, phi.as_basic_value());
    function.pending_phis.insert(
        value,
        crate::context::PendingPhi {
            phi,
            incoming: incoming.to_vec(),
        },
    );
    Ok(())
}

/// Resolves deferred PHI incoming edges once every block value has been emitted.
pub fn resolve_pending_phis<'ctx>(
    context: &CodegenContext<'ctx>,
    function: &mut FunctionContext<'ctx>,
) -> Result<(), CodegenError> {
    let pending = function.pending_phis.drain().collect::<Vec<_>>();
    for (_, pending_phi) in pending {
        let incoming_pairs = pending_phi
            .incoming
            .iter()
            .map(|(incoming_value, block_id)| {
                let runtime = lookup_value(context, function, *incoming_value)?;
                let block =
                    function
                        .blocks
                        .get(block_id)
                        .copied()
                        .ok_or(CodegenError::UnknownBlock {
                            block: *block_id,
                            function: function.name,
                        })?;
                Ok((runtime, block))
            })
            .collect::<Result<Vec<_>, CodegenError>>()?;
        let refs = incoming_pairs
            .iter()
            .map(|(value, block)| (value as &dyn BasicValue<'ctx>, *block))
            .collect::<Vec<_>>();
        pending_phi.phi.add_incoming(&refs);
    }
    Ok(())
}

fn type_of(
    function: &FunctionContext<'_>,
    value: Value,
) -> Result<thagore_typeck::TypeId, CodegenError> {
    function
        .value_types
        .get(value.as_u32() as usize)
        .copied()
        .ok_or(CodegenError::UnknownValue {
            value,
            function: function.name,
        })
}

fn lookup_value<'ctx>(
    _context: &CodegenContext<'ctx>,
    function: &FunctionContext<'ctx>,
    value: Value,
) -> Result<BasicValueEnum<'ctx>, CodegenError> {
    function
        .values
        .get(&value)
        .copied()
        .ok_or(CodegenError::UnknownValue {
            value,
            function: function.name,
        })
}

fn struct_field_index(
    context: &CodegenContext<'_>,
    type_id: thagore_typeck::TypeId,
    field: thagore_ast::InternedStr,
) -> Result<(thagore_ast::InternedStr, usize), CodegenError> {
    match context.types.kind(type_id) {
        TypeKind::Struct(struct_ty) => struct_ty
            .fields
            .iter()
            .enumerate()
            .find(|(_, candidate)| candidate.name == field)
            .map(|(index, _)| (struct_ty.name, index))
            .ok_or(CodegenError::UnknownField {
                struct_name: struct_ty.name,
                field,
                span: None,
            }),
        _ => Err(CodegenError::InvalidOperandType {
            value: Value::INVALID,
            expected: "struct",
            found: type_id,
            span: None,
        }),
    }
}

fn build_load<'ctx>(
    context: &CodegenContext<'ctx>,
    ptr: PointerValue<'ctx>,
    name: &str,
) -> Result<BasicValueEnum<'ctx>, CodegenError> {
    context.builder.build_load(ptr, name).map_err(builder_error)
}

fn builder_error(error: inkwell::builder::BuilderError) -> CodegenError {
    CodegenError::OutputFailed {
        artifact: "llvm-builder".into(),
        message: error.to_string(),
    }
}

fn can_use_exact_signed_div<'ctx>(
    lhs: inkwell::values::IntValue<'ctx>,
    rhs: inkwell::values::IntValue<'ctx>,
) -> bool {
    let Some(divisor) = rhs.get_sign_extended_constant() else {
        return false;
    };
    if divisor == 1 || divisor == -1 {
        return true;
    }

    let Some(dividend) = lhs.get_sign_extended_constant() else {
        return false;
    };
    divisor != 0 && dividend % divisor == 0
}
