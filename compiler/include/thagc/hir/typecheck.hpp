#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "thagc/frontend/ast.hpp"
#include "thagc/frontend/types.hpp"
#include "thagc/hir/expr.hpp"

namespace thagc::hir {

struct TypeEnv {
  const std::unordered_map<std::string, semantics::TypeKind>* scope = nullptr;
  const std::unordered_map<std::string, int>* enum_variants = nullptr;
  const std::unordered_map<std::string, std::string>* struct_bindings = nullptr;
  const std::unordered_map<std::string, std::vector<std::string>>* struct_fields = nullptr;
  const std::unordered_map<std::string, std::string>* struct_field_types = nullptr;
  const std::unordered_map<std::string, std::vector<std::string>>* struct_methods = nullptr;
  const std::unordered_map<std::string, semantics::TypeKind>* function_returns = nullptr;
  const std::unordered_map<std::string, std::size_t>* function_arity = nullptr;
  const std::unordered_set<std::string>* struct_names = nullptr;
};

HirExprPtr lower_ast_expr(const syntax::AstExprPtr& expr);

semantics::TypeKind infer_expression(const HirExprPtr& expr, const TypeEnv& env, std::string& error);

bool check_expression(const HirExprPtr& expr, semantics::TypeKind expected, const TypeEnv& env, std::string& error);

}  // namespace thagc::hir
