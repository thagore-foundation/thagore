//! LLVM instruction emission for Thagore IR instructions.

use inkwell::values::{BasicMetadataValueEnum, BasicValue, BasicValueEnum, PointerValue};
use inkwell::{FloatPredicate, IntPredicate};
use thagore_ir::{ARRAY_LEN_INTRINSIC, BinOp, Const, Instr, Value};
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
            });
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
                    });
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
                    });
                }
            }
        }
        _ => {
            return Err(CodegenError::InvalidOperandType {
                value: operand,
                expected: "numeric or bool",
                found: operand_ty,
                span: None,
            });
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
                .build_int_truncate(
                    len.into_int_value(),
                    context.llvm.i32_type(),
                    "array.len.i32",
                )
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
    let runtime_name = context.symbol_name(name);
    if (runtime_name == "thag_rt_split" || runtime_name == "std__string__split")
        && result_is_string_array(context, function, value)?
    {
        emit_string_split_call(context, function, value, args)?;
        return Ok(());
    }
    if (runtime_name == "thag_rt_join" || runtime_name == "std__string__join")
        && first_arg_is_string_array(context, function, args)?
    {
        emit_string_join_call(context, function, value, args)?;
        return Ok(());
    }
    if let Some(print_runtime) = print_runtime_name(&runtime_name) {
        emit_print_call(context, function, value, print_runtime, args)?;
        return Ok(());
    }

    let param_types = target.get_type().get_param_types();
    let args = args
        .iter()
        .enumerate()
        .map(|(index, value)| {
            let runtime = lookup_value(context, function, *value)?;
            let coerced = coerce_call_argument(context, runtime, param_types.get(index).copied())?;
            Ok(BasicMetadataValueEnum::from(coerced))
        })
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

fn result_is_string_array<'ctx>(
    context: &mut CodegenContext<'ctx>,
    function: &mut FunctionContext<'ctx>,
    value: Value,
) -> Result<bool, CodegenError> {
    Ok(matches!(
        context.types.kind(type_of(function, value)?),
        TypeKind::Array(element) if *element == context.types.str()
    ))
}

fn first_arg_is_string_array<'ctx>(
    context: &mut CodegenContext<'ctx>,
    function: &mut FunctionContext<'ctx>,
    args: &[Value],
) -> Result<bool, CodegenError> {
    let Some(first) = args.first().copied() else {
        return Ok(false);
    };
    Ok(matches!(
        context.types.kind(type_of(function, first)?),
        TypeKind::Array(element) if *element == context.types.str()
    ))
}

fn emit_string_split_call<'ctx>(
    context: &mut CodegenContext<'ctx>,
    function: &mut FunctionContext<'ctx>,
    value: Value,
    args: &[Value],
) -> Result<(), CodegenError> {
    let Some(input_arg) = args.first().copied() else {
        return Err(CodegenError::UnknownFunction {
            name: thagore_ast::InternedStr::new(0),
            span: None,
        });
    };
    let Some(sep_arg) = args.get(1).copied() else {
        return Err(CodegenError::UnknownFunction {
            name: thagore_ast::InternedStr::new(0),
            span: None,
        });
    };

    let i8_ptr = context
        .llvm
        .i8_type()
        .ptr_type(inkwell::AddressSpace::default());
    let i8_ptr_ptr = i8_ptr.ptr_type(inkwell::AddressSpace::default());
    let split_into = context.module.get_function("thag_str_split_into").unwrap_or_else(|| {
        context.module.add_function(
            "thag_str_split_into",
            context.llvm.void_type().fn_type(
                &[
                    i8_ptr.into(),
                    i8_ptr.into(),
                    context
                        .llvm
                        .i64_type()
                        .ptr_type(inkwell::AddressSpace::default())
                        .into(),
                    i8_ptr_ptr.ptr_type(inkwell::AddressSpace::default()).into(),
                ],
                false,
            ),
            None,
        )
    });

    let len_slot = context
        .builder
        .build_alloca(context.llvm.i64_type(), "split.len")
        .map_err(builder_error)?;
    let data_slot = context
        .builder
        .build_alloca(i8_ptr_ptr, "split.data")
        .map_err(builder_error)?;
    let input = lookup_value(context, function, input_arg)?;
    let separator = lookup_value(context, function, sep_arg)?;
    context
        .builder
        .build_call(
            split_into,
            &[
                BasicMetadataValueEnum::from(input),
                BasicMetadataValueEnum::from(separator),
                BasicMetadataValueEnum::from(len_slot.as_basic_value_enum()),
                BasicMetadataValueEnum::from(data_slot.as_basic_value_enum()),
            ],
            "split.call",
        )
        .map_err(builder_error)?;

    let len = build_load(context, len_slot, "split.len.value")?;
    let data = build_load(context, data_slot, "split.data.value")?;
    let result_ty = context.basic_type(type_of(function, value)?)?.into_struct_type();
    let result_ptr = context
        .builder
        .build_alloca(result_ty, "split.result")
        .map_err(builder_error)?;
    let len_ptr = context
        .builder
        .build_struct_gep(result_ptr, 0, "split.result.len")
        .map_err(builder_error)?;
    context
        .builder
        .build_store(len_ptr, len)
        .map_err(builder_error)?;
    let data_ptr = context
        .builder
        .build_struct_gep(result_ptr, 1, "split.result.data")
        .map_err(builder_error)?;
    context
        .builder
        .build_store(data_ptr, data)
        .map_err(builder_error)?;
    let result = build_load(context, result_ptr, "split.result.value")?;
    function.values.insert(value, result);
    Ok(())
}

fn emit_string_join_call<'ctx>(
    context: &mut CodegenContext<'ctx>,
    function: &mut FunctionContext<'ctx>,
    value: Value,
    args: &[Value],
) -> Result<(), CodegenError> {
    let Some(parts_arg) = args.first().copied() else {
        return Err(CodegenError::UnknownFunction {
            name: thagore_ast::InternedStr::new(0),
            span: None,
        });
    };
    let Some(sep_arg) = args.get(1).copied() else {
        return Err(CodegenError::UnknownFunction {
            name: thagore_ast::InternedStr::new(0),
            span: None,
        });
    };

    let i8_ptr = context
        .llvm
        .i8_type()
        .ptr_type(inkwell::AddressSpace::default());
    let i8_ptr_ptr = i8_ptr.ptr_type(inkwell::AddressSpace::default());
    let join_parts = context.module.get_function("thag_str_join_parts").unwrap_or_else(|| {
        context.module.add_function(
            "thag_str_join_parts",
            i8_ptr.fn_type(
                &[context.llvm.i64_type().into(), i8_ptr_ptr.into(), i8_ptr.into()],
                false,
            ),
            None,
        )
    });

    let parts_runtime = lookup_value(context, function, parts_arg)?;
    let parts = match parts_runtime {
        BasicValueEnum::StructValue(struct_value) => struct_value,
        BasicValueEnum::PointerValue(pointer) => {
            build_load(context, pointer, "join.parts.load")?.into_struct_value()
        }
        _ => {
            return Err(CodegenError::InvalidOperandType {
                value: parts_arg,
                expected: "array",
                found: type_of(function, parts_arg)?,
                span: None,
            })
        }
    };
    let len = context
        .builder
        .build_extract_value(parts, 0, "join.parts.len")
        .map_err(builder_error)?;
    let data = context
        .builder
        .build_extract_value(parts, 1, "join.parts.data")
        .map_err(builder_error)?;
    let separator = lookup_value(context, function, sep_arg)?;
    let call = context
        .builder
        .build_call(
            join_parts,
            &[
                BasicMetadataValueEnum::from(len),
                BasicMetadataValueEnum::from(data),
                BasicMetadataValueEnum::from(separator),
            ],
            "join.call",
        )
        .map_err(builder_error)?;
    if let Some(result) = call.try_as_basic_value().left() {
        function.values.insert(value, result);
    }
    Ok(())
}

fn emit_print_call<'ctx>(
    context: &mut CodegenContext<'ctx>,
    function: &mut FunctionContext<'ctx>,
    value: Value,
    runtime_name: &str,
    args: &[Value],
) -> Result<(), CodegenError> {
    let default_target = runtime_print_variant(
        context,
        runtime_name,
        "",
        context.llvm.void_type().fn_type(
            &[context
                .llvm
                .i8_type()
                .ptr_type(inkwell::AddressSpace::default())
                .into()],
            false,
        ),
    );
    let Some(arg_id) = args.first().copied() else {
        let call = context
            .builder
            .build_call(default_target, &[], "call")
            .map_err(builder_error)?;
        if let Some(result) = call.try_as_basic_value().left() {
            function.values.insert(value, result);
        }
        return Ok(());
    };

    let runtime = lookup_value(context, function, arg_id)?;
    let (target, arg) = select_print_target(context, runtime_name, default_target, runtime)?;
    let call = context
        .builder
        .build_call(target, &[BasicMetadataValueEnum::from(arg)], "call")
        .map_err(builder_error)?;
    if let Some(result) = call.try_as_basic_value().left() {
        function.values.insert(value, result);
    }
    Ok(())
}

fn coerce_call_argument<'ctx>(
    context: &mut CodegenContext<'ctx>,
    value: BasicValueEnum<'ctx>,
    target: Option<inkwell::types::BasicTypeEnum<'ctx>>,
) -> Result<BasicValueEnum<'ctx>, CodegenError> {
    let Some(target) = target else {
        return Ok(value);
    };

    match (value, target) {
        (BasicValueEnum::IntValue(int_value), inkwell::types::BasicTypeEnum::IntType(int_type))
            if int_value.get_type().get_bit_width() == 32 && int_type.get_bit_width() == 64 =>
        {
            Ok(context
                .builder
                .build_int_s_extend(int_value, int_type, "call.arg.sext")
                .map_err(builder_error)?
                .as_basic_value_enum())
        }
        (other, _) => Ok(other),
    }
}

fn select_print_target<'ctx>(
    context: &mut CodegenContext<'ctx>,
    runtime_name: &str,
    default_target: inkwell::values::FunctionValue<'ctx>,
    value: BasicValueEnum<'ctx>,
) -> Result<(inkwell::values::FunctionValue<'ctx>, BasicValueEnum<'ctx>), CodegenError> {
    match value {
        BasicValueEnum::PointerValue(_) => Ok((default_target, value)),
        BasicValueEnum::IntValue(int_value) => {
            if int_value.get_type().get_bit_width() == 1 {
                let target = runtime_print_variant(
                    context,
                    runtime_name,
                    "bool",
                    context
                        .llvm
                        .void_type()
                        .fn_type(&[context.llvm.bool_type().into()], false),
                );
                Ok((target, int_value.as_basic_value_enum()))
            } else {
                let widened = if int_value.get_type().get_bit_width() == 32 {
                    context
                        .builder
                        .build_int_s_extend(int_value, context.llvm.i64_type(), "print.i64")
                        .map_err(builder_error)?
                } else {
                    int_value
                };
                let target = runtime_print_variant(
                    context,
                    runtime_name,
                    "i64",
                    context
                        .llvm
                        .void_type()
                        .fn_type(&[context.llvm.i64_type().into()], false),
                );
                Ok((target, widened.as_basic_value_enum()))
            }
        }
        BasicValueEnum::FloatValue(float_value) => {
            let target = runtime_print_variant(
                context,
                runtime_name,
                "f64",
                context
                    .llvm
                    .void_type()
                    .fn_type(&[context.llvm.f64_type().into()], false),
            );
            Ok((target, float_value.as_basic_value_enum()))
        }
        other => Ok((default_target, other)),
    }
}

fn runtime_print_variant<'ctx>(
    context: &mut CodegenContext<'ctx>,
    runtime_name: &str,
    suffix: &str,
    function_type: inkwell::types::FunctionType<'ctx>,
) -> inkwell::values::FunctionValue<'ctx> {
    let name = if suffix.is_empty() {
        runtime_name.to_string()
    } else {
        format!("{runtime_name}_{suffix}")
    };
    context
        .module
        .get_function(&name)
        .unwrap_or_else(|| context.module.add_function(&name, function_type, None))
}

fn print_runtime_name(name: &str) -> Option<&'static str> {
    match name {
        "thagore_print" | "print" | "std__io__print" => Some("thagore_print"),
        "thagore_println" | "println" | "std__io__println" => Some("thagore_println"),
        "thagore_eprint" | "eprint" | "std__io__eprint" => Some("thagore_eprint"),
        "thagore_eprintln" | "eprintln" | "std__io__eprintln" => Some("thagore_eprintln"),
        _ => None,
    }
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
        .build_extract_value(
            object_value.into_struct_value(),
            field_index as u32,
            "field.load",
        )
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
            });
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
        let incoming_pairs =
            pending_phi
                .incoming
                .iter()
                .map(|(incoming_value, block_id)| {
                    let runtime = lookup_value(context, function, *incoming_value)?;
                    let block = function.blocks.get(block_id).copied().ok_or(
                        CodegenError::UnknownBlock {
                            block: *block_id,
                            function: function.name,
                        },
                    )?;
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
