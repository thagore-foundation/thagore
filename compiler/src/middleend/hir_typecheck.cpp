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

static std::string type_kind_name(TypeKind kind) {
  switch (kind) {
    case TypeKind::I32:
      return "i32";
    case TypeKind::I64:
      return "i64";
    case TypeKind::F32:
      return "f32";
    case TypeKind::F64:
      return "f64";
    case TypeKind::Bool:
      return "bool";
    case TypeKind::String:
      return "string";
    case TypeKind::Option:
      return "Option";
    case TypeKind::Result:
      return "Result";
    case TypeKind::List:
      return "List";
    case TypeKind::Rc:
      return "Rc";
    case TypeKind::Arc:
      return "Arc";
    case TypeKind::FunctionType:
      return "fn";
    case TypeKind::Ptr:
      return "ptr";
    case TypeKind::StructType:
      return "struct";
    case TypeKind::EnumType:
      return "enum";
    case TypeKind::TupleType:
      return "tuple";
    case TypeKind::ArrayType:
      return "array";
    case TypeKind::Void:
      return "void";
    case TypeKind::Unknown:
    default:
      return "unknown";
  }
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

static ty::Ty make_ty_var_type(std::uint32_t id) {
  return ty::Ty{ty::TyVar{id}};
}

static ty::TyPtr make_ty_ptr(const ty::Ty& value) {
  return std::make_shared<ty::Ty>(value);
}

static ty::Ty clone_ty(const ty::Ty& value);

static std::string ty_name(const ty::Ty& value);

static ty::Ty clone_ty(const ty::Ty& value) {
  if (const auto* named = std::get_if<ty::TyNamed>(&value.data)) {
    ty::TyNamed out;
    out.name = named->name;
    out.args.reserve(named->args.size());
    for (const auto& arg : named->args) {
      if (!arg) {
        continue;
      }
      out.args.push_back(make_ty_ptr(clone_ty(*arg)));
    }
    return ty::Ty{std::move(out)};
  }
  if (const auto* fn = std::get_if<ty::TyFn>(&value.data)) {
    ty::TyFn out;
    out.params.reserve(fn->params.size());
    for (const auto& param : fn->params) {
      if (!param) {
        continue;
      }
      out.params.push_back(make_ty_ptr(clone_ty(*param)));
    }
    if (fn->ret) {
      out.ret = make_ty_ptr(clone_ty(*fn->ret));
    }
    return ty::Ty{std::move(out)};
  }
  return value;
}

static ty::Ty apply_with_table(const ty::Ty& value, const std::vector<std::optional<ty::Ty>>& table) {
  if (const auto* var = std::get_if<ty::TyVar>(&value.data)) {
    if (var->id < table.size() && table[var->id].has_value()) {
      return apply_with_table(*table[var->id], table);
    }
    return value;
  }
  if (const auto* named = std::get_if<ty::TyNamed>(&value.data)) {
    ty::TyNamed out;
    out.name = named->name;
    out.args.reserve(named->args.size());
    for (const auto& arg : named->args) {
      if (!arg) {
        continue;
      }
      out.args.push_back(make_ty_ptr(apply_with_table(*arg, table)));
    }
    return ty::Ty{std::move(out)};
  }
  if (const auto* fn = std::get_if<ty::TyFn>(&value.data)) {
    ty::TyFn out;
    out.params.reserve(fn->params.size());
    for (const auto& param : fn->params) {
      if (!param) {
        continue;
      }
      out.params.push_back(make_ty_ptr(apply_with_table(*param, table)));
    }
    if (fn->ret) {
      out.ret = make_ty_ptr(apply_with_table(*fn->ret, table));
    }
    return ty::Ty{std::move(out)};
  }
  return value;
}

static bool occurs_in_ty(std::uint32_t needle, const ty::Ty& value, const std::vector<std::optional<ty::Ty>>& table) {
  const ty::Ty reduced = apply_with_table(value, table);
  if (const auto* var = std::get_if<ty::TyVar>(&reduced.data)) {
    return var->id == needle;
  }
  if (const auto* named = std::get_if<ty::TyNamed>(&reduced.data)) {
    for (const auto& arg : named->args) {
      if (arg && occurs_in_ty(needle, *arg, table)) {
        return true;
      }
    }
    return false;
  }
  if (const auto* fn = std::get_if<ty::TyFn>(&reduced.data)) {
    for (const auto& param : fn->params) {
      if (param && occurs_in_ty(needle, *param, table)) {
        return true;
      }
    }
    return fn->ret && occurs_in_ty(needle, *fn->ret, table);
  }
  return false;
}

static ty::Ty make_named_ty(std::string name, const std::vector<ty::Ty>& args) {
  ty::TyNamed named;
  named.name = std::move(name);
  named.args.reserve(args.size());
  for (const auto& arg : args) {
    named.args.push_back(make_ty_ptr(arg));
  }
  return ty::Ty{std::move(named)};
}

static ty::Ty primitive_ty_from_kind(TypeKind kind) {
  if (kind == TypeKind::I32) {
    return ty::Ty{ty::TyInt{32, true}};
  }
  if (kind == TypeKind::I64) {
    return ty::Ty{ty::TyInt{64, true}};
  }
  if (kind == TypeKind::F32) {
    return ty::Ty{ty::TyFloat{32}};
  }
  if (kind == TypeKind::F64) {
    return ty::Ty{ty::TyFloat{64}};
  }
  if (kind == TypeKind::Bool) {
    return ty::Ty{ty::TyBool{}};
  }
  if (kind == TypeKind::String) {
    return ty::Ty{ty::TyStr{}};
  }
  if (kind == TypeKind::Void) {
    return ty::Ty{ty::TyUnit{}};
  }
  if (kind == TypeKind::Ptr) {
    return make_named_ty("ptr", {});
  }
  if (kind == TypeKind::TupleType) {
    return make_named_ty("tuple", {});
  }
  if (kind == TypeKind::ArrayType) {
    return make_named_ty("array", {});
  }
  if (kind == TypeKind::StructType) {
    return make_named_ty("struct", {});
  }
  if (kind == TypeKind::EnumType) {
    return make_named_ty("enum", {});
  }
  if (kind == TypeKind::FunctionType) {
    return make_named_ty("fn", {});
  }
  if (kind == TypeKind::Option) {
    return make_named_ty("Option", {});
  }
  if (kind == TypeKind::Result) {
    return make_named_ty("Result", {});
  }
  if (kind == TypeKind::List) {
    return make_named_ty("List", {});
  }
  if (kind == TypeKind::Rc) {
    return make_named_ty("Rc", {});
  }
  if (kind == TypeKind::Arc) {
    return make_named_ty("Arc", {});
  }
  return make_ty_var_type(0);
}

static ty::Ty ty_from_type_kind_with_fresh(TypeKind kind, Unifier& unifier) {
  if (kind == TypeKind::Option) {
    return make_named_ty("Option", {make_ty_var_type(unifier.fresh().id)});
  }
  if (kind == TypeKind::Result) {
    return make_named_ty("Result", {make_ty_var_type(unifier.fresh().id), make_ty_var_type(unifier.fresh().id)});
  }
  if (kind == TypeKind::List) {
    return make_named_ty("List", {make_ty_var_type(unifier.fresh().id)});
  }
  if (kind == TypeKind::Rc) {
    return make_named_ty("Rc", {make_ty_var_type(unifier.fresh().id)});
  }
  if (kind == TypeKind::Arc) {
    return make_named_ty("Arc", {make_ty_var_type(unifier.fresh().id)});
  }
  if (kind == TypeKind::ArrayType) {
    return make_named_ty("array", {make_ty_var_type(unifier.fresh().id)});
  }
  return primitive_ty_from_kind(kind);
}

static TypeKind coarse_type_kind_from_ty(const ty::Ty& value) {
  if (const auto* int_ty = std::get_if<ty::TyInt>(&value.data)) {
    return int_ty->bits >= 64 ? TypeKind::I64 : TypeKind::I32;
  }
  if (const auto* float_ty = std::get_if<ty::TyFloat>(&value.data)) {
    return float_ty->bits >= 64 ? TypeKind::F64 : TypeKind::F32;
  }
  if (std::holds_alternative<ty::TyBool>(value.data)) {
    return TypeKind::Bool;
  }
  if (std::holds_alternative<ty::TyStr>(value.data)) {
    return TypeKind::String;
  }
  if (std::holds_alternative<ty::TyUnit>(value.data)) {
    return TypeKind::Void;
  }
  if (const auto* named = std::get_if<ty::TyNamed>(&value.data)) {
    if (named->name == "Option") {
      return TypeKind::Option;
    }
    if (named->name == "Result") {
      return TypeKind::Result;
    }
    if (named->name == "List") {
      return TypeKind::List;
    }
    if (named->name == "Rc") {
      return TypeKind::Rc;
    }
    if (named->name == "Arc") {
      return TypeKind::Arc;
    }
    if (named->name == "ptr") {
      return TypeKind::Ptr;
    }
    if (named->name == "fn") {
      return TypeKind::FunctionType;
    }
    if (named->name == "tuple") {
      return TypeKind::TupleType;
    }
    if (named->name == "array") {
      return TypeKind::ArrayType;
    }
    if (named->name == "struct") {
      return TypeKind::StructType;
    }
    if (named->name == "enum") {
      return TypeKind::EnumType;
    }
  }
  return TypeKind::Unknown;
}

static std::string ty_name(const ty::Ty& value) {
  if (const auto* int_ty = std::get_if<ty::TyInt>(&value.data)) {
    return int_ty->bits >= 64 ? "i64" : "i32";
  }
  if (const auto* float_ty = std::get_if<ty::TyFloat>(&value.data)) {
    return float_ty->bits >= 64 ? "f64" : "f32";
  }
  if (std::holds_alternative<ty::TyBool>(value.data)) {
    return "bool";
  }
  if (std::holds_alternative<ty::TyStr>(value.data)) {
    return "string";
  }
  if (std::holds_alternative<ty::TyUnit>(value.data)) {
    return "void";
  }
  if (const auto* var = std::get_if<ty::TyVar>(&value.data)) {
    return "'t" + std::to_string(var->id);
  }
  if (const auto* named = std::get_if<ty::TyNamed>(&value.data)) {
    if (named->args.empty()) {
      return named->name;
    }
    std::string rendered = named->name + "<";
    for (std::size_t i = 0; i < named->args.size(); ++i) {
      if (i > 0) {
        rendered += ", ";
      }
      rendered += named->args[i] ? ty_name(*named->args[i]) : "unknown";
    }
    rendered += ">";
    return rendered;
  }
  if (const auto* fn = std::get_if<ty::TyFn>(&value.data)) {
    std::string rendered = "fn(";
    for (std::size_t i = 0; i < fn->params.size(); ++i) {
      if (i > 0) {
        rendered += ", ";
      }
      rendered += fn->params[i] ? ty_name(*fn->params[i]) : "unknown";
    }
    rendered += ")";
    rendered += " -> ";
    rendered += fn->ret ? ty_name(*fn->ret) : "unknown";
    return rendered;
  }
  return "unknown";
}

static bool is_ty_numeric(const ty::Ty& value) {
  const TypeKind kind = coarse_type_kind_from_ty(value);
  return is_numeric_type(kind);
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

Unifier::Unifier() {
  table_.push_back(std::nullopt);
}

ty::TyVar Unifier::fresh() {
  const std::uint32_t id = static_cast<std::uint32_t>(table_.size());
  table_.push_back(std::nullopt);
  return ty::TyVar{id};
}

ty::Ty Unifier::apply(const ty::Ty& value) const {
  return apply_with_table(value, table_);
}

bool Unifier::unify(const ty::Ty& a, const ty::Ty& b, std::string& error) {
  const ty::Ty left = apply(a);
  const ty::Ty right = apply(b);

  if (const auto* left_var = std::get_if<ty::TyVar>(&left.data)) {
    if (const auto* right_var = std::get_if<ty::TyVar>(&right.data)) {
      if (left_var->id == right_var->id) {
        return true;
      }
    }
    if (left_var->id >= table_.size()) {
      table_.resize(left_var->id + 1);
    }
    if (occurs_in_ty(left_var->id, right, table_)) {
      error = "recursive type while unifying " + ty_name(left) + " with " + ty_name(right);
      return false;
    }
    table_[left_var->id] = right;
    return true;
  }

  if (const auto* right_var = std::get_if<ty::TyVar>(&right.data)) {
    if (right_var->id >= table_.size()) {
      table_.resize(right_var->id + 1);
    }
    if (occurs_in_ty(right_var->id, left, table_)) {
      error = "recursive type while unifying " + ty_name(left) + " with " + ty_name(right);
      return false;
    }
    table_[right_var->id] = left;
    return true;
  }

  if (const auto* lhs = std::get_if<ty::TyInt>(&left.data)) {
    const auto* rhs = std::get_if<ty::TyInt>(&right.data);
    if (rhs != nullptr && lhs->bits == rhs->bits && lhs->is_signed == rhs->is_signed) {
      return true;
    }
    error = "cannot unify " + ty_name(left) + " with " + ty_name(right);
    return false;
  }
  if (const auto* lhs = std::get_if<ty::TyFloat>(&left.data)) {
    const auto* rhs = std::get_if<ty::TyFloat>(&right.data);
    if (rhs != nullptr && lhs->bits == rhs->bits) {
      return true;
    }
    error = "cannot unify " + ty_name(left) + " with " + ty_name(right);
    return false;
  }
  if (std::holds_alternative<ty::TyBool>(left.data) || std::holds_alternative<ty::TyUnit>(left.data) ||
      std::holds_alternative<ty::TyStr>(left.data)) {
    if (left.data.index() == right.data.index()) {
      return true;
    }
    error = "cannot unify " + ty_name(left) + " with " + ty_name(right);
    return false;
  }
  if (const auto* lhs = std::get_if<ty::TyNamed>(&left.data)) {
    const auto* rhs = std::get_if<ty::TyNamed>(&right.data);
    if (rhs == nullptr || lhs->name != rhs->name) {
      error = "cannot unify " + ty_name(left) + " with " + ty_name(right);
      return false;
    }
    if (lhs->args.size() != rhs->args.size()) {
      error = "cannot unify " + ty_name(left) + " with " + ty_name(right);
      return false;
    }
    for (std::size_t i = 0; i < lhs->args.size(); ++i) {
      if (!lhs->args[i] || !rhs->args[i]) {
        continue;
      }
      if (!unify(*lhs->args[i], *rhs->args[i], error)) {
        return false;
      }
    }
    return true;
  }
  if (const auto* lhs = std::get_if<ty::TyFn>(&left.data)) {
    const auto* rhs = std::get_if<ty::TyFn>(&right.data);
    if (rhs == nullptr || lhs->params.size() != rhs->params.size()) {
      error = "cannot unify " + ty_name(left) + " with " + ty_name(right);
      return false;
    }
    for (std::size_t i = 0; i < lhs->params.size(); ++i) {
      if (!lhs->params[i] || !rhs->params[i]) {
        continue;
      }
      if (!unify(*lhs->params[i], *rhs->params[i], error)) {
        return false;
      }
    }
    if (lhs->ret && rhs->ret) {
      return unify(*lhs->ret, *rhs->ret, error);
    }
    return lhs->ret == nullptr && rhs->ret == nullptr;
  }
  error = "cannot unify " + ty_name(left) + " with " + ty_name(right);
  return false;
}

namespace {

static bool infer_expression_ty_impl(const HirExprPtr& expr, const TypeEnv& env, Unifier& unifier, ty::Ty& out,
                                     std::string& error);

static bool unify_as_named(const ty::Ty& value, const std::string& name, const std::vector<ty::Ty>& args, Unifier& unifier,
                           std::string& error) {
  return unifier.unify(value, make_named_ty(name, args), error);
}

static bool infer_call_ty(const HirCall& call, const TypeEnv& env, Unifier& unifier, ty::Ty& out, std::string& error) {
  std::vector<ty::Ty> args;
  args.reserve(call.args.size());
  for (const auto& arg_expr : call.args) {
    ty::Ty arg_ty = make_ty_var_type(unifier.fresh().id);
    if (!infer_expression_ty_impl(arg_expr, env, unifier, arg_ty, error)) {
      return false;
    }
    args.push_back(unifier.apply(arg_ty));
  }

  if (!call.callee) {
    error = "non-callable expression";
    return false;
  }
  if (std::holds_alternative<HirIdent>(call.callee->node)) {
    const std::string& name = std::get<HirIdent>(call.callee->node).name;
    if (name == "Some") {
      if (args.size() != 1) {
        error = "Some() expects 1 argument";
        return false;
      }
      out = make_named_ty("Option", {args[0]});
      return true;
    }
    if (name == "None") {
      if (!args.empty()) {
        error = "None() expects 0 arguments";
        return false;
      }
      out = make_named_ty("Option", {make_ty_var_type(unifier.fresh().id)});
      return true;
    }
    if (name == "Ok") {
      if (args.size() != 1) {
        error = "Ok() expects 1 argument";
        return false;
      }
      out = make_named_ty("Result", {args[0], make_ty_var_type(unifier.fresh().id)});
      return true;
    }
    if (name == "Err") {
      if (args.size() != 1) {
        error = "Err() expects 1 argument";
        return false;
      }
      out = make_named_ty("Result", {make_ty_var_type(unifier.fresh().id), args[0]});
      return true;
    }
    if (name == "is_some" || name == "is_none") {
      if (args.size() != 1) {
        error = "predicate '" + name + "' expects 1 argument";
        return false;
      }
      if (!unify_as_named(args[0], "Option", {make_ty_var_type(unifier.fresh().id)}, unifier, error)) {
        return false;
      }
      out = ty::Ty{ty::TyBool{}};
      return true;
    }
    if (name == "is_ok" || name == "is_err") {
      if (args.size() != 1) {
        error = "predicate '" + name + "' expects 1 argument";
        return false;
      }
      if (!unify_as_named(
              args[0], "Result", {make_ty_var_type(unifier.fresh().id), make_ty_var_type(unifier.fresh().id)}, unifier,
              error)) {
        return false;
      }
      out = ty::Ty{ty::TyBool{}};
      return true;
    }
    if (name == "unwrap") {
      if (args.size() != 1) {
        error = "unwrap() expects 1 argument";
        return false;
      }
      const ty::Ty value = unifier.apply(args[0]);
      const TypeKind value_kind = coarse_type_kind_from_ty(value);
      const ty::Ty inner = make_ty_var_type(unifier.fresh().id);
      if (value_kind == TypeKind::Option) {
        if (!unify_as_named(value, "Option", {inner}, unifier, error)) {
          return false;
        }
        out = unifier.apply(inner);
        return true;
      }
      if (value_kind == TypeKind::Result) {
        if (!unify_as_named(value, "Result", {inner, make_ty_var_type(unifier.fresh().id)}, unifier, error)) {
          return false;
        }
        out = unifier.apply(inner);
        return true;
      }
      error = "unwrap() expects Option<T> or Result<T, E>";
      return false;
    }
    if (name == "unwrap_or") {
      if (args.size() != 2) {
        error = "unwrap_or() expects 2 arguments";
        return false;
      }
      const ty::Ty value = unifier.apply(args[0]);
      const TypeKind value_kind = coarse_type_kind_from_ty(value);
      const ty::Ty inner = make_ty_var_type(unifier.fresh().id);
      if (value_kind == TypeKind::Option) {
        if (!unify_as_named(value, "Option", {inner}, unifier, error)) {
          return false;
        }
      } else if (value_kind == TypeKind::Result) {
        if (!unify_as_named(value, "Result", {inner, make_ty_var_type(unifier.fresh().id)}, unifier, error)) {
          return false;
        }
      } else {
        error = "unwrap_or() expects Option<T> or Result<T, E>";
        return false;
      }
      if (!unifier.unify(inner, args[1], error)) {
        return false;
      }
      out = unifier.apply(inner);
      return true;
    }
    if (name == "Rc" || name == "Arc") {
      if (args.size() != 1) {
        error = name + "() expects exactly 1 argument";
        return false;
      }
      out = make_named_ty(name, {args[0]});
      return true;
    }
    if (name == "len") {
      if (args.size() != 1) {
        error = "len() expects 1 argument";
        return false;
      }
      out = ty::Ty{ty::TyInt{32, true}};
      return true;
    }
    if (name == "print" || name == "open" || name == "close" || name == "read" || name == "write" || name == "spawn") {
      out = ty::Ty{ty::TyInt{32, true}};
      return true;
    }
    if (env.enum_variants != nullptr && env.enum_variants->find(name) != env.enum_variants->end()) {
      if (args.size() > 1) {
        error = "enum payload constructor '" + name + "' expects at most 1 argument";
        return false;
      }
      out = make_named_ty("enum", {});
      return true;
    }
    if (env.function_returns != nullptr) {
      auto ret = env.function_returns->find(name);
      if (ret != env.function_returns->end()) {
        if (env.function_arity != nullptr) {
          auto arity = env.function_arity->find(name);
          if (arity != env.function_arity->end() && args.size() != arity->second) {
            error = "function '" + name + "' expects " + std::to_string(arity->second) +
                    " arguments but got " + std::to_string(args.size());
            return false;
          }
        }
        out = ty_from_type_kind_with_fresh(ret->second, unifier);
        return true;
      }
    }
    if (env.scope != nullptr) {
      auto local_callable = env.scope->find(name);
      if (local_callable != env.scope->end() && local_callable->second == TypeKind::FunctionType) {
        out = ty::Ty{ty::TyInt{32, true}};
        return true;
      }
    }
    if (env.struct_names != nullptr && env.struct_names->find(name) != env.struct_names->end()) {
      out = make_named_ty("struct", {});
      return true;
    }
    error = "unknown callable '" + name + "'";
    return false;
  }
  if (std::holds_alternative<HirField>(call.callee->node)) {
    const HirField& callee = std::get<HirField>(call.callee->node);
    if (callee.base && std::holds_alternative<HirIdent>(callee.base->node) && env.struct_bindings != nullptr &&
        env.struct_methods != nullptr && env.function_returns != nullptr) {
      const std::string& receiver = std::get<HirIdent>(callee.base->node).name;
      auto binding = env.struct_bindings->find(receiver);
      if (binding == env.struct_bindings->end()) {
        error = "unknown method '" + callee.field + "'";
        return false;
      }
      const std::string& struct_name = binding->second;
      auto methods = env.struct_methods->find(struct_name);
      if (methods == env.struct_methods->end() ||
          std::find(methods->second.begin(), methods->second.end(), callee.field) == methods->second.end()) {
        error = "unknown method '" + callee.field + "' on struct '" + struct_name + "'";
        return false;
      }
      const std::string method_symbol = struct_name + "." + callee.field;
      auto ret = env.function_returns->find(method_symbol);
      if (ret == env.function_returns->end()) {
        error = "method '" + method_symbol + "' is declared but has no implementation";
        return false;
      }
      if (env.function_arity != nullptr) {
        auto arity = env.function_arity->find(method_symbol);
        if (arity != env.function_arity->end() && args.size() + 1 != arity->second) {
          error = "method '" + callee.field + "' expects " + std::to_string(arity->second - 1) +
                  " arguments but got " + std::to_string(args.size());
          return false;
        }
      }
      out = ty_from_type_kind_with_fresh(ret->second, unifier);
      return true;
    }
  }
  error = "non-callable expression";
  return false;
}

static bool infer_expression_ty_impl(const HirExprPtr& expr, const TypeEnv& env, Unifier& unifier, ty::Ty& out,
                                     std::string& error) {
  if (!expr) {
    error = "empty expression";
    return false;
  }
  if (std::holds_alternative<HirRaw>(expr->node)) {
    error = "unsupported raw expression in HIR checker";
    return false;
  }
  if (std::holds_alternative<HirLit>(expr->node)) {
    const HirLit& lit = std::get<HirLit>(expr->node);
    if (lit.kind == HirLitKind::Integer) {
      out = ty::Ty{ty::TyInt{32, true}};
      return true;
    }
    if (lit.kind == HirLitKind::Float) {
      out = ty::Ty{ty::TyFloat{32}};
      return true;
    }
    if (lit.kind == HirLitKind::Bool) {
      out = ty::Ty{ty::TyBool{}};
      return true;
    }
    if (lit.kind == HirLitKind::String) {
      out = ty::Ty{ty::TyStr{}};
      return true;
    }
    error = "unsupported literal kind";
    return false;
  }
  if (std::holds_alternative<HirIdent>(expr->node)) {
    const std::string& name = std::get<HirIdent>(expr->node).name;
    if (env.scope_tys != nullptr) {
      auto typed_it = env.scope_tys->find(name);
      if (typed_it != env.scope_tys->end()) {
        out = clone_ty(typed_it->second);
        return true;
      }
    }
    if (env.scope != nullptr) {
      auto it = env.scope->find(name);
      if (it != env.scope->end()) {
        out = ty_from_type_kind_with_fresh(it->second, unifier);
        return true;
      }
    }
    if (env.enum_variants != nullptr && env.enum_variants->find(name) != env.enum_variants->end()) {
      out = make_named_ty("enum", {});
      return true;
    }
    error = "unknown identifier '" + name + "'";
    return false;
  }
  if (std::holds_alternative<HirCall>(expr->node)) {
    return infer_call_ty(std::get<HirCall>(expr->node), env, unifier, out, error);
  }
  if (std::holds_alternative<HirUnary>(expr->node)) {
    const HirUnary& unary = std::get<HirUnary>(expr->node);
    ty::Ty inner = make_ty_var_type(unifier.fresh().id);
    if (!infer_expression_ty_impl(unary.operand, env, unifier, inner, error)) {
      return false;
    }
    inner = unifier.apply(inner);
    if (unary.op == "await") {
      out = inner;
      return true;
    }
    if (unary.op == "-") {
      if (!is_ty_numeric(inner)) {
        error = "operator '-' requires numeric operand";
        return false;
      }
      out = inner;
      return true;
    }
    if (unary.op == "!") {
      if (!unifier.unify(inner, ty::Ty{ty::TyBool{}}, error)) {
        error = "operator '!' requires bool operand";
        return false;
      }
      out = ty::Ty{ty::TyBool{}};
      return true;
    }
    error = "unsupported unary operator '" + unary.op + "'";
    return false;
  }
  if (std::holds_alternative<HirBin>(expr->node)) {
    const HirBin& bin = std::get<HirBin>(expr->node);
    ty::Ty lhs = make_ty_var_type(unifier.fresh().id);
    ty::Ty rhs = make_ty_var_type(unifier.fresh().id);
    if (!infer_expression_ty_impl(bin.lhs, env, unifier, lhs, error)) {
      return false;
    }
    if (!infer_expression_ty_impl(bin.rhs, env, unifier, rhs, error)) {
      return false;
    }
    lhs = unifier.apply(lhs);
    rhs = unifier.apply(rhs);

    if (bin.op == "+" || bin.op == "-" || bin.op == "*" || bin.op == "/") {
      if (std::holds_alternative<ty::TyVar>(lhs.data)) {
        if (!unifier.unify(lhs, ty::Ty{ty::TyInt{32, true}}, error)) {
          return false;
        }
        lhs = unifier.apply(lhs);
      }
      if (std::holds_alternative<ty::TyVar>(rhs.data)) {
        if (!unifier.unify(rhs, ty::Ty{ty::TyInt{32, true}}, error)) {
          return false;
        }
        rhs = unifier.apply(rhs);
      }
      const TypeKind lhs_kind = coarse_type_kind_from_ty(lhs);
      const TypeKind rhs_kind = coarse_type_kind_from_ty(rhs);
      if (!is_numeric_type(lhs_kind) || !is_numeric_type(rhs_kind)) {
        error = "operator '" + bin.op + "' requires numeric operands";
        return false;
      }
      out = primitive_ty_from_kind(combine_numeric(lhs_kind, rhs_kind));
      return true;
    }
    if (bin.op == "<" || bin.op == "<=" || bin.op == ">" || bin.op == ">=") {
      const TypeKind lhs_kind = coarse_type_kind_from_ty(lhs);
      const TypeKind rhs_kind = coarse_type_kind_from_ty(rhs);
      if (!is_numeric_type(lhs_kind) || !is_numeric_type(rhs_kind)) {
        error = "operator '" + bin.op + "' requires numeric operands";
        return false;
      }
      out = ty::Ty{ty::TyBool{}};
      return true;
    }
    if (bin.op == "==" || bin.op == "!=") {
      if (!unifier.unify(lhs, rhs, error)) {
        const TypeKind lhs_kind = coarse_type_kind_from_ty(lhs);
        const TypeKind rhs_kind = coarse_type_kind_from_ty(rhs);
        if (!(is_numeric_type(lhs_kind) && is_numeric_type(rhs_kind))) {
          error = "operator '" + bin.op + "' requires operands of same type";
          return false;
        }
      }
      out = ty::Ty{ty::TyBool{}};
      return true;
    }
    if (bin.op == "&&" || bin.op == "||") {
      if (!unifier.unify(lhs, ty::Ty{ty::TyBool{}}, error) || !unifier.unify(rhs, ty::Ty{ty::TyBool{}}, error)) {
        error = "operator '" + bin.op + "' requires bool operands";
        return false;
      }
      out = ty::Ty{ty::TyBool{}};
      return true;
    }
    error = "unsupported binary operator '" + bin.op + "'";
    return false;
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
          return false;
        }
        if (env.struct_field_types != nullptr) {
          auto type_it = env.struct_field_types->find(struct_name + "." + field.field);
          if (type_it != env.struct_field_types->end()) {
            out = primitive_ty_from_kind(parse_declared_type_expr(type_it->second, env));
            if (coarse_type_kind_from_ty(out) == TypeKind::Unknown) {
              out = ty::Ty{ty::TyInt{32, true}};
            }
            return true;
          }
        }
        out = ty::Ty{ty::TyInt{32, true}};
        return true;
      }
    }
    ty::Ty base_ty = make_ty_var_type(unifier.fresh().id);
    if (!infer_expression_ty_impl(field.base, env, unifier, base_ty, error)) {
      return false;
    }
    if (coarse_type_kind_from_ty(unifier.apply(base_ty)) == TypeKind::TupleType) {
      bool all_digit = !field.field.empty();
      for (char ch : field.field) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
          all_digit = false;
          break;
        }
      }
      if (all_digit) {
        out = ty::Ty{ty::TyInt{32, true}};
        return true;
      }
    }
    error = "unknown field access '" + field.field + "'";
    return false;
  }
  if (std::holds_alternative<HirIndex>(expr->node)) {
    const HirIndex& index = std::get<HirIndex>(expr->node);
    ty::Ty base_ty = make_ty_var_type(unifier.fresh().id);
    ty::Ty idx_ty = make_ty_var_type(unifier.fresh().id);
    if (!infer_expression_ty_impl(index.base, env, unifier, base_ty, error)) {
      return false;
    }
    if (!infer_expression_ty_impl(index.index, env, unifier, idx_ty, error)) {
      return false;
    }
    const TypeKind base_kind = coarse_type_kind_from_ty(unifier.apply(base_ty));
    const TypeKind idx_kind = coarse_type_kind_from_ty(unifier.apply(idx_ty));
    if (base_kind != TypeKind::ArrayType) {
      error = "index access requires array value";
      return false;
    }
    if (!is_numeric_type(idx_kind) && idx_kind != TypeKind::Bool) {
      error = "array index must be numeric";
      return false;
    }
    const ty::Ty reduced_base = unifier.apply(base_ty);
    if (const auto* named = std::get_if<ty::TyNamed>(&reduced_base.data);
        named != nullptr && named->name == "array" && !named->args.empty() && named->args[0]) {
      out = unifier.apply(*named->args[0]);
      return true;
    }
    out = ty::Ty{ty::TyInt{32, true}};
    return true;
  }
  if (std::holds_alternative<HirTuple>(expr->node)) {
    const HirTuple& tuple = std::get<HirTuple>(expr->node);
    std::vector<ty::Ty> items;
    items.reserve(tuple.items.size());
    for (const auto& item : tuple.items) {
      ty::Ty item_ty = make_ty_var_type(unifier.fresh().id);
      if (!infer_expression_ty_impl(item, env, unifier, item_ty, error)) {
        return false;
      }
      items.push_back(unifier.apply(item_ty));
    }
    out = make_named_ty("tuple", items);
    return true;
  }
  if (std::holds_alternative<HirArray>(expr->node)) {
    const HirArray& array = std::get<HirArray>(expr->node);
    const ty::Ty elem_var = make_ty_var_type(unifier.fresh().id);
    for (const auto& item : array.items) {
      ty::Ty item_ty = make_ty_var_type(unifier.fresh().id);
      if (!infer_expression_ty_impl(item, env, unifier, item_ty, error)) {
        return false;
      }
      if (!unifier.unify(elem_var, item_ty, error)) {
        error = "array literal requires elements of same type";
        return false;
      }
    }
    out = make_named_ty("array", {unifier.apply(elem_var)});
    return true;
  }
  error = "unsupported expression node";
  return false;
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

bool infer_expression_ty(const HirExprPtr& expr, const TypeEnv& env, ty::Ty& out, std::string& error) {
  error.clear();
  Unifier unifier;
  ty::Ty inferred = make_ty_var_type(unifier.fresh().id);
  if (!infer_expression_ty_impl(expr, env, unifier, inferred, error)) {
    return false;
  }
  out = unifier.apply(inferred);
  if (expr) {
    expr->inferred_type = out;
  }
  return true;
}

bool check_expression_ty(const HirExprPtr& expr, const ty::Ty& expected, const TypeEnv& env, std::string& error) {
  error.clear();
  Unifier unifier;
  ty::Ty inferred = make_ty_var_type(unifier.fresh().id);
  if (!infer_expression_ty_impl(expr, env, unifier, inferred, error)) {
    return false;
  }
  if (!unifier.unify(inferred, expected, error)) {
    if (error.empty()) {
      error = "expected " + ty_name(expected) + " but inferred " + ty_name(unifier.apply(inferred));
    }
    return false;
  }
  if (expr) {
    expr->inferred_type = unifier.apply(inferred);
  }
  return true;
}

TypeKind infer_expression(const HirExprPtr& expr, const TypeEnv& env, std::string& error) {
  ty::Ty inferred_ty = make_ty_var_type(0);
  if (infer_expression_ty(expr, env, inferred_ty, error)) {
    const TypeKind inferred = coarse_type_kind_from_ty(inferred_ty);
    if (expr && inferred != TypeKind::Unknown) {
      expr->inferred_type = inferred_ty;
    }
    return inferred;
  }
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
    error = "expected " + type_kind_name(expected) + " but inferred " + type_kind_name(inferred);
    return false;
  }
  return true;
}

}  // namespace thagc::hir
