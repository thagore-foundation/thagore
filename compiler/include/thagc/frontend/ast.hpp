#pragma once

#include <string>
#include <vector>

namespace thagc::syntax {

enum class StatementKind {
  Return,
  Let,
  If,
  While,
  Expr,
};

struct AstStatement {
  StatementKind kind = StatementKind::Expr;
  std::string text;
  int line = 0;
  bool has_expression = false;
  bool expression_valid = true;
  std::string expression_normalized;
  std::string expression_error;
};

struct AstFunction {
  std::string name;
  std::string return_type;
  int header_line = 0;
  int header_indent = 0;
  std::vector<AstStatement> body;
};

struct AstProgram {
  std::vector<std::string> top_level_lines;
  std::vector<AstFunction> functions;
  std::vector<std::string> imports;
  std::vector<std::string> extern_decls;
  std::vector<std::string> structs;
  std::vector<std::string> impls;
  std::vector<std::string> parse_errors;
  bool has_main = false;
  int main_return_literal = 0;
  std::string source;
};

}  // namespace thagc::syntax
