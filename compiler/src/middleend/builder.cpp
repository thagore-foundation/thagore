#include "thagc/middleend/core_ir.hpp"

#include <cctype>
#include <string_view>
#include <string>
#include <unordered_map>

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

static bool is_ident_start(char ch) {
  return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}

static bool is_ident_body(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

static std::string parse_let_name(const std::string& line) {
  constexpr std::string_view kLet = "let ";
  if (line.size() < kLet.size() || line.compare(0, kLet.size(), kLet) != 0) {
    return "";
  }
  std::size_t i = kLet.size();
  while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
    ++i;
  }
  if (i >= line.size() || !is_ident_start(line[i])) {
    return "";
  }
  const std::size_t start = i;
  while (i < line.size() && is_ident_body(line[i])) {
    ++i;
  }
  return line.substr(start, i - start);
}

static std::string substitute_identifiers(
    const std::string& expr, const std::unordered_map<std::string, std::string>& known_values) {
  std::string out;
  for (std::size_t i = 0; i < expr.size();) {
    const char ch = expr[i];
    if (!is_ident_start(ch)) {
      out.push_back(ch);
      ++i;
      continue;
    }
    const std::size_t start = i;
    while (i < expr.size() && is_ident_body(expr[i])) {
      ++i;
    }
    const std::string ident = expr.substr(start, i - start);
    const auto it = known_values.find(ident);
    if (it != known_values.end()) {
      out += "(" + it->second + ")";
    } else {
      out += ident;
    }
  }
  return out;
}

static CoreStmtKind map_stmt_kind(syntax::StatementKind kind) {
  switch (kind) {
    case syntax::StatementKind::Let:
      return CoreStmtKind::Let;
    case syntax::StatementKind::Return:
      return CoreStmtKind::Return;
    case syntax::StatementKind::If:
      return CoreStmtKind::If;
    case syntax::StatementKind::Else:
      return CoreStmtKind::Else;
    case syntax::StatementKind::While:
      return CoreStmtKind::While;
    case syntax::StatementKind::For:
      return CoreStmtKind::For;
    case syntax::StatementKind::Match:
      return CoreStmtKind::Match;
    case syntax::StatementKind::Expr:
    default:
      return CoreStmtKind::Expr;
  }
}

static void append_core_statement(CoreProgram& core, const syntax::AstStatement& st,
                                  const std::unordered_map<std::string, std::string>& known_values) {
  CoreStmt out;
  out.kind = map_stmt_kind(st.kind);
  out.indent = st.indent;
  out.text = st.text;
  out.has_expression = st.has_expression && st.expression_valid;
  if (out.has_expression) {
    out.expression = substitute_identifiers(st.expression_normalized, known_values);
  }
  core.main_statements.push_back(std::move(out));
}

CoreProgram lower_to_core(const syntax::AstProgram& program) {
  CoreProgram core;
  core.normalized_source = program.source;
  core.enum_variant_tags = program.enum_variant_tags;
  core.has_main = program.has_main || !program.top_level_statements.empty();
  core.main_return_literal = program.main_return_literal;

  std::unordered_map<std::string, std::string> known_values;
  if (program.has_main) {
    for (const auto& fn : program.functions) {
      if (fn.name != "main") {
        continue;
      }
      for (const auto& st : fn.body) {
        append_core_statement(core, st, known_values);
        if (st.kind == syntax::StatementKind::Let && st.has_expression && st.expression_valid) {
          const std::string name = parse_let_name(st.text);
          if (!name.empty()) {
            known_values[name] = st.expression_normalized;
          }
        }
        if (st.kind == syntax::StatementKind::Return && st.has_expression && st.expression_valid) {
          core.main_return_expression = substitute_identifiers(st.expression_normalized, known_values);
        }
      }
    }
  }

  if (core.main_return_expression.empty()) {
    core.main_return_expression = std::to_string(core.main_return_literal);
  }

  if (!program.has_main && !program.top_level_statements.empty()) {
    known_values.clear();
    const syntax::AstStatement& last = program.top_level_statements.back();
    for (const auto& st : program.top_level_statements) {
      append_core_statement(core, st, known_values);
      if (st.kind == syntax::StatementKind::Let && st.has_expression && st.expression_valid) {
        const std::string name = parse_let_name(st.text);
        if (!name.empty()) {
          known_values[name] = st.expression_normalized;
        }
      }
    }
    int top_ret = 0;
    if (last.kind == syntax::StatementKind::Expr && last.has_expression && last.expression_valid) {
      core.main_return_expression = substitute_identifiers(last.expression_normalized, known_values);
      if (parse_i32_literal(last.expression_normalized, top_ret)) {
        core.main_return_literal = top_ret;
      } else {
        core.main_return_literal = 0;
      }
    } else {
      core.main_return_literal = 0;
      core.main_return_expression = "0";
    }
  }
  return core;
}

}  // namespace thagc::lowering
