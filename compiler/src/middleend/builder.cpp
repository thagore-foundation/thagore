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

static bool should_track_known_value(const std::string& expr) {
  if (expr.empty()) {
    return false;
  }
  if (expr.find('(') != std::string::npos || expr.find(')') != std::string::npos || expr.find('.') != std::string::npos) {
    return false;
  }
  return true;
}

static CoreStmtKind map_stmt_kind(syntax::StatementKind kind) {
  switch (kind) {
    case syntax::StatementKind::Let:
      return CoreStmtKind::Let;
    case syntax::StatementKind::Assign:
      return CoreStmtKind::Assign;
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

static void append_core_statement(std::vector<CoreStmt>& out_statements, const syntax::AstStatement& st,
                                  const std::unordered_map<std::string, std::string>& known_values) {
  CoreStmt out;
  out.kind = map_stmt_kind(st.kind);
  out.indent = st.indent;
  out.text = st.text;
  out.target = st.target;
  out.has_expression = st.has_expression && st.expression_valid;
  if (out.has_expression) {
    out.expression = substitute_identifiers(st.expression_normalized, known_values);
  }
  out_statements.push_back(std::move(out));
}

CoreProgram lower_to_core(const syntax::AstProgram& program) {
  CoreProgram core;
  core.normalized_source = program.source;
  core.enum_variant_tags = program.enum_variant_tags;
  core.struct_fields = program.struct_fields;
  core.struct_field_types = program.struct_field_types;
  core.has_main = program.has_main || !program.top_level_statements.empty();
  core.main_return_literal = program.main_return_literal;

  auto lower_function = [&](const syntax::AstFunction& fn) {
    CoreFunction out;
    out.name = fn.name;
    out.params = fn.params;
    out.return_type = fn.return_type;
    std::unordered_map<std::string, std::string> known_values;
    for (const auto& st : fn.body) {
      append_core_statement(out.statements, st, known_values);
      if ((st.kind == syntax::StatementKind::Let || st.kind == syntax::StatementKind::Assign) && st.has_expression &&
          st.expression_valid) {
        const std::string name = st.kind == syntax::StatementKind::Let ? parse_let_name(st.text) : st.target;
        if (!name.empty() && name.find('.') == std::string::npos && should_track_known_value(st.expression_normalized)) {
          known_values[name] = st.expression_normalized;
        }
      }
      if (st.kind == syntax::StatementKind::Return && st.has_expression && st.expression_valid) {
        out.return_expression = substitute_identifiers(st.expression_normalized, known_values);
        int parsed = 0;
        if (parse_i32_literal(st.expression_normalized, parsed)) {
          out.return_literal = parsed;
        }
      }
    }
    if (out.return_expression.empty()) {
      out.return_expression = std::to_string(out.return_literal);
    }
    return out;
  };

  for (const auto& fn : program.functions) {
    core.functions.push_back(lower_function(fn));
  }

  for (const auto& fn : core.functions) {
    if (fn.name == "main") {
      core.main_statements = fn.statements;
      core.main_return_expression = fn.return_expression;
      core.main_return_literal = fn.return_literal;
      break;
    }
  }

  if (!program.has_main && !program.top_level_statements.empty()) {
    std::unordered_map<std::string, std::string> known_values;
    const syntax::AstStatement& last = program.top_level_statements.back();
    CoreFunction script_main;
    script_main.name = "main";
    script_main.return_type = "i32";
    for (const auto& st : program.top_level_statements) {
      append_core_statement(script_main.statements, st, known_values);
      if ((st.kind == syntax::StatementKind::Let || st.kind == syntax::StatementKind::Assign) && st.has_expression &&
          st.expression_valid) {
        const std::string name = st.kind == syntax::StatementKind::Let ? parse_let_name(st.text) : st.target;
        if (!name.empty() && name.find('.') == std::string::npos && should_track_known_value(st.expression_normalized)) {
          known_values[name] = st.expression_normalized;
        }
      }
    }
    int top_ret = 0;
    if (last.kind == syntax::StatementKind::Expr && last.has_expression && last.expression_valid) {
      script_main.return_expression = substitute_identifiers(last.expression_normalized, known_values);
      if (parse_i32_literal(last.expression_normalized, top_ret)) {
        script_main.return_literal = top_ret;
      }
    } else {
      script_main.return_literal = 0;
      script_main.return_expression = "0";
    }
    core.main_statements = script_main.statements;
    core.main_return_expression = script_main.return_expression;
    core.main_return_literal = script_main.return_literal;
    core.functions.push_back(std::move(script_main));
  }

  if (core.main_return_expression.empty()) {
    core.main_return_expression = std::to_string(core.main_return_literal);
  }
  return core;
}

}  // namespace thagc::lowering
