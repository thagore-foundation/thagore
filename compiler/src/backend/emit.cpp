#include "internal.hpp"

namespace thagc::codegen {

llvm::Value* emit_ref_new_call(VariableSlot::Ownership ownership, llvm::Value* value_i64,
                                      llvm::Function* fn, llvm::IRBuilder<>& builder) {
  if (value_i64 == nullptr || fn == nullptr) {
    return nullptr;
  }
  llvm::Module* module = fn->getParent();
  if (module == nullptr) {
    return nullptr;
  }
  const char* symbol = ownership == VariableSlot::Ownership::Arc ? "thag_arc_new" : "thag_rc_new";
  llvm::FunctionCallee callee =
      module->getOrInsertFunction(symbol, llvm::FunctionType::get(pointer_type(builder), {builder.getInt64Ty(), pointer_type(builder)}, false));
  llvm::AllocaInst* tmp = create_entry_alloca(fn, builder.getInt64Ty(), std::string(symbol) + ".tmp");
  builder.CreateStore(value_i64, tmp);
  return builder.CreateCall(callee, {builder.getInt64(8), tmp});
}

llvm::Value* emit_ref_clone_call(VariableSlot::Ownership ownership, llvm::Value* handle, llvm::Function* fn,
                                        llvm::IRBuilder<>& builder) {
  if (handle == nullptr || fn == nullptr) {
    return nullptr;
  }
  llvm::Module* module = fn->getParent();
  if (module == nullptr) {
    return nullptr;
  }
  const char* symbol = ownership == VariableSlot::Ownership::Arc ? "thag_arc_clone" : "thag_rc_clone";
  llvm::FunctionCallee callee =
      module->getOrInsertFunction(symbol, llvm::FunctionType::get(pointer_type(builder), {pointer_type(builder)}, false));
  return builder.CreateCall(callee, {handle});
}

void emit_ref_drop_call(VariableSlot::Ownership ownership, llvm::Value* handle, llvm::Function* fn,
                               llvm::IRBuilder<>& builder) {
  if (handle == nullptr || fn == nullptr) {
    return;
  }
  llvm::Module* module = fn->getParent();
  if (module == nullptr) {
    return;
  }
  const char* symbol = ownership == VariableSlot::Ownership::Arc ? "thag_arc_drop" : "thag_rc_drop";
  llvm::FunctionCallee callee =
      module->getOrInsertFunction(symbol, llvm::FunctionType::get(builder.getVoidTy(), {pointer_type(builder)}, false));
  builder.CreateCall(callee, {handle});
}

void emit_await_spawn_wrapper(llvm::IRBuilder<>& builder, llvm::Function* fn, llvm::Value* coro_handle) {
  if (fn == nullptr || fn->getParent() == nullptr) {
    return;
  }
  llvm::Module* module = fn->getParent();
  llvm::FunctionCallee spawn_fn =
      module->getOrInsertFunction("thag_task_scope_spawn",
                                  llvm::FunctionType::get(builder.getInt32Ty(),
                                                          {pointer_type(builder), pointer_type(builder), pointer_type(builder)},
                                                          false));
  llvm::FunctionCallee done_fn =
      module->getOrInsertFunction("thag_coro_done",
                                  llvm::FunctionType::get(builder.getInt1Ty(), {pointer_type(builder)}, false));
  llvm::FunctionCallee resume_fn =
      module->getOrInsertFunction("thag_coro_resume",
                                  llvm::FunctionType::get(builder.getVoidTy(), {pointer_type(builder)}, false));

  llvm::Value* effective_handle = coro_handle;
  if (effective_handle == nullptr) {
    effective_handle = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(pointer_type(builder)));
  }
  llvm::Value* done = builder.CreateCall(done_fn, {effective_handle}, "await.done");
  auto* spawn_bb = llvm::BasicBlock::Create(builder.getContext(), "await.spawn", fn);
  auto* cont_bb = llvm::BasicBlock::Create(builder.getContext(), "await.spawn.cont", fn);
  builder.CreateCondBr(done, cont_bb, spawn_bb);

  builder.SetInsertPoint(spawn_bb);
  llvm::Value* null_scope = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(pointer_type(builder)));
  llvm::Value* resume_ptr = builder.CreateBitCast(resume_fn.getCallee(), pointer_type(builder));
  builder.CreateCall(spawn_fn, {null_scope, resume_ptr, effective_handle});
  builder.CreateBr(cont_bb);

  builder.SetInsertPoint(cont_bb);
}

bool emit_async_suspend_point(AsyncLoweringContext* async_ctx, llvm::IRBuilder<>& builder, llvm::Function* fn,
                                     support::DiagnosticSink& diag) {
  if (async_ctx == nullptr || !async_ctx->enabled) {
    return true;
  }
  if (fn == nullptr || fn->getParent() == nullptr || async_ctx->coro_handle == nullptr ||
      async_ctx->finalize_block == nullptr) {
    diag.error("E2065", "internal async lowering error: missing coroutine context");
    return false;
  }

  llvm::Value* save = builder.CreateCall(async_ctx->coro_save, {async_ctx->coro_handle}, "await.save");
  llvm::Value* suspend = builder.CreateCall(async_ctx->coro_suspend, {save, builder.getFalse()}, "await.suspend");

  auto* resume_bb = llvm::BasicBlock::Create(builder.getContext(), "await.resume", fn);
  auto* finalize_bb = llvm::BasicBlock::Create(builder.getContext(), "await.finalize", fn);
  llvm::SwitchInst* sw = builder.CreateSwitch(suspend, resume_bb, 2);
  sw->addCase(builder.getInt8(1), finalize_bb);
  sw->addCase(builder.getInt8(2), finalize_bb);

  builder.SetInsertPoint(finalize_bb);
  builder.CreateBr(async_ctx->finalize_block);

  builder.SetInsertPoint(resume_bb);
  return true;
}

static std::size_t find_block_end(const std::vector<lowering::CoreStmt>& stmts, std::size_t start_index,
                                  int parent_indent) {
  std::size_t i = start_index;
  while (i < stmts.size() && stmts[i].indent > parent_indent) {
    ++i;
  }
  return i;
}

bool function_contains_await(const lowering::CoreFunction& fn_def) {
  for (const auto& st : fn_def.statements) {
    if (st.has_await) {
      return true;
    }
  }
  return starts_with(trim(fn_def.return_expression), "await ");
}

bool parse_loop_header_label(const std::string& line, const std::string& keyword, std::string& label,
                                    std::string& normalized) {
  label.clear();
  normalized = trim(line);
  if (starts_with(normalized, keyword + " ")) {
    return true;
  }
  const std::size_t colon = normalized.find(':');
  if (colon == std::string::npos || colon + 1 >= normalized.size()) {
    return false;
  }
  std::string maybe_label = trim(normalized.substr(0, colon));
  normalized = trim(normalized.substr(colon + 1));
  if (!starts_with(normalized, keyword + " ")) {
    return false;
  }
  if (!maybe_label.empty() && maybe_label.front() == '\'') {
    maybe_label = trim(maybe_label.substr(1));
  }
  if (maybe_label.empty() || !is_ident_start(maybe_label[0])) {
    return false;
  }
  for (std::size_t i = 1; i < maybe_label.size(); ++i) {
    if (!is_ident_body(maybe_label[i])) {
      return false;
    }
  }
  label = maybe_label;
  return true;
}

bool parse_loop_control_statement(const std::string& line, const std::string& keyword, std::string& label) {
  label.clear();
  const std::string clean = trim(line);
  if (clean == keyword) {
    return true;
  }
  if (!starts_with(clean, keyword + " ")) {
    return false;
  }
  std::string rest = trim(clean.substr(keyword.size()));
  if (!rest.empty() && rest.front() == '\'') {
    rest = trim(rest.substr(1));
  }
  if (rest.empty() || !is_ident_start(rest[0])) {
    return false;
  }
  for (std::size_t i = 1; i < rest.size(); ++i) {
    if (!is_ident_body(rest[i])) {
      return false;
    }
  }
  label = rest;
  return true;
}

ForHeader parse_for_header(const lowering::CoreStmt& st) {
  ForHeader out;
  std::string line;
  if (!parse_loop_header_label(st.text, "for", out.label, line) || !ends_with(line, ":")) {
    return out;
  }
  line = trim(line.substr(4, line.size() - 5));
  const std::size_t in_pos = line.find(" in ");
  if (in_pos == std::string::npos || in_pos == 0) {
    return out;
  }
  out.loop_var = trim(line.substr(0, in_pos));
  if (out.loop_var.empty()) {
    return out;
  }
  std::string range = trim(line.substr(in_pos + 4));
  const std::size_t dots = range.find("..");
  if (dots == std::string::npos) {
    return out;
  }
  out.start_expr = trim(range.substr(0, dots));
  out.end_expr = trim(range.substr(dots + 2));
  if (out.start_expr.empty() || out.end_expr.empty()) {
    return out;
  }
  out.ok = true;
  return out;
}

MatchArmLabel parse_match_arm_label(const std::string& line,
                                           const std::unordered_map<std::string, int>& enum_variant_tags) {
  MatchArmLabel out;
  std::string text = trim(line);
  if (!ends_with(text, ":")) {
    return out;
  }
  text = trim(text.substr(0, text.size() - 1));
  if (text == "_" || text == "else") {
    out.valid = true;
    out.wildcard = true;
    return out;
  }
  if (text == "true") {
    out.valid = true;
    out.value = 1;
    return out;
  }
  if (text == "false") {
    out.valid = true;
    out.value = 0;
    return out;
  }
  std::string variant_name = text;
  std::string payload_binding;
  const std::size_t lparen = text.find('(');
  const std::size_t rparen = text.rfind(')');
  if (lparen != std::string::npos || rparen != std::string::npos) {
    if (lparen == std::string::npos || rparen == std::string::npos || rparen <= lparen) {
      return out;
    }
    variant_name = trim(text.substr(0, lparen));
    payload_binding = trim(text.substr(lparen + 1, rparen - lparen - 1));
    if (payload_binding == "_") {
      payload_binding.clear();
    } else if (!payload_binding.empty()) {
      if (!is_ident_start(payload_binding[0])) {
        return out;
      }
      for (std::size_t i = 1; i < payload_binding.size(); ++i) {
        if (!is_ident_body(payload_binding[i])) {
          return out;
        }
      }
    }
  }

  const auto variant_it = enum_variant_tags.find(variant_name);
  if (variant_it != enum_variant_tags.end()) {
    out.valid = true;
    out.value = variant_it->second;
    out.enum_variant = true;
    out.payload_binding = payload_binding;
    return out;
  }
  if (const std::optional<int> builtin = builtin_variant_tag(variant_name); builtin.has_value()) {
    out.valid = true;
    out.value = *builtin;
    out.enum_variant = true;
    out.payload_binding = payload_binding;
    return out;
  }
  bool neg = false;
  std::size_t i = 0;
  if (!text.empty() && text[0] == '-') {
    neg = true;
    i = 1;
  }
  if (i >= text.size()) {
    return out;
  }
  for (std::size_t k = i; k < text.size(); ++k) {
    if (!std::isdigit(static_cast<unsigned char>(text[k]))) {
      return out;
    }
  }
  long long parsed = 0;
  try {
    parsed = std::stoll(text.substr(i));
  } catch (const std::exception&) {
    return out;
  }
  if (parsed > std::numeric_limits<int>::max() || parsed < std::numeric_limits<int>::min()) {
    return out;
  }
  out.valid = true;
  const int as_int = static_cast<int>(parsed);
  out.value = neg ? -as_int : as_int;
  return out;
}

bool emit_block(const std::vector<lowering::CoreStmt>& stmts,
                       const std::unordered_map<std::string, int>& enum_variant_tags,
                       const std::unordered_map<std::string, std::vector<std::string>>& struct_fields,
                       const std::unordered_map<std::string, std::string>& struct_field_types,
                       const std::unordered_map<std::string, llvm::StructType*>& llvm_struct_types,
                       const std::unordered_map<std::string, llvm::Function*>& functions,
                       const std::unordered_map<std::string, ValueType>& function_returns,
                       const std::unordered_map<std::string, bool>& function_async_flags,
                       ValueType expected_return_type,
                       std::size_t& index, int parent_indent, llvm::Function* fn, llvm::Module* module,
                       llvm::IRBuilder<>& builder, std::unordered_map<std::string, VariableSlot>& variables,
                       std::unordered_map<std::string, StructInstance>& struct_instances,
                       std::unordered_map<std::string, TupleInstance>& tuple_instances,
                       std::unordered_map<std::string, ArrayInstance>& array_instances,
                       std::vector<LoopControlTarget>& loop_targets,
                       std::unordered_map<std::string, ClosureDef>& closures, AsyncLoweringContext* async_ctx,
                       support::DiagnosticSink& diag) {
  llvm::FunctionCallee printf_fn = module->getOrInsertFunction(
      "printf",
      llvm::FunctionType::get(builder.getInt32Ty(), pointer_type(builder), true));
  llvm::Value* printf_i32_fmt = create_global_cstr_ptr(builder, "%d\n", "printf_i32_fmt");
  llvm::Value* printf_i64_fmt = create_global_cstr_ptr(builder, "%lld\n", "printf_i64_fmt");
  llvm::Value* printf_f64_fmt = create_global_cstr_ptr(builder, "%f\n", "printf_f64_fmt");
  llvm::Value* printf_str_fmt = create_global_cstr_ptr(builder, "%s\n", "printf_str_fmt");
  llvm::Value* printf_i32_raw_fmt = create_global_cstr_ptr(builder, "%d", "printf_i32_raw_fmt");
  llvm::Value* printf_i64_raw_fmt = create_global_cstr_ptr(builder, "%lld", "printf_i64_raw_fmt");
  llvm::Value* printf_f64_raw_fmt = create_global_cstr_ptr(builder, "%f", "printf_f64_raw_fmt");
  llvm::Value* printf_raw_str_fmt = create_global_cstr_ptr(builder, "%s", "printf_raw_str_fmt");
  llvm::Value* printf_newline_fmt = create_global_cstr_ptr(builder, "\n", "printf_newline_fmt");
  std::vector<lowering::CoreStmt> deferred;

  auto flush_deferred = [&]() {
    for (auto it = deferred.rbegin(); it != deferred.rend(); ++it) {
      const std::string deferred_line = it->has_expression ? trim(it->expression) : trim(it->text);
      if (!emit_expression_statement(deferred_line, it->has_expression, it->expression, it->expression_ast, builder, fn, printf_fn,
                                     printf_i32_fmt, printf_f64_fmt, printf_i64_fmt, printf_str_fmt,
                                     printf_i32_raw_fmt, printf_i64_raw_fmt,
                                     printf_f64_raw_fmt, printf_raw_str_fmt, printf_newline_fmt, variables,
                                     enum_variant_tags, struct_fields, struct_field_types, struct_instances,
                                     tuple_instances, array_instances, functions, function_returns, function_async_flags,
                                     closures, it->has_await, async_ctx, diag)) {
        return false;
      }
    }
    deferred.clear();
    return true;
  };

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

  auto eval_with_try = [&](const std::string& expr_text, const syntax::AstExprPtr& expr_ast, bool has_await, ExprValue& out_value) {
    std::string try_inner;
    if (!parse_try_operator_expr(expr_text, try_inner)) {
      out_value = evaluate_expression(expr_ast, expr_text, builder, variables, enum_variant_tags, struct_fields,
                                      struct_field_types, struct_instances, tuple_instances, array_instances, functions,
                                      function_returns, &closures, fn, diag);
      if (out_value.value == nullptr || out_value.type == ValueType::Invalid) {
        return false;
      }
      if (has_await &&
          !emit_await_semantics_for_expression(expr_text, function_async_flags, async_ctx, builder, fn, diag)) {
        return false;
      }
      return true;
    }
    if (expected_return_type != ValueType::I32) {
      diag.error("E2053", "operator '?' requires function returning Result/Option-compatible i32 encoding");
      return false;
    }
    ExprValue encoded = evaluate_expression(try_inner, builder, variables, enum_variant_tags, struct_fields,
                                            struct_field_types, struct_instances, tuple_instances, array_instances,
                                            functions, function_returns, &closures, fn, diag);
    if (encoded.value == nullptr || encoded.type == ValueType::Invalid) {
      return false;
    }
    if (encoded.type != ValueType::I32) {
      diag.error("E2054", "operator '?' expects i32-encoded Result/Option value");
      return false;
    }
    if (has_await &&
        !emit_await_semantics_for_expression(expr_text, function_async_flags, async_ctx, builder, fn, diag)) {
      return false;
    }
    llvm::Value* tag = extract_enum_tag_value(encoded.value, builder);
    llvm::Value* is_err = builder.CreateICmpEQ(tag, builder.getInt32(3));
    llvm::Value* is_none = builder.CreateICmpEQ(tag, builder.getInt32(0));
    llvm::Value* should_return = builder.CreateOr(is_err, is_none);
    auto* ok_bb = llvm::BasicBlock::Create(builder.getContext(), "try.ok", fn);
    auto* err_bb = llvm::BasicBlock::Create(builder.getContext(), "try.err", fn);
    builder.CreateCondBr(should_return, err_bb, ok_bb);

    builder.SetInsertPoint(err_bb);
    for (auto it = deferred.rbegin(); it != deferred.rend(); ++it) {
      const std::string deferred_line = it->has_expression ? trim(it->expression) : trim(it->text);
      if (!emit_expression_statement(deferred_line, it->has_expression, it->expression, it->expression_ast, builder, fn, printf_fn,
                                     printf_i32_fmt, printf_f64_fmt, printf_i64_fmt, printf_str_fmt,
                                     printf_i32_raw_fmt, printf_i64_raw_fmt,
                                     printf_f64_raw_fmt, printf_raw_str_fmt, printf_newline_fmt, variables,
                                     enum_variant_tags, struct_fields, struct_field_types, struct_instances,
                                     tuple_instances, array_instances, functions, function_returns, function_async_flags,
                                     closures, it->has_await, async_ctx, diag)) {
        return false;
      }
    }
    drop_owned_variables();
    if (async_ctx != nullptr && async_ctx->enabled) {
      if (async_ctx->return_slot != nullptr) {
        builder.CreateStore(encoded.value, async_ctx->return_slot);
      }
      builder.CreateBr(async_ctx->finalize_block);
    } else {
      builder.CreateRet(encoded.value);
    }

    builder.SetInsertPoint(ok_bb);
    out_value = ExprValue{extract_enum_payload_value(encoded.value, builder), ValueType::I32};
    return true;
  };

  while (index < stmts.size() && stmts[index].indent > parent_indent) {
    const lowering::CoreStmt& st = stmts[index];
    if (st.kind == lowering::CoreStmtKind::Let) {
      std::vector<std::string> tuple_bindings;
      std::string tuple_source_expr;
      if (parse_tuple_destructure_let(st.text, tuple_bindings, tuple_source_expr)) {
        const std::string tuple_source = trim(tuple_source_expr);
        auto tuple_it = tuple_instances.find(tuple_source);
        if (tuple_it == tuple_instances.end()) {
          diag.error("E2055", "tuple destructuring source is not a tuple variable: '" + tuple_source + "'");
          return false;
        }
        if (tuple_bindings.size() != tuple_it->second.element_types.size()) {
          diag.error("E2056", "tuple destructuring arity mismatch: expected " +
                                  std::to_string(tuple_it->second.element_types.size()) + " got " +
                                  std::to_string(tuple_bindings.size()));
          return false;
        }
        for (std::size_t i = 0; i < tuple_bindings.size(); ++i) {
          const std::string& bind_name = tuple_bindings[i];
          const ValueType bind_type = tuple_it->second.element_types[i];
          llvm::Value* field_ptr =
              builder.CreateStructGEP(tuple_it->second.llvm_type, tuple_it->second.alloca, static_cast<unsigned>(i),
                                      tuple_source + ".destructure." + std::to_string(i) + ".ptr");
          llvm::Value* loaded = builder.CreateLoad(llvm_type_from_value_type(bind_type, builder), field_ptr);
          auto var_it = variables.find(bind_name);
          if (var_it == variables.end()) {
            llvm::AllocaInst* alloca = create_entry_alloca(fn, llvm_type_from_value_type(bind_type, builder), bind_name);
            variables[bind_name] = VariableSlot{alloca, bind_type};
            var_it = variables.find(bind_name);
          }
          if (var_it->second.type != bind_type) {
            diag.error("E2057", "tuple destructuring changes existing variable type for '" + bind_name + "'");
            return false;
          }
          builder.CreateStore(loaded, var_it->second.alloca);
        }
        ++index;
        continue;
      }

      const std::string name = parse_let_name(st.text);
      if (name.empty() || !st.has_expression) {
        diag.error("E2010", "invalid let statement in backend: '" + st.text + "'");
        return false;
      }
      std::vector<std::string> closure_params;
      std::string closure_body;
      bool closure_block = false;
      if (parse_closure_literal(st.expression, closure_params, closure_body, closure_block)) {
        ClosureDef closure;
        closure.params = closure_params;
        closure.body_expr = closure_body;
        closure.block_body = closure_block;
        for (const std::string& capture : collect_closure_captures(closure_params, closure_body)) {
          auto capture_it = variables.find(capture);
          if (capture_it == variables.end() || capture_it->second.alloca == nullptr) {
            continue;
          }
          llvm::Value* loaded =
              builder.CreateLoad(capture_it->second.alloca->getAllocatedType(), capture_it->second.alloca, capture + ".capt");
          if (capture_it->second.type == ValueType::I1) {
            loaded = builder.CreateZExt(loaded, builder.getInt32Ty());
          } else if (capture_it->second.type == ValueType::F32 || capture_it->second.type == ValueType::F64) {
            loaded = builder.CreateFPToSI(loaded, builder.getInt32Ty());
          }
          closure.captured_i32_values[capture] = loaded;
        }
        closures[name] = std::move(closure);
        llvm::AllocaInst* marker = create_entry_alloca(fn, builder.getInt32Ty(), name + ".closure");
        variables[name] = VariableSlot{marker, ValueType::I32};
        builder.CreateStore(builder.getInt32(0), marker);
        ++index;
        continue;
      }
      const ParsedConstructorCall ctor_call = parse_constructor_call(st.expression);
      if (!ctor_call.error.empty()) {
        diag.error("E2033", "invalid constructor call for '" + name + "': " + ctor_call.error);
        return false;
      }
      if (ctor_call.ok) {
        auto struct_ty_it = llvm_struct_types.find(ctor_call.name);
        auto fields_it = struct_fields.find(ctor_call.name);
        if (struct_ty_it != llvm_struct_types.end() && fields_it != struct_fields.end()) {
          llvm::StructType* struct_ty = struct_ty_it->second;
          llvm::AllocaInst* instance_alloca = create_entry_alloca(fn, struct_ty, name + ".struct");
          const auto& fields = fields_it->second;
          if (ctor_call.args.size() > fields.size()) {
            diag.error("E2034", "struct constructor '" + ctor_call.name + "' expects at most " +
                                    std::to_string(fields.size()) + " args but got " +
                                    std::to_string(ctor_call.args.size()));
            return false;
          }
          for (std::size_t field_index = 0; field_index < fields.size(); ++field_index) {
            const std::string& field_name = fields[field_index];
            llvm::Value* field_ptr =
                builder.CreateStructGEP(struct_ty, instance_alloca, static_cast<unsigned>(field_index),
                                        name + "." + field_name + ".ptr");
            const ValueType field_type = field_value_type_for_struct(ctor_call.name, field_name, struct_field_types);
            llvm::Value* init_value = nullptr;
            if (field_index < ctor_call.args.size()) {
              ExprValue arg_value = evaluate_expression(
                  ctor_call.args[field_index], builder, variables, enum_variant_tags, struct_fields, struct_field_types,
                  struct_instances, tuple_instances, array_instances, functions, function_returns, &closures, fn, diag);
              init_value = cast_value_to_type(arg_value, field_type, builder);
              if (init_value == nullptr) {
                diag.error("E2035", "constructor argument type mismatch for field '" + field_name + "'");
                return false;
              }
            } else if (field_type == ValueType::I1) {
              init_value = builder.getInt1(false);
            } else if (field_type == ValueType::F32) {
              init_value = llvm::ConstantFP::get(builder.getFloatTy(), 0.0);
            } else if (field_type == ValueType::F64) {
              init_value = llvm::ConstantFP::get(builder.getDoubleTy(), 0.0);
            } else {
              init_value = builder.getInt32(0);
            }
            builder.CreateStore(init_value, field_ptr);
          }
          struct_instances[name] = StructInstance{instance_alloca, struct_ty, ctor_call.name};
          llvm::AllocaInst* marker = create_entry_alloca(fn, builder.getInt32Ty(), name);
          variables[name] = VariableSlot{marker, ValueType::I32};
          builder.CreateStore(builder.getInt32(0), marker);
          ++index;
          continue;
        }
      }

      std::vector<std::string> tuple_elements;
      if (parse_tuple_literal_expr(st.expression, tuple_elements)) {
        std::vector<ExprValue> tuple_values;
        std::vector<ValueType> tuple_types;
        std::vector<llvm::Type*> llvm_types;
        tuple_values.reserve(tuple_elements.size());
        tuple_types.reserve(tuple_elements.size());
        llvm_types.reserve(tuple_elements.size());
        for (const std::string& element_expr : tuple_elements) {
          ExprValue element_value;
          if (!eval_with_try(element_expr, nullptr, false, element_value)) {
            return false;
          }
          tuple_values.push_back(element_value);
          tuple_types.push_back(element_value.type);
          llvm_types.push_back(llvm_type_from_value_type(element_value.type, builder));
        }
        llvm::StructType* tuple_ty = llvm::StructType::get(builder.getContext(), llvm_types, false);
        llvm::AllocaInst* tuple_alloca = create_entry_alloca(fn, tuple_ty, name + ".tuple");
        for (std::size_t i = 0; i < tuple_values.size(); ++i) {
          llvm::Value* field_ptr =
              builder.CreateStructGEP(tuple_ty, tuple_alloca, static_cast<unsigned>(i), name + ".tuple.ptr");
          builder.CreateStore(tuple_values[i].value, field_ptr);
        }
        tuple_instances[name] = TupleInstance{tuple_alloca, tuple_ty, tuple_types};
        llvm::AllocaInst* marker = create_entry_alloca(fn, builder.getInt32Ty(), name);
        variables[name] = VariableSlot{marker, ValueType::I32};
        builder.CreateStore(builder.getInt32(0), marker);
        ++index;
        continue;
      }

      std::vector<std::string> array_elements;
      if (parse_array_literal_expr(st.expression, array_elements)) {
        std::vector<ExprValue> array_values;
        array_values.reserve(array_elements.size());
        ValueType element_type = ValueType::I32;
        for (const std::string& element_expr : array_elements) {
          ExprValue element_value;
          if (!eval_with_try(element_expr, nullptr, false, element_value)) {
            return false;
          }
          if (array_values.empty()) {
            element_type = element_value.type;
          } else if (element_type != element_value.type) {
            if ((element_type == ValueType::F64 || element_value.type == ValueType::F64) &&
                is_numeric_type(element_type) && is_numeric_type(element_value.type)) {
              element_type = ValueType::F64;
            } else if ((element_type == ValueType::F32 || element_value.type == ValueType::F32) &&
                       is_numeric_type(element_type) && is_numeric_type(element_value.type)) {
              element_type = ValueType::F32;
            } else if (is_numeric_type(element_type) && is_numeric_type(element_value.type)) {
              element_type = ValueType::I32;
            } else {
              diag.error("E2058", "array literal elements must have compatible numeric or same types");
              return false;
            }
          }
          array_values.push_back(element_value);
        }
        llvm::Type* element_llvm_type = llvm_type_from_value_type(element_type, builder);
        llvm::ArrayType* array_ty = llvm::ArrayType::get(element_llvm_type, array_values.size());
        llvm::AllocaInst* array_alloca = create_entry_alloca(fn, array_ty, name + ".array");
        for (std::size_t i = 0; i < array_values.size(); ++i) {
          llvm::Value* casted = cast_value_to_type(array_values[i], element_type, builder);
          if (casted == nullptr) {
            diag.error("E2059", "array literal element cannot be converted to element type");
            return false;
          }
          llvm::Value* elem_ptr =
              builder.CreateInBoundsGEP(array_ty, array_alloca, {builder.getInt32(0), builder.getInt32(static_cast<int>(i))},
                                        name + ".array.elem.ptr");
          builder.CreateStore(casted, elem_ptr);
        }
        array_instances[name] = ArrayInstance{array_alloca, element_type, array_values.size()};
        llvm::AllocaInst* marker = create_entry_alloca(fn, builder.getInt32Ty(), name);
        variables[name] = VariableSlot{marker, ValueType::I32};
        builder.CreateStore(builder.getInt32(0), marker);
        ++index;
        continue;
      }

      ExprValue value;
      if (!eval_with_try(st.expression, st.expression_ast, st.has_await, value)) {
        return false;
      }
      const std::string annotation = parse_let_annotation(st.text);
      VariableSlot::Ownership ownership = VariableSlot::Ownership::None;
      if (!annotation.empty() && annotation != "Send" && annotation != "Sync") {
        ValueType declared_type = ValueType::Invalid;
        if (annotation == "i32" || annotation == "Option" || annotation == "Result" ||
            starts_with(annotation, "Option<") || starts_with(annotation, "Result<")) {
          declared_type = ValueType::I32;
        } else if (annotation == "i64") {
          declared_type = ValueType::I64;
        } else if (annotation == "f32") {
          declared_type = ValueType::F32;
        } else if (annotation == "f64") {
          declared_type = ValueType::F64;
        } else if (annotation == "bool") {
          declared_type = ValueType::I1;
        } else if (annotation == "Rc" || starts_with(annotation, "Rc<")) {
          declared_type = ValueType::I8Ptr;
          ownership = VariableSlot::Ownership::Rc;
        } else if (annotation == "Arc" || starts_with(annotation, "Arc<")) {
          declared_type = ValueType::I8Ptr;
          ownership = VariableSlot::Ownership::Arc;
        } else if (annotation == "string" || annotation == "String" || annotation == "ptr") {
          declared_type = ValueType::I8Ptr;
        }
        if (declared_type == ValueType::Invalid) {
          diag.error("E2010", "unsupported let annotation in backend: '" + annotation + "'");
          return false;
        }
        llvm::Value* casted = nullptr;
        if (ownership == VariableSlot::Ownership::Rc || ownership == VariableSlot::Ownership::Arc) {
          const std::string source_ident = parse_simple_identifier_expr(st.expression);
          if (!source_ident.empty()) {
            auto source_it = variables.find(source_ident);
            if (source_it != variables.end() && source_it->second.type == ValueType::I8Ptr &&
                source_it->second.ownership == ownership) {
              llvm::Value* src_handle = builder.CreateLoad(source_it->second.alloca->getAllocatedType(), source_it->second.alloca);
              casted = emit_ref_clone_call(ownership, src_handle, fn, builder);
            }
          }
          if (casted == nullptr && value.type == ValueType::I8Ptr) {
            casted = value.value;
          }
          if (casted == nullptr) {
            llvm::Value* as_i64 = to_i64(value, builder);
            casted = emit_ref_new_call(ownership, as_i64, fn, builder);
          }
        } else {
          casted = cast_value_to_type(value, declared_type, builder);
        }
        if (casted == nullptr) {
          diag.error("E2011", "cannot cast let value for '" + name + "' to declared type '" + annotation + "'");
          return false;
        }
        value = ExprValue{casted, declared_type};
      }
      auto it = variables.find(name);
      if (it == variables.end()) {
        llvm::Type* type = llvm_type_from_value_type(value.type, builder);
        llvm::AllocaInst* alloca = create_entry_alloca(fn, type, name);
        variables[name] = VariableSlot{alloca, value.type};
        it = variables.find(name);
      }
      if (it->second.type != value.type) {
        diag.error("E2011", "type change for variable '" + name + "' is not supported in backend");
        return false;
      }
      if (ownership == VariableSlot::Ownership::None && value.type == ValueType::I8Ptr) {
        if (starts_with(trim(st.expression), "Rc(")) {
          ownership = VariableSlot::Ownership::Rc;
        } else if (starts_with(trim(st.expression), "Arc(")) {
          ownership = VariableSlot::Ownership::Arc;
        }
      }
      if (it->second.type == ValueType::I8Ptr &&
          (it->second.ownership == VariableSlot::Ownership::Rc || it->second.ownership == VariableSlot::Ownership::Arc) &&
          it->second.alloca != nullptr) {
        llvm::Value* prev = builder.CreateLoad(it->second.alloca->getAllocatedType(), it->second.alloca);
        emit_ref_drop_call(it->second.ownership, prev, fn, builder);
      }
      builder.CreateStore(value.value, it->second.alloca);
      it->second.ownership = ownership == VariableSlot::Ownership::None ? it->second.ownership : ownership;
      ++index;
      continue;
    }

    if (st.kind == lowering::CoreStmtKind::Assign) {
      const std::string target = trim(st.target.empty() ? st.text : st.target);
      if (target.empty() || !st.has_expression) {
        diag.error("E2022", "invalid assignment statement in backend: '" + st.text + "'");
        return false;
      }
      std::string base_name;
      std::string field_name;
      if (split_dotted_name(target, base_name, field_name)) {
        auto inst_it = struct_instances.find(base_name);
        if (inst_it != struct_instances.end()) {
          const std::size_t field_index =
              field_index_for_struct(inst_it->second.struct_name, field_name, struct_fields);
          if (field_index == static_cast<std::size_t>(-1)) {
            diag.error("E2023", "unknown field '" + field_name + "' on struct '" + inst_it->second.struct_name + "'");
            return false;
          }
          ExprValue value =
              ExprValue{};
          if (!eval_with_try(st.expression, st.expression_ast, st.has_await, value)) {
            return false;
          }
          const ValueType field_type =
              field_value_type_for_struct(inst_it->second.struct_name, field_name, struct_field_types);
          llvm::Value* casted = cast_value_to_type(value, field_type, builder);
          if (casted == nullptr) {
            diag.error("E2024", "assignment type mismatch for field '" + target + "'");
            return false;
          }
          if (inst_it->second.ptr == nullptr || inst_it->second.llvm_type == nullptr) {
            diag.error("E2023", "field assignment target is not addressable: '" + target + "'");
            return false;
          }
          llvm::Value* field_ptr =
              builder.CreateStructGEP(inst_it->second.llvm_type, inst_it->second.ptr, static_cast<unsigned>(field_index),
                                      target + ".ptr");
          builder.CreateStore(casted, field_ptr);
          ++index;
          continue;
        }
      }
      auto it = variables.find(target);
      if (it == variables.end()) {
        diag.error("E2023", "assignment target is not declared: '" + target + "'");
        return false;
      }
      ExprValue value =
          ExprValue{};
      if (!eval_with_try(st.expression, st.expression_ast, st.has_await, value)) {
        return false;
      }
      llvm::Value* casted = nullptr;
      if (it->second.type == ValueType::I8Ptr &&
          (it->second.ownership == VariableSlot::Ownership::Rc || it->second.ownership == VariableSlot::Ownership::Arc)) {
        const std::string source_ident = parse_simple_identifier_expr(st.expression);
        if (!source_ident.empty()) {
          auto source_it = variables.find(source_ident);
          if (source_it != variables.end() && source_it->second.type == ValueType::I8Ptr &&
              source_it->second.ownership == it->second.ownership) {
            llvm::Value* src_handle = builder.CreateLoad(source_it->second.alloca->getAllocatedType(), source_it->second.alloca);
            casted = emit_ref_clone_call(it->second.ownership, src_handle, fn, builder);
          }
        }
        if (casted == nullptr && value.type == ValueType::I8Ptr) {
          casted = value.value;
        }
        if (casted == nullptr) {
          llvm::Value* as_i64 = to_i64(value, builder);
          casted = emit_ref_new_call(it->second.ownership, as_i64, fn, builder);
        }
      } else {
        casted = cast_value_to_type(value, it->second.type, builder);
      }
      if (casted == nullptr) {
        diag.error("E2024", "assignment type mismatch for '" + target + "'");
        return false;
      }
      if (it->second.type == ValueType::I8Ptr &&
          (it->second.ownership == VariableSlot::Ownership::Rc || it->second.ownership == VariableSlot::Ownership::Arc)) {
        llvm::Value* prev = builder.CreateLoad(it->second.alloca->getAllocatedType(), it->second.alloca);
        emit_ref_drop_call(it->second.ownership, prev, fn, builder);
      }
      builder.CreateStore(casted, it->second.alloca);
      ++index;
      continue;
    }

    if (st.kind == lowering::CoreStmtKind::Defer) {
      if (!st.has_expression) {
        diag.error("E2029", "defer statement requires expression");
        return false;
      }
      deferred.push_back(st);
      ++index;
      continue;
    }

    if (st.kind == lowering::CoreStmtKind::Break || st.kind == lowering::CoreStmtKind::Continue) {
      const std::string keyword = st.kind == lowering::CoreStmtKind::Break ? "break" : "continue";
      std::string loop_label;
      if (!parse_loop_control_statement(st.text, keyword, loop_label)) {
        diag.error("E2060", "invalid " + keyword + " statement: '" + st.text + "'");
        return false;
      }
      const LoopControlTarget* target = nullptr;
      if (loop_label.empty()) {
        if (!loop_targets.empty()) {
          target = &loop_targets.back();
        }
      } else {
        for (auto it = loop_targets.rbegin(); it != loop_targets.rend(); ++it) {
          if (it->label == loop_label) {
            target = &(*it);
            break;
          }
        }
      }
      if (target == nullptr) {
        diag.error("E2061", "no matching loop target for " + keyword +
                               (loop_label.empty() ? std::string() : " '" + loop_label + "'"));
        return false;
      }
      if (!flush_deferred()) {
        return false;
      }
      llvm::BasicBlock* jump_target =
          st.kind == lowering::CoreStmtKind::Break ? target->break_target : target->continue_target;
      if (jump_target == nullptr) {
        diag.error("E2062", "internal loop target is null for " + keyword);
        return false;
      }
      builder.CreateBr(jump_target);
      index = find_block_end(stmts, index + 1, parent_indent);
      return true;
    }

    if (st.kind == lowering::CoreStmtKind::Expr) {
      if (!emit_expression_statement(trim(st.text), st.has_expression, st.expression, st.expression_ast, builder, fn, printf_fn,
                                     printf_i32_fmt, printf_f64_fmt, printf_i64_fmt, printf_str_fmt,
                                     printf_i32_raw_fmt, printf_i64_raw_fmt,
                                     printf_f64_raw_fmt, printf_raw_str_fmt, printf_newline_fmt, variables,
                                     enum_variant_tags, struct_fields, struct_field_types, struct_instances,
                                     tuple_instances, array_instances, functions, function_returns, function_async_flags,
                                     closures, st.has_await, async_ctx, diag)) {
        return false;
      }
      ++index;
      continue;
    }

    if (st.kind == lowering::CoreStmtKind::Return) {
      llvm::Value* ret = nullptr;
      if (st.has_expression) {
        ExprValue value;
        if (!eval_with_try(st.expression, st.expression_ast, st.has_await, value)) {
          return false;
        }
        ret = cast_value_to_type(value, expected_return_type, builder);
        if (ret == nullptr && expected_return_type != ValueType::Void) {
          diag.error("E2013", "return type mismatch");
          return false;
        }
      } else if (expected_return_type != ValueType::Void) {
        if (expected_return_type == ValueType::I1) {
          ret = builder.getInt1(false);
        } else if (expected_return_type == ValueType::I64) {
          ret = builder.getInt64(0);
        } else if (expected_return_type == ValueType::F32) {
          ret = llvm::ConstantFP::get(builder.getFloatTy(), 0.0);
        } else if (expected_return_type == ValueType::F64) {
          ret = llvm::ConstantFP::get(builder.getDoubleTy(), 0.0);
        } else {
          ret = builder.getInt32(0);
        }
      }
      if (!flush_deferred()) {
        return false;
      }
      drop_owned_variables();
      if (async_ctx != nullptr && async_ctx->enabled) {
        if (expected_return_type != ValueType::Void && async_ctx->return_slot != nullptr) {
          llvm::Value* stored = ret;
          if (stored == nullptr) {
            stored = default_value_for_type(expected_return_type, builder);
          }
          builder.CreateStore(stored, async_ctx->return_slot);
        }
        builder.CreateBr(async_ctx->finalize_block);
      } else {
        if (expected_return_type == ValueType::Void) {
          builder.CreateRetVoid();
        } else {
          builder.CreateRet(ret);
        }
      }
      index = find_block_end(stmts, index + 1, parent_indent);
      return true;
    }

    if (st.kind == lowering::CoreStmtKind::If) {
      if (!st.has_expression) {
        diag.error("E2014", "if statement missing condition expression");
        return false;
      }
      const std::size_t then_start = index + 1;
      const std::size_t then_end = find_block_end(stmts, then_start, st.indent);
      const bool has_else =
          then_end < stmts.size() && stmts[then_end].kind == lowering::CoreStmtKind::Else && stmts[then_end].indent == st.indent;

      ExprValue cond_value =
          evaluate_expression(st.expression, builder, variables, enum_variant_tags, struct_fields, struct_field_types,
                              struct_instances, tuple_instances, array_instances, functions, function_returns, &closures,
                              fn, diag);
      if (st.has_await &&
          !emit_await_semantics_for_expression(st.expression, function_async_flags, async_ctx, builder, fn, diag)) {
        return false;
      }
      llvm::Value* cond = to_i1(cond_value, builder);
      if (cond == nullptr) {
        diag.error("E2015", "if condition must be bool/i32-compatible");
        return false;
      }

      auto* then_bb = llvm::BasicBlock::Create(builder.getContext(), "if.then", fn);
      auto* else_bb = has_else ? llvm::BasicBlock::Create(builder.getContext(), "if.else", fn) : nullptr;
      auto* merge_bb = llvm::BasicBlock::Create(builder.getContext(), "if.end", fn);
      builder.CreateCondBr(cond, then_bb, has_else ? else_bb : merge_bb);

      builder.SetInsertPoint(then_bb);
      index = then_start;
      if (!emit_block(stmts, enum_variant_tags, struct_fields, struct_field_types, llvm_struct_types, functions,
                      function_returns, function_async_flags,
                      expected_return_type, index, st.indent, fn, module, builder, variables, struct_instances,
                      tuple_instances, array_instances, loop_targets, closures, async_ctx, diag)) {
        return false;
      }
      if (!builder.GetInsertBlock()->getTerminator()) {
        builder.CreateBr(merge_bb);
      }

      if (has_else) {
        builder.SetInsertPoint(else_bb);
        index = then_end + 1;
        if (!emit_block(stmts, enum_variant_tags, struct_fields, struct_field_types, llvm_struct_types, functions,
                        function_returns, function_async_flags,
                        expected_return_type, index, st.indent, fn, module, builder, variables, struct_instances,
                        tuple_instances, array_instances, loop_targets, closures, async_ctx, diag)) {
          return false;
        }
        if (!builder.GetInsertBlock()->getTerminator()) {
          builder.CreateBr(merge_bb);
        }
      } else {
        index = then_end;
      }

      builder.SetInsertPoint(merge_bb);
      continue;
    }

    if (st.kind == lowering::CoreStmtKind::While) {
      if (!st.has_expression) {
        diag.error("E2016", "while statement missing condition expression");
        return false;
      }
      std::string while_label;
      std::string normalized_while;
      if (!parse_loop_header_label(st.text, "while", while_label, normalized_while)) {
        diag.error("E2063", "invalid while header: '" + st.text + "'");
        return false;
      }
      auto* cond_bb = llvm::BasicBlock::Create(builder.getContext(), "while.cond", fn);
      auto* body_bb = llvm::BasicBlock::Create(builder.getContext(), "while.body", fn);
      auto* after_bb = llvm::BasicBlock::Create(builder.getContext(), "while.end", fn);
      builder.CreateBr(cond_bb);

      builder.SetInsertPoint(cond_bb);
      ExprValue cond_value =
          evaluate_expression(st.expression, builder, variables, enum_variant_tags, struct_fields, struct_field_types,
                              struct_instances, tuple_instances, array_instances, functions, function_returns, &closures,
                              fn, diag);
      if (st.has_await &&
          !emit_await_semantics_for_expression(st.expression, function_async_flags, async_ctx, builder, fn, diag)) {
        return false;
      }
      llvm::Value* cond = to_i1(cond_value, builder);
      if (cond == nullptr) {
        diag.error("E2017", "while condition must be bool/i32-compatible");
        return false;
      }
      builder.CreateCondBr(cond, body_bb, after_bb);

      builder.SetInsertPoint(body_bb);
      loop_targets.push_back(LoopControlTarget{while_label, cond_bb, after_bb});
      ++index;
      if (!emit_block(stmts, enum_variant_tags, struct_fields, struct_field_types, llvm_struct_types, functions,
                      function_returns, function_async_flags,
                      expected_return_type, index, st.indent, fn, module, builder, variables, struct_instances,
                      tuple_instances, array_instances, loop_targets, closures, async_ctx, diag)) {
        loop_targets.pop_back();
        return false;
      }
      loop_targets.pop_back();
      if (!builder.GetInsertBlock()->getTerminator()) {
        builder.CreateBr(cond_bb);
      }
      builder.SetInsertPoint(after_bb);
      continue;
    }

    if (st.kind == lowering::CoreStmtKind::Else) {
      if (!flush_deferred()) {
        return false;
      }
      return true;
    }

    if (st.kind == lowering::CoreStmtKind::For) {
      const ForHeader header = parse_for_header(st);
      if (!header.ok) {
        diag.error("E2018", "invalid for header in backend: '" + st.text + "'");
        return false;
      }
      ExprValue start_value =
          evaluate_expression(header.start_expr, builder, variables, enum_variant_tags, struct_fields, struct_field_types,
                              struct_instances, tuple_instances, array_instances, functions, function_returns, &closures,
                              fn, diag);
      ExprValue end_value =
          evaluate_expression(header.end_expr, builder, variables, enum_variant_tags, struct_fields, struct_field_types,
                              struct_instances, tuple_instances, array_instances, functions, function_returns, &closures,
                              fn, diag);
      llvm::Value* start_i32 = to_i32(start_value, builder);
      llvm::Value* end_i32 = to_i32(end_value, builder);
      if (start_i32 == nullptr || end_i32 == nullptr) {
        diag.error("E2018", "for range bounds must be i32-compatible");
        return false;
      }

      auto previous = variables.find(header.loop_var);
      const bool had_previous = previous != variables.end();
      VariableSlot previous_slot;
      if (had_previous) {
        previous_slot = previous->second;
      }

      llvm::AllocaInst* loop_alloca = create_entry_alloca(fn, builder.getInt32Ty(), header.loop_var);
      builder.CreateStore(start_i32, loop_alloca);
      variables[header.loop_var] = VariableSlot{loop_alloca, ValueType::I32};

      auto* cond_bb = llvm::BasicBlock::Create(builder.getContext(), "for.cond", fn);
      auto* body_bb = llvm::BasicBlock::Create(builder.getContext(), "for.body", fn);
      auto* step_bb = llvm::BasicBlock::Create(builder.getContext(), "for.step", fn);
      auto* after_bb = llvm::BasicBlock::Create(builder.getContext(), "for.end", fn);
      builder.CreateBr(cond_bb);

      builder.SetInsertPoint(cond_bb);
      llvm::Value* cur_i = builder.CreateLoad(builder.getInt32Ty(), loop_alloca);
      llvm::Value* cond = builder.CreateICmpSLT(cur_i, end_i32);
      builder.CreateCondBr(cond, body_bb, after_bb);

      builder.SetInsertPoint(body_bb);
      loop_targets.push_back(LoopControlTarget{header.label, step_bb, after_bb});
      ++index;
      if (!emit_block(stmts, enum_variant_tags, struct_fields, struct_field_types, llvm_struct_types, functions,
                      function_returns, function_async_flags,
                      expected_return_type, index, st.indent, fn, module, builder, variables, struct_instances,
                      tuple_instances, array_instances, loop_targets, closures, async_ctx, diag)) {
        loop_targets.pop_back();
        return false;
      }
      loop_targets.pop_back();
      if (!builder.GetInsertBlock()->getTerminator()) {
        builder.CreateBr(step_bb);
      }

      builder.SetInsertPoint(step_bb);
      llvm::Value* loaded_i = builder.CreateLoad(builder.getInt32Ty(), loop_alloca);
      llvm::Value* next_i = builder.CreateAdd(loaded_i, builder.getInt32(1));
      builder.CreateStore(next_i, loop_alloca);
      builder.CreateBr(cond_bb);

      builder.SetInsertPoint(after_bb);
      if (had_previous) {
        variables[header.loop_var] = previous_slot;
      } else {
        variables.erase(header.loop_var);
      }
      continue;
    }

    if (st.kind == lowering::CoreStmtKind::Match) {
      if (!st.has_expression) {
        diag.error("E2018", "match statement missing expression");
        return false;
      }
      ExprValue match_expr =
          evaluate_expression(st.expression, builder, variables, enum_variant_tags, struct_fields, struct_field_types,
                              struct_instances, tuple_instances, array_instances, functions, function_returns, &closures,
                              fn, diag);
      if (st.has_await &&
          !emit_await_semantics_for_expression(st.expression, function_async_flags, async_ctx, builder, fn, diag)) {
        return false;
      }
      llvm::Value* match_i32 = to_i32(match_expr, builder);
      if (match_i32 == nullptr) {
        diag.error("E2018", "match expression must be i32/bool-compatible");
        return false;
      }

      const std::size_t match_body_start = index + 1;
      const std::size_t match_end = find_block_end(stmts, match_body_start, st.indent);
      if (match_body_start >= stmts.size() || match_body_start >= match_end) {
        diag.error("E2018", "match statement has no arms");
        return false;
      }

      auto* after_bb = llvm::BasicBlock::Create(builder.getContext(), "match.end", fn);
      llvm::BasicBlock* current_cmp_bb = builder.GetInsertBlock();
      std::size_t arm_index = match_body_start;
      bool has_any_arm = false;
      while (arm_index < match_end) {
        const lowering::CoreStmt& arm_label_stmt = stmts[arm_index];
        if (arm_label_stmt.indent <= st.indent) {
          break;
        }
        const MatchArmLabel label = parse_match_arm_label(arm_label_stmt.text, enum_variant_tags);
        if (!label.valid) {
          diag.error("E2018", "invalid match arm label: '" + arm_label_stmt.text + "'");
          return false;
        }
        has_any_arm = true;
        const std::size_t arm_body_start = arm_index + 1;
        const std::size_t arm_body_end = find_block_end(stmts, arm_body_start, arm_label_stmt.indent);

        auto* arm_bb = llvm::BasicBlock::Create(builder.getContext(), "match.arm", fn);
        llvm::BasicBlock* next_cmp_bb = nullptr;
        if (!label.wildcard) {
          next_cmp_bb = llvm::BasicBlock::Create(builder.getContext(), "match.next", fn);
        }

        builder.SetInsertPoint(current_cmp_bb);
        if (label.wildcard) {
          builder.CreateBr(arm_bb);
        } else {
          llvm::Value* lhs = match_i32;
          if (label.enum_variant) {
            lhs = extract_enum_tag_value(match_i32, builder);
          }
          llvm::Value* cmp = builder.CreateICmpEQ(lhs, builder.getInt32(label.value));
          builder.CreateCondBr(cmp, arm_bb, next_cmp_bb);
        }

        builder.SetInsertPoint(arm_bb);
        bool bound_payload = false;
        VariableSlot previous_payload_slot;
        if (label.enum_variant && !label.payload_binding.empty()) {
          auto payload_prev = variables.find(label.payload_binding);
          if (payload_prev != variables.end()) {
            previous_payload_slot = payload_prev->second;
          }
          llvm::AllocaInst* payload_alloca = create_entry_alloca(fn, builder.getInt32Ty(), label.payload_binding);
          builder.CreateStore(extract_enum_payload_value(match_i32, builder), payload_alloca);
          variables[label.payload_binding] = VariableSlot{payload_alloca, ValueType::I32};
          bound_payload = true;
        }
        std::size_t nested_index = arm_body_start;
        if (!emit_block(stmts, enum_variant_tags, struct_fields, struct_field_types, llvm_struct_types, functions,
                        function_returns, function_async_flags,
                        expected_return_type, nested_index, arm_label_stmt.indent, fn, module, builder, variables,
                        struct_instances, tuple_instances, array_instances, loop_targets, closures, async_ctx, diag)) {
          return false;
        }
        if (bound_payload) {
          if (previous_payload_slot.alloca != nullptr) {
            variables[label.payload_binding] = previous_payload_slot;
          } else {
            variables.erase(label.payload_binding);
          }
        }
        if (!builder.GetInsertBlock()->getTerminator()) {
          builder.CreateBr(after_bb);
        }

        arm_index = arm_body_end;
        if (label.wildcard) {
          current_cmp_bb = nullptr;
          break;
        }
        current_cmp_bb = next_cmp_bb;
      }

      if (!has_any_arm) {
        diag.error("E2018", "match statement has no valid arms");
        return false;
      }
      if (current_cmp_bb != nullptr) {
        builder.SetInsertPoint(current_cmp_bb);
        builder.CreateBr(after_bb);
      }
      builder.SetInsertPoint(after_bb);
      index = match_end;
      continue;
    }

    ++index;
  }
  if (!flush_deferred()) {
    return false;
  }
  return true;
}


}  // namespace thagc::codegen
