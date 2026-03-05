#include "thagc/hir/typecheck.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace thagc::hir {

namespace {

using semantics::TypeKind;

static bool starts_with(const std::string& text, std::string_view prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

static bool is_ident_start(char ch) {
  return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}

static bool is_ident_body(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

static bool is_integer_atom(const std::string& atom) {
  if (atom.empty()) {
    return false;
  }
  std::size_t i = 0;
  if (atom[i] == '-') {
    ++i;
  }
  if (i >= atom.size()) {
    return false;
  }
  for (; i < atom.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(atom[i]))) {
      return false;
    }
  }
  return true;
}

static bool is_float_atom(const std::string& atom) {
  if (atom.empty()) {
    return false;
  }
  std::size_t i = 0;
  if (atom[i] == '-') {
    ++i;
  }
  bool seen_dot = false;
  bool seen_digit = false;
  for (; i < atom.size(); ++i) {
    const char ch = atom[i];
    if (std::isdigit(static_cast<unsigned char>(ch))) {
      seen_digit = true;
      continue;
    }
    if (ch == '.' && !seen_dot) {
      seen_dot = true;
      continue;
    }
    return false;
  }
  return seen_dot && seen_digit;
}

static bool is_string_atom(const std::string& atom) {
  return atom.size() >= 2 && atom.front() == '"' && atom.back() == '"';
}

static bool is_interpolated_string_atom(const std::string& atom) {
  return atom.size() >= 3 && atom[0] == 'v' && atom[1] == '"' && atom.back() == '"';
}

static bool split_dotted_name(const std::string& text, std::string& base, std::string& member) {
  const std::size_t dot = text.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= text.size()) {
    return false;
  }
  base = text.substr(0, dot);
  member = text.substr(dot + 1);
  return true;
}

static bool is_numeric_type(TypeKind type) {
  return type == TypeKind::I32 || type == TypeKind::I64 || type == TypeKind::F32 || type == TypeKind::F64;
}

static TypeKind combine_numeric(TypeKind lhs, TypeKind rhs) {
  if (lhs == TypeKind::F64 || rhs == TypeKind::F64) {
    return TypeKind::F64;
  }
  if (lhs == TypeKind::F32 || rhs == TypeKind::F32) {
    return TypeKind::F32;
  }
  if (lhs == TypeKind::I64 || rhs == TypeKind::I64) {
    return TypeKind::I64;
  }
  if (lhs == TypeKind::I32 && rhs == TypeKind::I32) {
    return TypeKind::I32;
  }
  return TypeKind::Unknown;
}

static bool is_assignable(TypeKind expected, TypeKind actual) {
  if (expected == actual) {
    return true;
  }
  if (expected == TypeKind::I64 && actual == TypeKind::I32) {
    return true;
  }
  if (expected == TypeKind::F32 && (actual == TypeKind::I32 || actual == TypeKind::I64)) {
    return true;
  }
  if (expected == TypeKind::F64 && (actual == TypeKind::F32 || actual == TypeKind::I32 || actual == TypeKind::I64)) {
    return true;
  }
  return false;
}

static TypeKind parse_declared_type_expr(const std::string& type_name, const TypeEnv& env) {
  if (type_name == "i32") {
    return TypeKind::I32;
  }
  if (type_name == "i64") {
    return TypeKind::I64;
  }
  if (type_name == "f32") {
    return TypeKind::F32;
  }
  if (type_name == "f64") {
    return TypeKind::F64;
  }
  if (type_name == "bool") {
    return TypeKind::Bool;
  }
  if (type_name == "ptr") {
    return TypeKind::Ptr;
  }
  if (type_name == "string" || type_name == "String") {
    return TypeKind::String;
  }
  if (type_name == "Rc" || starts_with(type_name, "Rc<")) {
    return TypeKind::Rc;
  }
  if (type_name == "Arc" || starts_with(type_name, "Arc<")) {
    return TypeKind::Arc;
  }
  if (type_name == "Option" || starts_with(type_name, "Option<")) {
    return TypeKind::Option;
  }
  if (type_name == "Result" || starts_with(type_name, "Result<")) {
    return TypeKind::Result;
  }
  if (type_name == "List" || starts_with(type_name, "List<")) {
    return TypeKind::List;
  }
  if (env.struct_names != nullptr && env.struct_names->find(type_name) != env.struct_names->end()) {
    return TypeKind::StructType;
  }
  return TypeKind::Unknown;
}

static HirExprPtr make_hir_expr(HirExprNode node, const std::optional<syntax::Span>& span) {
  auto out = std::make_shared<HirExpr>();
  out->node = std::move(node);
  out->span = span;
  return out;
}

static TypeKind infer_expression_impl(const HirExprPtr& expr, const TypeEnv& env, std::string& error);

static TypeKind infer_call(const HirCall& call, const TypeEnv& env, std::string& error) {
  std::vector<TypeKind> args;
  args.reserve(call.args.size());
  for (const auto& arg : call.args) {
    std::string arg_error;
    const TypeKind arg_type = infer_expression_impl(arg, env, arg_error);
    if (arg_type == TypeKind::Unknown) {
      error = arg_error.empty() ? "cannot infer call argument type" : arg_error;
      return TypeKind::Unknown;
    }
    args.push_back(arg_type);
  }

  if (!call.callee) {
    error = "non-callable expression";
    return TypeKind::Unknown;
  }

  if (std::holds_alternative<HirIdent>(call.callee->node)) {
    const std::string& name = std::get<HirIdent>(call.callee->node).name;
    if (name == "Some" || name == "None") {
      return TypeKind::Option;
    }
    if (name == "Ok" || name == "Err") {
      return TypeKind::Result;
    }
    if (name == "is_some" || name == "is_none" || name == "is_ok" || name == "is_err") {
      if (args.size() != 1) {
        error = "predicate '" + name + "' expects 1 argument";
        return TypeKind::Unknown;
      }
      return TypeKind::Bool;
    }
    if (name == "unwrap") {
      if (args.size() != 1) {
        error = "unwrap() expects 1 argument";
        return TypeKind::Unknown;
      }
      return TypeKind::I32;
    }
    if (name == "unwrap_or") {
      if (args.size() != 2) {
        error = "unwrap_or() expects 2 arguments";
        return TypeKind::Unknown;
      }
      return args[1];
    }
    if (name == "open" || name == "close" || name == "read" || name == "write" || name == "spawn") {
      return TypeKind::I32;
    }
    if (name == "Rc") {
      if (args.size() != 1) {
        error = "Rc() expects exactly 1 argument";
        return TypeKind::Unknown;
      }
      return TypeKind::Rc;
    }
    if (name == "Arc") {
      if (args.size() != 1) {
        error = "Arc() expects exactly 1 argument";
        return TypeKind::Unknown;
      }
      return TypeKind::Arc;
    }
    if (name == "len") {
      if (args.size() != 1) {
        error = "len() expects 1 argument";
        return TypeKind::Unknown;
      }
      return TypeKind::I32;
    }
    if (name == "print") {
      return TypeKind::I32;
    }
    if (env.enum_variants != nullptr && env.enum_variants->find(name) != env.enum_variants->end()) {
      if (args.size() > 1) {
        error = "enum payload constructor '" + name + "' expects at most 1 argument";
        return TypeKind::Unknown;
      }
      return TypeKind::EnumType;
    }
    if (env.function_returns != nullptr) {
      auto ret = env.function_returns->find(name);
      if (ret != env.function_returns->end()) {
        if (env.function_arity != nullptr) {
          auto arity = env.function_arity->find(name);
          if (arity != env.function_arity->end() && args.size() != arity->second) {
            error = "function '" + name + "' expects " + std::to_string(arity->second) +
                    " arguments but got " + std::to_string(args.size());
            return TypeKind::Unknown;
          }
        }
        return ret->second;
      }
    }
    if (env.scope != nullptr) {
      auto local_callable = env.scope->find(name);
      if (local_callable != env.scope->end() && local_callable->second == TypeKind::FunctionType) {
        return TypeKind::I32;
      }
    }
    if (env.struct_names != nullptr && env.struct_names->find(name) != env.struct_names->end()) {
      return TypeKind::StructType;
    }
    error = "unknown callable '" + name + "'";
    return TypeKind::Unknown;
  }

  if (std::holds_alternative<HirField>(call.callee->node)) {
    const HirField& callee = std::get<HirField>(call.callee->node);
    if (callee.base && std::holds_alternative<HirIdent>(callee.base->node) && env.struct_bindings != nullptr &&
        env.struct_methods != nullptr && env.function_returns != nullptr) {
      const std::string& receiver = std::get<HirIdent>(callee.base->node).name;
      auto binding = env.struct_bindings->find(receiver);
      if (binding == env.struct_bindings->end()) {
        error = "unknown method '" + callee.field + "'";
        return TypeKind::Unknown;
      }
      const std::string& struct_name = binding->second;
      auto methods = env.struct_methods->find(struct_name);
      if (methods == env.struct_methods->end() ||
          std::find(methods->second.begin(), methods->second.end(), callee.field) == methods->second.end()) {
        error = "unknown method '" + callee.field + "' on struct '" + struct_name + "'";
        return TypeKind::Unknown;
      }
      const std::string method_symbol = struct_name + "." + callee.field;
      auto ret = env.function_returns->find(method_symbol);
      if (ret == env.function_returns->end()) {
        error = "method '" + method_symbol + "' is declared but has no implementation";
        return TypeKind::Unknown;
      }
      if (env.function_arity != nullptr) {
        auto arity = env.function_arity->find(method_symbol);
        if (arity != env.function_arity->end() && args.size() + 1 != arity->second) {
          error = "method '" + callee.field + "' expects " + std::to_string(arity->second - 1) +
                  " arguments but got " + std::to_string(args.size());
          return TypeKind::Unknown;
        }
      }
      return ret->second;
    }
    error = "non-callable expression";
    return TypeKind::Unknown;
  }

  error = "non-callable expression";
  return TypeKind::Unknown;
}

static TypeKind infer_expression_impl(const HirExprPtr& expr, const TypeEnv& env, std::string& error) {
  if (!expr) {
    error = "empty expression";
    return TypeKind::Unknown;
  }

  if (std::holds_alternative<HirRaw>(expr->node)) {
    error = "unsupported raw expression in HIR checker";
    return TypeKind::Unknown;
  }

  if (std::holds_alternative<HirLit>(expr->node)) {
    const HirLit& lit = std::get<HirLit>(expr->node);
    switch (lit.kind) {
      case HirLitKind::Integer:
        return TypeKind::I32;
      case HirLitKind::Float:
        return TypeKind::F32;
      case HirLitKind::Bool:
        return TypeKind::Bool;
      case HirLitKind::String:
        return TypeKind::String;
      default:
        break;
    }
    error = "unsupported literal kind";
    return TypeKind::Unknown;
  }

  if (std::holds_alternative<HirIdent>(expr->node)) {
    const std::string& name = std::get<HirIdent>(expr->node).name;
    if (env.scope != nullptr) {
      auto it = env.scope->find(name);
      if (it != env.scope->end()) {
        return it->second;
      }
    }
    if (env.enum_variants != nullptr && env.enum_variants->find(name) != env.enum_variants->end()) {
      return TypeKind::EnumType;
    }
    error = "unknown identifier '" + name + "'";
    return TypeKind::Unknown;
  }

  if (std::holds_alternative<HirCall>(expr->node)) {
    return infer_call(std::get<HirCall>(expr->node), env, error);
  }

  if (std::holds_alternative<HirUnary>(expr->node)) {
    const HirUnary& unary = std::get<HirUnary>(expr->node);
    std::string inner_error;
    const TypeKind inner = infer_expression_impl(unary.operand, env, inner_error);
    if (inner == TypeKind::Unknown) {
      error = inner_error;
      return TypeKind::Unknown;
    }
    if (unary.op == "await") {
      return inner;
    }
    if (unary.op == "-") {
      if (!is_numeric_type(inner)) {
        error = "operator '-' requires numeric operand";
        return TypeKind::Unknown;
      }
      return inner;
    }
    if (unary.op == "!") {
      if (inner != TypeKind::Bool) {
        error = "operator '!' requires bool operand";
        return TypeKind::Unknown;
      }
      return TypeKind::Bool;
    }
    error = "unsupported unary operator '" + unary.op + "'";
    return TypeKind::Unknown;
  }

  if (std::holds_alternative<HirBin>(expr->node)) {
    const HirBin& bin = std::get<HirBin>(expr->node);
    std::string lhs_error;
    std::string rhs_error;
    const TypeKind lhs = infer_expression_impl(bin.lhs, env, lhs_error);
    const TypeKind rhs = infer_expression_impl(bin.rhs, env, rhs_error);
    if (lhs == TypeKind::Unknown) {
      error = lhs_error;
      return TypeKind::Unknown;
    }
    if (rhs == TypeKind::Unknown) {
      error = rhs_error;
      return TypeKind::Unknown;
    }

    if (bin.op == "+" || bin.op == "-" || bin.op == "*" || bin.op == "/") {
      if (!is_numeric_type(lhs) || !is_numeric_type(rhs)) {
        error = "operator '" + bin.op + "' requires numeric operands";
        return TypeKind::Unknown;
      }
      return combine_numeric(lhs, rhs);
    }
    if (bin.op == "<" || bin.op == "<=" || bin.op == ">" || bin.op == ">=") {
      if (!is_numeric_type(lhs) || !is_numeric_type(rhs)) {
        error = "operator '" + bin.op + "' requires numeric operands";
        return TypeKind::Unknown;
      }
      return TypeKind::Bool;
    }
    if (bin.op == "==" || bin.op == "!=") {
      if (lhs != rhs && !(is_numeric_type(lhs) && is_numeric_type(rhs))) {
        error = "operator '" + bin.op + "' requires operands of same type";
        return TypeKind::Unknown;
      }
      return TypeKind::Bool;
    }
    if (bin.op == "&&" || bin.op == "||") {
      if (lhs != TypeKind::Bool || rhs != TypeKind::Bool) {
        error = "operator '" + bin.op + "' requires bool operands";
        return TypeKind::Unknown;
      }
      return TypeKind::Bool;
    }

    error = "unsupported binary operator '" + bin.op + "'";
    return TypeKind::Unknown;
  }

  if (std::holds_alternative<HirField>(expr->node)) {
    const HirField& field = std::get<HirField>(expr->node);
    if (field.base && std::holds_alternative<HirIdent>(field.base->node) && env.struct_bindings != nullptr &&
        env.struct_fields != nullptr) {
      const std::string& base_name = std::get<HirIdent>(field.base->node).name;
      auto binding = env.struct_bindings->find(base_name);
      if (binding != env.struct_bindings->end()) {
        const std::string& struct_name = binding->second;
        auto fields = env.struct_fields->find(struct_name);
        if (fields == env.struct_fields->end() ||
            std::find(fields->second.begin(), fields->second.end(), field.field) == fields->second.end()) {
          error = "unknown field '" + field.field + "' on struct '" + struct_name + "'";
          return TypeKind::Unknown;
        }
        if (env.struct_field_types != nullptr) {
          auto type_it = env.struct_field_types->find(struct_name + "." + field.field);
          if (type_it != env.struct_field_types->end()) {
            const TypeKind parsed = parse_declared_type_expr(type_it->second, env);
            return parsed == TypeKind::Unknown ? TypeKind::I32 : parsed;
          }
        }
        return TypeKind::I32;
      }
    }

    std::string base_error;
    const TypeKind base_type = infer_expression_impl(field.base, env, base_error);
    if (base_type == TypeKind::TupleType) {
      bool all_digit = !field.field.empty();
      for (char ch : field.field) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
          all_digit = false;
          break;
        }
      }
      if (all_digit) {
        return TypeKind::I32;
      }
    }
    error = "unknown field access '" + field.field + "'";
    return TypeKind::Unknown;
  }

  if (std::holds_alternative<HirIndex>(expr->node)) {
    const HirIndex& index = std::get<HirIndex>(expr->node);
    std::string base_error;
    std::string idx_error;
    const TypeKind base_type = infer_expression_impl(index.base, env, base_error);
    const TypeKind idx_type = infer_expression_impl(index.index, env, idx_error);
    if (base_type == TypeKind::Unknown) {
      error = base_error;
      return TypeKind::Unknown;
    }
    if (idx_type == TypeKind::Unknown) {
      error = idx_error;
      return TypeKind::Unknown;
    }
    if (base_type != TypeKind::ArrayType) {
      error = "index access requires array value";
      return TypeKind::Unknown;
    }
    if (!is_numeric_type(idx_type) && idx_type != TypeKind::Bool) {
      error = "array index must be numeric";
      return TypeKind::Unknown;
    }
    return TypeKind::I32;
  }

  if (std::holds_alternative<HirTuple>(expr->node)) {
    return TypeKind::TupleType;
  }
  if (std::holds_alternative<HirArray>(expr->node)) {
    return TypeKind::ArrayType;
  }

  error = "unsupported expression node";
  return TypeKind::Unknown;
}

}  // namespace

HirExprPtr lower_ast_expr(const syntax::AstExprPtr& expr) {
  if (!expr) {
    return nullptr;
  }

  switch (expr->kind) {
    case syntax::AstExprKind::Raw:
      return make_hir_expr(HirRaw{expr->text}, expr->span);
    case syntax::AstExprKind::Atom: {
      if (expr->text == "true" || expr->text == "false") {
        return make_hir_expr(HirLit{HirLitKind::Bool, expr->text}, expr->span);
      }
      if (is_integer_atom(expr->text)) {
        return make_hir_expr(HirLit{HirLitKind::Integer, expr->text}, expr->span);
      }
      if (is_float_atom(expr->text)) {
        return make_hir_expr(HirLit{HirLitKind::Float, expr->text}, expr->span);
      }
      if (is_string_atom(expr->text) || is_interpolated_string_atom(expr->text)) {
        return make_hir_expr(HirLit{HirLitKind::String, expr->text}, expr->span);
      }
      if (!expr->text.empty() && is_ident_start(expr->text[0])) {
        bool valid_ident = true;
        for (std::size_t i = 1; i < expr->text.size(); ++i) {
          if (!is_ident_body(expr->text[i])) {
            valid_ident = false;
            break;
          }
        }
        if (valid_ident) {
          return make_hir_expr(HirIdent{expr->text}, expr->span);
        }
      }
      return make_hir_expr(HirRaw{expr->text}, expr->span);
    }
    case syntax::AstExprKind::Unary: {
      if (expr->children.empty()) {
        return make_hir_expr(HirRaw{expr->text}, expr->span);
      }
      return make_hir_expr(HirUnary{expr->op, lower_ast_expr(expr->children[0])}, expr->span);
    }
    case syntax::AstExprKind::Binary: {
      if (expr->children.size() < 2) {
        return make_hir_expr(HirRaw{expr->text}, expr->span);
      }
      return make_hir_expr(
          HirBin{expr->op, lower_ast_expr(expr->children[0]), lower_ast_expr(expr->children[1])}, expr->span);
    }
    case syntax::AstExprKind::Call: {
      HirCall out;
      if (!expr->children.empty()) {
        out.callee = lower_ast_expr(expr->children[0]);
      }
      for (std::size_t i = 1; i < expr->children.size(); ++i) {
        out.args.push_back(lower_ast_expr(expr->children[i]));
      }
      return make_hir_expr(std::move(out), expr->span);
    }
    case syntax::AstExprKind::Field: {
      if (expr->children.empty()) {
        return make_hir_expr(HirRaw{expr->text}, expr->span);
      }
      return make_hir_expr(HirField{lower_ast_expr(expr->children[0]), expr->op}, expr->span);
    }
    case syntax::AstExprKind::Index: {
      if (expr->children.size() < 2) {
        return make_hir_expr(HirRaw{expr->text}, expr->span);
      }
      return make_hir_expr(HirIndex{lower_ast_expr(expr->children[0]), lower_ast_expr(expr->children[1])}, expr->span);
    }
    case syntax::AstExprKind::Tuple: {
      HirTuple out;
      for (const auto& item : expr->children) {
        out.items.push_back(lower_ast_expr(item));
      }
      return make_hir_expr(std::move(out), expr->span);
    }
    case syntax::AstExprKind::Array: {
      HirArray out;
      for (const auto& item : expr->children) {
        out.items.push_back(lower_ast_expr(item));
      }
      return make_hir_expr(std::move(out), expr->span);
    }
    default:
      return make_hir_expr(HirRaw{expr->text}, expr->span);
  }
}

TypeKind infer_expression(const HirExprPtr& expr, const TypeEnv& env, std::string& error) {
  error.clear();
  const TypeKind inferred = infer_expression_impl(expr, env, error);
  if (expr && inferred != TypeKind::Unknown) {
    expr->inferred_type = ty::from_type_kind(inferred);
  }
  return inferred;
}

bool check_expression(const HirExprPtr& expr, TypeKind expected, const TypeEnv& env, std::string& error) {
  const TypeKind inferred = infer_expression(expr, env, error);
  if (inferred == TypeKind::Unknown) {
    return false;
  }
  if (!is_assignable(expected, inferred)) {
    error = "expected " + std::to_string(static_cast<int>(expected)) + " but inferred " +
            std::to_string(static_cast<int>(inferred));
    return false;
  }
  return true;
}

}  // namespace thagc::hir
