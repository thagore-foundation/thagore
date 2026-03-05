#pragma once

#include "thagc/backend/llvm_emitter.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <llvm/Config/llvm-config.h>
#include <llvm/ADT/Optional.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#if __has_include(<llvm/TargetParser/Host.h>)
#include <llvm/TargetParser/Host.h>
#endif
#if __has_include(<llvm/TargetParser/Triple.h>)
#include <llvm/TargetParser/Triple.h>
#else
#include <llvm/ADT/Triple.h>
#endif
#include <llvm/Transforms/Coroutines/CoroCleanup.h>
#include <llvm/Transforms/Coroutines/CoroEarly.h>
#include <llvm/Transforms/Coroutines/CoroElide.h>
#include <llvm/Transforms/Coroutines/CoroSplit.h>

namespace thagc::codegen {

enum class ExprTokKind {
  Atom,
  Op,
  LParen,
  RParen,
  Comma,
  End,
};

struct ExprTok {
  ExprTokKind kind = ExprTokKind::End;
  std::string text;
};

enum class ValueType {
  I32,
  I64,
  F32,
  F64,
  I1,
  I8Ptr,
  Void,
  Invalid,
};

struct ExprValue {
  llvm::Value* value = nullptr;
  ValueType type = ValueType::Invalid;
};

struct VariableSlot {
  llvm::AllocaInst* alloca = nullptr;
  ValueType type = ValueType::Invalid;
  enum class Ownership {
    None,
    Rc,
    Arc,
  } ownership = Ownership::None;
};

struct StructInstance {
  llvm::Value* ptr = nullptr;
  llvm::StructType* llvm_type = nullptr;
  std::string struct_name;
};

struct ClosureDef {
  std::vector<std::string> params;
  std::string body_expr;
  bool block_body = false;
  std::unordered_map<std::string, llvm::Value*> captured_i32_values;
};

struct TupleInstance {
  llvm::AllocaInst* alloca = nullptr;
  llvm::StructType* llvm_type = nullptr;
  std::vector<ValueType> element_types;
};

struct ArrayInstance {
  llvm::AllocaInst* alloca = nullptr;
  ValueType element_type = ValueType::Invalid;
  std::size_t length = 0;
};

struct AsyncLoweringContext {
  bool enabled = false;
  ValueType expected_return_type = ValueType::Invalid;
  llvm::Value* coro_id = nullptr;
  llvm::Value* coro_handle = nullptr;
  llvm::AllocaInst* return_slot = nullptr;
  llvm::BasicBlock* finalize_block = nullptr;
  llvm::FunctionCallee coro_save;
  llvm::FunctionCallee coro_suspend;
  llvm::FunctionCallee coro_end;
  llvm::FunctionCallee coro_free;
  llvm::FunctionCallee free_fn;
};

inline constexpr int kEnumPayloadShift = 20;
inline constexpr int kEnumPayloadMask = (1 << kEnumPayloadShift) - 1;

inline llvm::Type* pointer_type(llvm::IRBuilder<>& builder) {
#if LLVM_VERSION_MAJOR >= 15
  return builder.getPtrTy();
#else
  return builder.getInt8PtrTy();
#endif
}

struct ExprCursor {
  std::vector<ExprTok> tokens;
  std::size_t index = 0;
  std::string error;
  llvm::IRBuilder<>* builder = nullptr;
  std::unordered_map<std::string, VariableSlot>* variables = nullptr;
  const std::unordered_map<std::string, int>* enum_variant_tags = nullptr;
  const std::unordered_map<std::string, llvm::Function*>* functions = nullptr;
  const std::unordered_map<std::string, ValueType>* function_returns = nullptr;
  const std::unordered_map<std::string, std::vector<std::string>>* struct_fields = nullptr;
  const std::unordered_map<std::string, std::string>* struct_field_types = nullptr;
  const std::unordered_map<std::string, StructInstance>* struct_instances = nullptr;
  std::unordered_map<std::string, TupleInstance>* tuple_instances = nullptr;
  std::unordered_map<std::string, ArrayInstance>* array_instances = nullptr;
  std::unordered_map<std::string, ClosureDef>* closures = nullptr;
  llvm::Function* current_function = nullptr;
};

struct ParsedConstructorCall {
  bool ok = false;
  std::string name;
  std::vector<std::string> args;
  std::string error;
};

struct ForHeader {
  bool ok = false;
  std::string label;
  std::string loop_var;
  std::string start_expr;
  std::string end_expr;
};

struct LoopControlTarget {
  std::string label;
  llvm::BasicBlock* continue_target = nullptr;
  llvm::BasicBlock* break_target = nullptr;
};

struct MatchArmLabel {
  bool valid = false;
  bool wildcard = false;
  int value = 0;
  bool enum_variant = false;
  std::string payload_binding;
};

struct InterpChunk {
  bool placeholder = false;
  std::string text;
};

std::string trim(const std::string& text);
std::string unescape_string_body(const std::string& body);
bool starts_with(const std::string& text, std::string_view prefix);
bool ends_with(const std::string& text, std::string_view suffix);
bool is_ident_start(char ch);
bool is_ident_body(char ch);
bool is_integer_atom(const std::string& text);
bool is_float_atom(const std::string& text);
bool is_string_atom(const std::string& text);
std::vector<ExprTok> tokenize_expression(const std::string& text, std::string& error);

std::string parse_let_name(const std::string& line);
std::string parse_let_annotation(const std::string& line);
bool split_dotted_name(const std::string& text, std::string& base, std::string& member);
ParsedConstructorCall parse_constructor_call(const std::string& expr);
bool split_top_level_items(const std::string& text, std::vector<std::string>& out);
std::string parse_simple_identifier_expr(const std::string& expr);
bool parse_tuple_literal_expr(const std::string& expr, std::vector<std::string>& elements);
bool parse_array_literal_expr(const std::string& expr, std::vector<std::string>& elements);
bool parse_tuple_destructure_let(const std::string& line, std::vector<std::string>& names, std::string& expr);

llvm::AllocaInst* create_entry_alloca(llvm::Function* fn, llvm::Type* type, const std::string& name);
llvm::Value* create_global_cstr_ptr(llvm::IRBuilder<>& builder, const std::string& value,
                                    const std::string& symbol);
std::size_t field_index_for_struct(const std::string& struct_name, const std::string& field_name,
                                   const std::unordered_map<std::string, std::vector<std::string>>& struct_fields);

bool is_interpolated_literal(const std::string& text);
bool parse_closure_literal(const std::string& text, std::vector<std::string>& params, std::string& body_expr,
                           bool& block_body);
std::vector<std::string> collect_closure_captures(const std::vector<std::string>& params, const std::string& body);

bool parse_try_operator_expr(const std::string& text, std::string& inner);
bool parse_tuple_field_access(const std::string& text, std::string& base, std::size_t& index);
bool parse_array_index_access(const std::string& text, std::string& base, std::string& index_expr);
bool parse_len_call(const std::string& text, std::string& base);
std::optional<int> builtin_variant_tag(const std::string& name);
int encode_enum_with_payload(int tag, int payload);
llvm::Value* extract_enum_tag_value(llvm::Value* encoded, llvm::IRBuilder<>& builder);
llvm::Value* extract_enum_payload_value(llvm::Value* encoded, llvm::IRBuilder<>& builder);

bool is_numeric_type(ValueType type);
bool is_float_type(ValueType type);
bool is_integer_numeric_type(ValueType type);
ValueType promoted_integer_type(ValueType lhs, ValueType rhs);
llvm::Value* to_integer_numeric_value(ExprValue value, ValueType target, llvm::IRBuilder<>& builder);
ValueType promoted_float_type(ValueType lhs, ValueType rhs);
llvm::Value* to_float_value(ExprValue value, ValueType target, llvm::IRBuilder<>& builder);
llvm::Value* to_f64_value(ExprValue value, llvm::IRBuilder<>& builder);
llvm::Value* to_i32(ExprValue value, llvm::IRBuilder<>& builder);
llvm::Value* to_i64(ExprValue value, llvm::IRBuilder<>& builder);
llvm::Value* to_i1(ExprValue value, llvm::IRBuilder<>& builder);

ValueType value_type_from_return_type(const std::string& type_name);
ValueType value_type_from_field_annotation(const std::string& type_name);
ValueType field_value_type_for_struct(const std::string& struct_name, const std::string& field_name,
                                      const std::unordered_map<std::string, std::string>& struct_field_types);
llvm::Type* llvm_type_from_value_type(ValueType type, llvm::IRBuilder<>& builder);
llvm::Value* cast_value_to_type(ExprValue value, ValueType target, llvm::IRBuilder<>& builder);
llvm::Value* default_value_for_type(ValueType type, llvm::IRBuilder<>& builder);

ExprValue evaluate_expression(const std::string& expr_text, llvm::IRBuilder<>& builder,
                              std::unordered_map<std::string, VariableSlot>& variables,
                              const std::unordered_map<std::string, int>& enum_variant_tags,
                              const std::unordered_map<std::string, std::vector<std::string>>& struct_fields,
                              const std::unordered_map<std::string, std::string>& struct_field_types,
                              const std::unordered_map<std::string, StructInstance>& struct_instances,
                              std::unordered_map<std::string, TupleInstance>& tuple_instances,
                              std::unordered_map<std::string, ArrayInstance>& array_instances,
                              const std::unordered_map<std::string, llvm::Function*>& functions,
                              const std::unordered_map<std::string, ValueType>& function_returns,
                              std::unordered_map<std::string, ClosureDef>* closures,
                              llvm::Function* current_function,
                              support::DiagnosticSink& diag);
ExprValue evaluate_expression(const syntax::AstExprPtr& expr_ast, const std::string& fallback_expr,
                              llvm::IRBuilder<>& builder, std::unordered_map<std::string, VariableSlot>& variables,
                              const std::unordered_map<std::string, int>& enum_variant_tags,
                              const std::unordered_map<std::string, std::vector<std::string>>& struct_fields,
                              const std::unordered_map<std::string, std::string>& struct_field_types,
                              const std::unordered_map<std::string, StructInstance>& struct_instances,
                              std::unordered_map<std::string, TupleInstance>& tuple_instances,
                              std::unordered_map<std::string, ArrayInstance>& array_instances,
                              const std::unordered_map<std::string, llvm::Function*>& functions,
                              const std::unordered_map<std::string, ValueType>& function_returns,
                              std::unordered_map<std::string, ClosureDef>* closures,
                              llvm::Function* current_function,
                              support::DiagnosticSink& diag);

bool emit_expression_statement(const std::string& line, bool has_expression, const std::string& expression,
                               const syntax::AstExprPtr& expression_ast,
                               llvm::IRBuilder<>& builder, llvm::Function* fn, llvm::FunctionCallee printf_fn,
                               llvm::Value* printf_i32_fmt, llvm::Value* printf_f64_fmt,
                               llvm::Value* printf_i64_fmt, llvm::Value* printf_str_fmt,
                               llvm::Value* printf_i32_raw_fmt, llvm::Value* printf_i64_raw_fmt,
                               llvm::Value* printf_f64_raw_fmt, llvm::Value* printf_raw_str_fmt,
                               llvm::Value* printf_newline_fmt,
                               std::unordered_map<std::string, VariableSlot>& variables,
                               const std::unordered_map<std::string, int>& enum_variant_tags,
                               const std::unordered_map<std::string, std::vector<std::string>>& struct_fields,
                               const std::unordered_map<std::string, std::string>& struct_field_types,
                               const std::unordered_map<std::string, StructInstance>& struct_instances,
                               std::unordered_map<std::string, TupleInstance>& tuple_instances,
                               std::unordered_map<std::string, ArrayInstance>& array_instances,
                               const std::unordered_map<std::string, llvm::Function*>& functions,
                               const std::unordered_map<std::string, ValueType>& function_returns,
                               const std::unordered_map<std::string, bool>& function_async_flags,
                               std::unordered_map<std::string, ClosureDef>& closures,
                               bool await_expression, AsyncLoweringContext* async_ctx,
                               support::DiagnosticSink& diag);

bool emit_await_semantics_for_expression(const std::string& expression,
                                         const std::unordered_map<std::string, bool>& function_async_flags,
                                         AsyncLoweringContext* async_ctx, llvm::IRBuilder<>& builder,
                                         llvm::Function* fn, support::DiagnosticSink& diag);

llvm::Value* emit_ref_new_call(VariableSlot::Ownership ownership, llvm::Value* value_i64, llvm::Function* fn,
                               llvm::IRBuilder<>& builder);
llvm::Value* emit_ref_clone_call(VariableSlot::Ownership ownership, llvm::Value* handle, llvm::Function* fn,
                                 llvm::IRBuilder<>& builder);
void emit_ref_drop_call(VariableSlot::Ownership ownership, llvm::Value* handle, llvm::Function* fn,
                        llvm::IRBuilder<>& builder);

void emit_await_spawn_wrapper(llvm::IRBuilder<>& builder, llvm::Function* fn, llvm::Value* coro_handle);
bool emit_async_suspend_point(AsyncLoweringContext* async_ctx, llvm::IRBuilder<>& builder, llvm::Function* fn,
                              support::DiagnosticSink& diag);
bool function_contains_await(const lowering::CoreFunction& fn_def);

bool parse_loop_header_label(const std::string& line, const std::string& keyword, std::string& label,
                             std::string& normalized);
bool parse_loop_control_statement(const std::string& line, const std::string& keyword, std::string& label);
ForHeader parse_for_header(const lowering::CoreStmt& st);
MatchArmLabel parse_match_arm_label(const std::string& line,
                                    const std::unordered_map<std::string, int>& enum_variant_tags);

bool emit_block(const std::vector<lowering::CoreStmt>& stmts,
                const std::unordered_map<std::string, int>& enum_variant_tags,
                const std::unordered_map<std::string, std::vector<std::string>>& struct_fields,
                const std::unordered_map<std::string, std::string>& struct_field_types,
                const std::unordered_map<std::string, llvm::StructType*>& llvm_struct_types,
                const std::unordered_map<std::string, llvm::Function*>& functions,
                const std::unordered_map<std::string, ValueType>& function_returns,
                const std::unordered_map<std::string, bool>& function_async_flags,
                ValueType expected_return_type,
                std::size_t& index,
                int base_indent,
                llvm::Function* fn,
                llvm::Module* module,
                llvm::IRBuilder<>& builder,
                std::unordered_map<std::string, VariableSlot>& variables,
                std::unordered_map<std::string, StructInstance>& struct_instances,
                std::unordered_map<std::string, TupleInstance>& tuple_instances,
                std::unordered_map<std::string, ArrayInstance>& array_instances,
                std::vector<LoopControlTarget>& loop_targets,
                std::unordered_map<std::string, ClosureDef>& closures,
                AsyncLoweringContext* async_ctx,
                support::DiagnosticSink& diag);

std::unique_ptr<llvm::Module> build_module(llvm::LLVMContext& context, const std::string& module_name,
                                           const lowering::CoreProgram& core,
                                           support::DiagnosticSink& diag);

}  // namespace thagc::codegen
