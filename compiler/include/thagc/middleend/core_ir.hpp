#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "thagc/frontend/ast.hpp"

namespace thagc::lowering {

enum class CoreStmtKind {
  Let,
  Expr,
  Return,
  If,
  Else,
  While,
  For,
  Match,
};

struct CoreStmt {
  CoreStmtKind kind = CoreStmtKind::Expr;
  int indent = 0;
  std::string text;
  bool has_expression = false;
  std::string expression;
};

struct CoreProgram {
  std::string normalized_source;
  bool has_main = false;
  int main_return_literal = 0;
  std::string main_return_expression;
  std::vector<CoreStmt> main_statements;
  std::unordered_map<std::string, int> enum_variant_tags;
};

CoreProgram lower_to_core(const syntax::AstProgram& program);

}  // namespace thagc::lowering
