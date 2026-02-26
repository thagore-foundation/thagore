#include "thagc/middleend/core_ir.hpp"

#include <cctype>
#include <string>

namespace thagc::lowering {

static bool parse_i32_literal(const std::string& text, int& value) {
  if (text.empty()) return false;
  std::size_t i = 0;
  bool neg = false;
  if (text[i] == '-') {
    neg = true;
    ++i;
  }
  if (i >= text.size()) return false;
  for (std::size_t k = i; k < text.size(); ++k) {
    if (!std::isdigit(static_cast<unsigned char>(text[k]))) {
      return false;
    }
  }
  value = std::stoi(text.substr(i));
  if (neg) value = -value;
  return true;
}

CoreProgram lower_to_core(const syntax::AstProgram& program) {
  CoreProgram core;
  core.normalized_source = program.source;
  core.has_main = program.has_main || !program.top_level_statements.empty();
  core.main_return_literal = program.main_return_literal;
  if (!program.has_main && !program.top_level_statements.empty()) {
    const syntax::AstStatement& last = program.top_level_statements.back();
    int top_ret = 0;
    if (last.kind == syntax::StatementKind::Expr && last.has_expression && last.expression_valid &&
        parse_i32_literal(last.expression_normalized, top_ret)) {
      core.main_return_literal = top_ret;
    } else {
      core.main_return_literal = 0;
    }
  }
  return core;
}

}  // namespace thagc::lowering
