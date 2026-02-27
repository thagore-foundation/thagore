#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "thagc/frontend/ast.hpp"

namespace thagc::lowering {

enum class CoreStmtKind {
  Let,
  Assign,
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
  std::string target;
  bool has_expression = false;
  std::string expression;
};

struct CoreFunction {
  std::string name;
  std::vector<std::string> params;
  std::string return_type;
  int return_literal = 0;
  std::string return_expression;
  std::vector<CoreStmt> statements;
};

struct CoreProgram {
  std::string normalized_source;
  bool has_main = false;
  int main_return_literal = 0;
  std::string main_return_expression;
  std::vector<CoreStmt> main_statements;
  std::vector<CoreFunction> functions;
  std::unordered_map<std::string, int> enum_variant_tags;
  std::unordered_map<std::string, std::vector<std::string>> struct_fields;
  std::unordered_map<std::string, std::string> struct_field_types;
};

CoreProgram lower_to_core(const syntax::AstProgram& program);

}  // namespace thagc::lowering
