#pragma once

#include <string>
#include <vector>

namespace thagc::syntax {

enum class StatementKind {
  Return,
  Let,
  Expr,
};

struct AstStatement {
  StatementKind kind = StatementKind::Expr;
  std::string text;
  int line = 0;
};

struct AstFunction {
  std::string name;
  std::string return_type;
  int header_line = 0;
  std::vector<AstStatement> body;
};

struct AstProgram {
  std::vector<std::string> top_level_lines;
  std::vector<AstFunction> functions;
  bool has_main = false;
  int main_return_literal = 0;
  std::string source;
};

}  // namespace thagc::syntax
