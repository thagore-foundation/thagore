#include "internal.hpp"

namespace thagc::codegen {

std::size_t field_index_for_struct(const std::string& struct_name, const std::string& field_name,
                                          const std::unordered_map<std::string, std::vector<std::string>>& struct_fields) {
  auto fields_it = struct_fields.find(struct_name);
  if (fields_it == struct_fields.end()) {
    return static_cast<std::size_t>(-1);
  }
  const auto& fields = fields_it->second;
  auto found = std::find(fields.begin(), fields.end(), field_name);
  if (found == fields.end()) {
    return static_cast<std::size_t>(-1);
  }
  return static_cast<std::size_t>(std::distance(fields.begin(), found));
}

llvm::AllocaInst* create_entry_alloca(llvm::Function* fn, llvm::Type* type, const std::string& name) {
  llvm::IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().begin());
  return tmp.CreateAlloca(type, nullptr, name);
}

llvm::Value* create_global_cstr_ptr(llvm::IRBuilder<>& builder, const std::string& value,
                                           const std::string& name_hint) {
  llvm::GlobalVariable* global = builder.CreateGlobalString(value, name_hint);
  llvm::Value* zero = builder.getInt32(0);
  return builder.CreateInBoundsGEP(global->getValueType(), global, {zero, zero}, name_hint + ".ptr");
}

std::unique_ptr<llvm::Module> build_module(llvm::LLVMContext& context, const std::string& module_name,
                                                  const lowering::CoreProgram& core,
                                                  support::DiagnosticSink& diag) {
  auto module = std::make_unique<llvm::Module>(module_name, context);
  llvm::IRBuilder<> builder(context);

  std::vector<lowering::CoreFunction> functions = core.functions;
  if (functions.empty()) {
    lowering::CoreFunction legacy_main;
    legacy_main.name = "main";
    legacy_main.return_type = "i32";
    legacy_main.return_literal = core.main_return_literal;
    legacy_main.return_expression = core.main_return_expression;
    legacy_main.return_expression_ast = core.main_return_expression_ast;
    legacy_main.statements = core.main_statements;
    functions.push_back(std::move(legacy_main));
  }

  std::unordered_map<std::string, llvm::StructType*> llvm_struct_types;
  for (const auto& struct_def : core.struct_types) {
    if (struct_def.name.empty()) {
      continue;
    }
    llvm_struct_types[struct_def.name] = llvm::StructType::create(context, struct_def.name);
  }
  for (const auto& struct_def : core.struct_types) {
    auto llvm_struct_it = llvm_struct_types.find(struct_def.name);
    if (llvm_struct_it == llvm_struct_types.end() || llvm_struct_it->second == nullptr) {
      continue;
    }
    std::vector<llvm::Type*> field_types;
    field_types.reserve(struct_def.field_types.size());
    for (std::size_t i = 0; i < struct_def.field_types.size(); ++i) {
      const std::string& field_type_name = struct_def.field_types[i];
      auto nested_struct_it = llvm_struct_types.find(field_type_name);
      if (nested_struct_it != llvm_struct_types.end() && nested_struct_it->second != nullptr) {
        field_types.push_back(nested_struct_it->second);
        continue;
      }
      field_types.push_back(llvm_type_from_value_type(value_type_from_return_type(field_type_name), builder));
    }
    llvm_struct_it->second->setBody(field_types, false);
  }

  std::unordered_map<std::string, llvm::Function*> llvm_functions;
  std::unordered_map<std::string, ValueType> function_returns;
  std::unordered_map<std::string, bool> function_async_flags;
  for (const auto& ext : core.extern_functions) {
    std::vector<llvm::Type*> params;
    params.reserve(ext.param_types.size());
    for (const auto& param : ext.param_types) {
      params.push_back(llvm_type_from_value_type(value_type_from_return_type(param), builder));
    }
    llvm::FunctionType* fn_type =
        llvm::FunctionType::get(llvm_type_from_value_type(value_type_from_return_type(ext.return_type), builder),
                                params, false);
    llvm::Function* fn =
        llvm::Function::Create(fn_type, llvm::GlobalValue::ExternalLinkage, ext.name, module.get());
    llvm_functions[ext.name] = fn;
    function_returns[ext.name] = value_type_from_return_type(ext.return_type);
    function_async_flags[ext.name] = false;
  }
  for (const auto& fn_def : functions) {
    ValueType return_type = value_type_from_return_type(fn_def.return_type);
    if (fn_def.name == "main") {
      return_type = ValueType::I32;
    }
    std::vector<llvm::Type*> params;
    params.reserve(fn_def.params.size() + (fn_def.is_method ? 1 : 0));
    if (fn_def.is_method) {
      auto owner_it = llvm_struct_types.find(fn_def.owner_type);
      if (owner_it == llvm_struct_types.end() || owner_it->second == nullptr) {
        diag.error("E2026", "missing struct layout for method owner '" + fn_def.owner_type + "'");
        return nullptr;
      }
      params.push_back(pointer_type(builder));
    }
    for (std::size_t i = 0; i < fn_def.params.size(); ++i) {
      ValueType param_type = ValueType::I32;
      if (i < fn_def.param_types.size()) {
        const ValueType annotated = value_type_from_return_type(fn_def.param_types[i]);
        if (annotated != ValueType::Invalid) {
          param_type = annotated;
        }
      }
      params.push_back(llvm_type_from_value_type(param_type, builder));
    }
    llvm::FunctionType* fn_type =
        llvm::FunctionType::get(llvm_type_from_value_type(return_type, builder), params, false);
    const auto linkage = fn_def.name == "main" ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::InternalLinkage;
    llvm::Function* fn = llvm::Function::Create(fn_type, linkage, fn_def.name, module.get());
    if (fn_def.name != "main" && !fn_def.is_async && fn_def.statements.size() <= 3) {
      fn->addFnAttr(llvm::Attribute::InlineHint);
    }
    llvm_functions[fn_def.name] = fn;
    function_returns[fn_def.name] = return_type;
    function_async_flags[fn_def.name] = fn_def.is_async && function_contains_await(fn_def);
  }

  for (const auto& fn_def : functions) {
    llvm::Function* fn = llvm_functions[fn_def.name];
    if (fn == nullptr) {
      diag.error("E2025", "internal backend error: missing llvm function '" + fn_def.name + "'");
      return nullptr;
    }
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, fn_def.name + ".entry", fn);
    builder.SetInsertPoint(entry);
    AsyncLoweringContext async_ctx;
    async_ctx.enabled = function_async_flags[fn_def.name];
    async_ctx.expected_return_type = function_returns[fn_def.name];
    if (async_ctx.enabled) {
      fn->addFnAttr("presplitcoroutine");
      llvm::Type* ptr_ty = pointer_type(builder);
      llvm::Value* null_ptr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty));
      llvm::Function* coro_id_decl = llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::coro_id);
      llvm::Function* coro_size_decl =
          llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::coro_size, {builder.getInt64Ty()});
      llvm::Function* coro_begin_decl = llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::coro_begin);
      llvm::Function* coro_save_decl = llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::coro_save);
      llvm::Function* coro_suspend_decl =
          llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::coro_suspend);
      llvm::Function* coro_end_decl = llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::coro_end);
      llvm::Function* coro_free_decl = llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::coro_free);
      llvm::FunctionCallee malloc_fn = module->getOrInsertFunction(
          "malloc", llvm::FunctionType::get(ptr_ty, {builder.getInt64Ty()}, false));

      async_ctx.coro_id =
          builder.CreateCall(coro_id_decl, {builder.getInt32(0), null_ptr, null_ptr, null_ptr}, "coro.id");
      llvm::Value* coro_size = builder.CreateCall(coro_size_decl, {}, "coro.size");
      llvm::Value* coro_frame = builder.CreateCall(malloc_fn, {coro_size}, "coro.frame");
      async_ctx.coro_handle = builder.CreateCall(coro_begin_decl, {async_ctx.coro_id, coro_frame}, "coro.begin");
      async_ctx.coro_save = coro_save_decl;
      async_ctx.coro_suspend = coro_suspend_decl;
      async_ctx.coro_end = coro_end_decl;
      async_ctx.coro_free = coro_free_decl;
      async_ctx.free_fn = module->getOrInsertFunction(
          "free", llvm::FunctionType::get(builder.getVoidTy(), {pointer_type(builder)}, false));
      async_ctx.finalize_block = llvm::BasicBlock::Create(context, fn_def.name + ".async.finalize", fn);
      if (async_ctx.expected_return_type != ValueType::Void) {
        async_ctx.return_slot = create_entry_alloca(
            fn, llvm_type_from_value_type(async_ctx.expected_return_type, builder), fn_def.name + ".async.ret");
        llvm::Value* init_ret = default_value_for_type(async_ctx.expected_return_type, builder);
        if (init_ret != nullptr) {
          builder.CreateStore(init_ret, async_ctx.return_slot);
        }
      }
    }

    std::unordered_map<std::string, VariableSlot> variables;
    std::unordered_map<std::string, StructInstance> struct_instances;
    std::unordered_map<std::string, TupleInstance> tuple_instances;
    std::unordered_map<std::string, ArrayInstance> array_instances;
    std::vector<LoopControlTarget> loop_targets;
    std::unordered_map<std::string, ClosureDef> closures;

    std::size_t arg_index = 0;
    std::size_t param_index = 0;
    for (llvm::Argument& arg : fn->args()) {
      if (fn_def.is_method && arg_index == 0) {
        auto owner_it = llvm_struct_types.find(fn_def.owner_type);
        if (owner_it == llvm_struct_types.end() || owner_it->second == nullptr) {
          diag.error("E2026", "missing struct layout for method owner '" + fn_def.owner_type + "'");
          return nullptr;
        }
        struct_instances["self"] = StructInstance{&arg, owner_it->second, fn_def.owner_type};
        ++arg_index;
        continue;
      }
      if (param_index >= fn_def.params.size()) {
        break;
      }
      const std::string& param_name = fn_def.params[param_index];
      ValueType param_type = ValueType::I32;
      if (param_index < fn_def.param_types.size()) {
        const ValueType annotated = value_type_from_return_type(fn_def.param_types[param_index]);
        if (annotated != ValueType::Invalid) {
          param_type = annotated;
        }
      }
      llvm::Type* param_llvm_type = llvm_type_from_value_type(param_type, builder);
      llvm::AllocaInst* alloca = create_entry_alloca(fn, param_llvm_type, param_name);
      builder.CreateStore(&arg, alloca);
      variables[param_name] = VariableSlot{alloca, param_type};
      ++param_index;
      ++arg_index;
    }

    std::size_t index = 0;
    if (!emit_block(fn_def.statements, core.enum_variant_tags, core.struct_fields, core.struct_field_types,
                    llvm_struct_types, llvm_functions, function_returns, function_async_flags,
                    function_returns[fn_def.name], index, -1, fn,
                    module.get(), builder, variables, struct_instances, tuple_instances, array_instances, loop_targets,
                    closures, &async_ctx,
                    diag)) {
      return nullptr;
    }

    auto drop_owned_variables = [&]() {
      for (auto& [_, slot] : variables) {
        if (slot.type != ValueType::I8Ptr || slot.alloca == nullptr) {
          continue;
        }
        if (slot.ownership != VariableSlot::Ownership::Rc && slot.ownership != VariableSlot::Ownership::Arc) {
          continue;
        }
        llvm::Value* handle = builder.CreateLoad(slot.alloca->getAllocatedType(), slot.alloca);
        emit_ref_drop_call(slot.ownership, handle, fn, builder);
      }
    };

    const ValueType expected = function_returns[fn_def.name];
    if (async_ctx.enabled) {
      if (!builder.GetInsertBlock()->getTerminator()) {
        drop_owned_variables();
        if (expected != ValueType::Void && async_ctx.return_slot != nullptr) {
          llvm::Value* fallback = nullptr;
          if (!trim(fn_def.return_expression).empty()) {
            ExprValue value = evaluate_expression(fn_def.return_expression_ast, fn_def.return_expression, builder, variables,
                                                  core.enum_variant_tags, core.struct_fields, core.struct_field_types,
                                                  struct_instances, tuple_instances, array_instances, llvm_functions,
                                                  function_returns, &closures, fn, diag);
            fallback = cast_value_to_type(value, expected, builder);
          }
          if (fallback == nullptr) {
            if (expected == ValueType::I32) {
              fallback = builder.getInt32(fn_def.return_literal);
            } else if (expected == ValueType::I64) {
              fallback = builder.getInt64(fn_def.return_literal);
            } else {
              fallback = default_value_for_type(expected, builder);
            }
          }
          if (fallback != nullptr) {
            builder.CreateStore(fallback, async_ctx.return_slot);
          }
        }
        builder.CreateBr(async_ctx.finalize_block);
      }

      builder.SetInsertPoint(async_ctx.finalize_block);
      if (!builder.GetInsertBlock()->getTerminator()) {
        llvm::Value* save = builder.CreateCall(async_ctx.coro_save, {async_ctx.coro_handle}, "coro.final.save");
        llvm::Value* suspend = builder.CreateCall(async_ctx.coro_suspend, {save, builder.getTrue()}, "coro.final.suspend");

        auto* final_suspend_ret_bb =
            llvm::BasicBlock::Create(context, fn_def.name + ".async.final.suspend.ret", fn);
        auto* final_cleanup_bb = llvm::BasicBlock::Create(context, fn_def.name + ".async.final.cleanup", fn);
        auto* final_resume_bb = llvm::BasicBlock::Create(context, fn_def.name + ".async.final.resume", fn);
        llvm::SwitchInst* final_sw = builder.CreateSwitch(suspend, final_resume_bb, 2);
        final_sw->addCase(builder.getInt8(1), final_cleanup_bb);
        final_sw->addCase(builder.getInt8(2), final_cleanup_bb);

        builder.SetInsertPoint(final_resume_bb);
        builder.CreateBr(final_suspend_ret_bb);

        builder.SetInsertPoint(final_cleanup_bb);
        llvm::Value* frame = builder.CreateCall(async_ctx.coro_free, {async_ctx.coro_id, async_ctx.coro_handle},
                                                "coro.final.free.frame");
        llvm::Value* has_frame = builder.CreateIsNotNull(frame, "coro.final.has.frame");
        auto* final_free_bb = llvm::BasicBlock::Create(context, fn_def.name + ".async.final.free", fn);
        auto* final_no_free_bb = llvm::BasicBlock::Create(context, fn_def.name + ".async.final.no_free", fn);
        builder.CreateCondBr(has_frame, final_free_bb, final_no_free_bb);

        builder.SetInsertPoint(final_free_bb);
        builder.CreateCall(async_ctx.free_fn, {frame});
        builder.CreateBr(final_suspend_ret_bb);

        builder.SetInsertPoint(final_no_free_bb);
        builder.CreateBr(final_suspend_ret_bb);

        builder.SetInsertPoint(final_suspend_ret_bb);
        builder.CreateCall(async_ctx.coro_end, {async_ctx.coro_handle, builder.getFalse(), save});
        if (expected == ValueType::Void) {
          builder.CreateRetVoid();
        } else {
          llvm::Value* ret_value = async_ctx.return_slot == nullptr
                                       ? default_value_for_type(expected, builder)
                                       : builder.CreateLoad(
                                             llvm_type_from_value_type(expected, builder), async_ctx.return_slot);
          if (ret_value == nullptr) {
            ret_value = default_value_for_type(expected, builder);
          }
          builder.CreateRet(ret_value);
        }
      }
    } else if (!builder.GetInsertBlock()->getTerminator()) {
      drop_owned_variables();
      if (expected == ValueType::Void) {
        builder.CreateRetVoid();
      } else {
        llvm::Value* fallback = nullptr;
        if (!trim(fn_def.return_expression).empty()) {
          ExprValue value = evaluate_expression(fn_def.return_expression_ast, fn_def.return_expression, builder, variables,
                                                core.enum_variant_tags, core.struct_fields, core.struct_field_types,
                                                struct_instances, tuple_instances, array_instances, llvm_functions,
                                                function_returns, &closures, fn, diag);
          fallback = cast_value_to_type(value, expected, builder);
        }
        if (fallback == nullptr) {
          if (expected == ValueType::I1) {
            fallback = builder.getInt1(false);
          } else if (expected == ValueType::I64) {
            fallback = builder.getInt64(fn_def.return_literal);
          } else if (expected == ValueType::F32) {
            fallback = llvm::ConstantFP::get(builder.getFloatTy(), 0.0);
          } else if (expected == ValueType::F64) {
            fallback = llvm::ConstantFP::get(builder.getDoubleTy(), 0.0);
          } else {
            fallback = builder.getInt32(fn_def.return_literal);
          }
        }
        builder.CreateRet(fallback);
      }
    }
  }

  if (llvm_functions.find("main") == llvm_functions.end()) {
    llvm::FunctionType* fn_type = llvm::FunctionType::get(builder.getInt32Ty(), false);
    llvm::Function* fn = llvm::Function::Create(fn_type, llvm::GlobalValue::ExternalLinkage, "main", module.get());
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "main.entry", fn);
    builder.SetInsertPoint(entry);
    builder.CreateRet(builder.getInt32(core.main_return_literal));
  }

  return module;
}


}  // namespace thagc::codegen
