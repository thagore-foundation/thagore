#include "thagc/frontend/parser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace thagc::syntax {

struct SourceLine {
  int number = 0;
  int indent = 0;
  std::string clean;
};

enum class ExprTokenKind {
  Atom,
  Operator,
  LParen,
  RParen,
  Comma,
  End,
};

struct ExprToken {
  ExprTokenKind kind = ExprTokenKind::End;
  std::string text;
};

struct ParsedExpression {
  bool ok = false;
  std::string normalized;
  std::string error;
};

struct ExprCursor {
  std::vector<ExprToken> tokens;
  std::size_t index = 0;
  std::string error;
};

static std::string trim(const std::string& text) {
  std::size_t left = 0;
  while (left < text.size() && std::isspace(static_cast<unsigned char>(text[left]))) {
    ++left;
  }
  std::size_t right = text.size();
  while (right > left && std::isspace(static_cast<unsigned char>(text[right - 1]))) {
    --right;
  }
  return text.substr(left, right - left);
}

static bool starts_with(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

static bool ends_with(const std::string& text, const std::string& suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static int leading_indent(const std::string& line) {
  int indent = 0;
  for (char ch : line) {
    if (ch == ' ') {
      ++indent;
      continue;
    }
    if (ch == '\t') {
      indent += 2;
      continue;
    }
    break;
  }
  return indent;
}

static std::string strip_comments(const std::string& line) {
  std::size_t cut = line.size();
  const std::size_t hash = line.find('#');
  if (hash != std::string::npos) {
    cut = std::min(cut, hash);
  }
  const std::size_t slash = line.find("//");
  if (slash != std::string::npos) {
    cut = std::min(cut, slash);
  }
  return line.substr(0, cut);
}

static std::string function_name_from_header(const std::string& line) {
  const std::size_t func_pos = line.find("func ");
  if (func_pos == std::string::npos) {
    return "";
  }
  std::size_t start = func_pos + 5;
  while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) {
    ++start;
  }
  std::size_t end = start;
  while (end < line.size() && (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '_')) {
    ++end;
  }
  return line.substr(start, end - start);
}

static std::vector<std::string> function_params_from_header(const std::string& line) {
  std::vector<std::string> out;
  const std::size_t func_pos = line.find("func ");
  if (func_pos == std::string::npos) {
    return out;
  }
  const std::size_t lparen = line.find('(', func_pos + 5);
  const std::size_t rparen = line.find(')', lparen == std::string::npos ? 0 : lparen + 1);
  if (lparen == std::string::npos || rparen == std::string::npos || rparen < lparen) {
    return out;
  }
  const std::string param_block = line.substr(lparen + 1, rparen - lparen - 1);
  std::size_t i = 0;
  while (i < param_block.size()) {
    std::size_t comma = param_block.find(',', i);
    if (comma == std::string::npos) {
      comma = param_block.size();
    }
    std::string part = trim(param_block.substr(i, comma - i));
    if (!part.empty()) {
      const std::size_t colon = part.find(':');
      if (colon != std::string::npos) {
        part = trim(part.substr(0, colon));
      }
      if (!part.empty()) {
        out.push_back(part);
      }
    }
    i = comma + 1;
  }
  return out;
}

static std::string method_name_from_line(const std::string& line) {
  std::string clean = trim(line);
  if (starts_with(clean, "pub ")) {
    clean = trim(clean.substr(4));
  }
  if (!starts_with(clean, "func ")) {
    return "";
  }
  return function_name_from_header(clean);
}

static std::string function_return_type_from_header(const std::string& line) {
  const std::size_t arrow = line.find("->");
  if (arrow == std::string::npos) {
    return "";
  }
  std::size_t start = arrow + 2;
  while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) {
    ++start;
  }
  std::size_t end = line.rfind(':');
  if (end == std::string::npos || end <= start) {
    end = line.size();
  }
  return trim(line.substr(start, end - start));
}

static bool parse_impl_for_header(const std::string& line, std::string& trait_name, std::string& type_name) {
  std::string clean = trim(line);
  if (!starts_with(clean, "impl ") || !ends_with(clean, ":")) {
    return false;
  }
  clean = trim(clean.substr(5, clean.size() - 6));
  const std::size_t for_pos = clean.find(" for ");
  if (for_pos == std::string::npos) {
    return false;
  }
  trait_name = trim(clean.substr(0, for_pos));
  type_name = trim(clean.substr(for_pos + 5));
  return !trait_name.empty() && !type_name.empty();
}

static std::string enum_variant_name_from_line(const std::string& line) {
  std::string clean = trim(line);
  if (clean.empty() || clean == ":") {
    return "";
  }
  std::size_t end = 0;
  while (end < clean.size() &&
         (std::isalnum(static_cast<unsigned char>(clean[end])) || clean[end] == '_')) {
    ++end;
  }
  if (end == 0) {
    return "";
  }
  return clean.substr(0, end);
}

static std::string struct_name_from_header(const std::string& line) {
  if (!starts_with(line, "struct ") || !ends_with(line, ":")) {
    return "";
  }
  const std::string body = trim(line.substr(7, line.size() - 8));
  if (body.empty()) {
    return "";
  }
  std::size_t end = 0;
  while (end < body.size() && (std::isalnum(static_cast<unsigned char>(body[end])) || body[end] == '_')) {
    ++end;
  }
  if (end == 0) {
    return "";
  }
  return body.substr(0, end);
}

static std::string struct_field_name_from_line(const std::string& line) {
  auto is_ident_start = [](char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
  };
  auto is_ident_body_local = [](char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
  };
  std::string clean = trim(line);
  if (clean.empty()) {
    return "";
  }
  const std::size_t colon = clean.find(':');
  if (colon != std::string::npos) {
    clean = trim(clean.substr(0, colon));
  }
  if (clean.empty() || !is_ident_start(clean[0])) {
    return "";
  }
  for (std::size_t i = 1; i < clean.size(); ++i) {
    if (!is_ident_body_local(clean[i])) {
      return "";
    }
  }
  return clean;
}

static std::string struct_field_type_from_line(const std::string& line) {
  const std::size_t colon = line.find(':');
  if (colon == std::string::npos) {
    return "i32";
  }
  const std::string ty = trim(line.substr(colon + 1));
  return ty.empty() ? "i32" : ty;
}

static void add_parse_error(AstProgram& program, int line, const std::string& message) {
  program.parse_errors.push_back("line " + std::to_string(line) + ": " + message);
}

static bool valid_control_header(const std::string& keyword, const std::string& line) {
  if (!starts_with(line, keyword + " ")) {
    return false;
  }
  if (!ends_with(line, ":")) {
    return false;
  }
  const std::string head = trim(line.substr(0, line.size() - 1));
  const std::size_t lparen = head.find('(');
  const std::size_t rparen = head.rfind(')');
  if (lparen == std::string::npos || rparen == std::string::npos || lparen > rparen) {
    return false;
  }
  return true;
}

static bool valid_for_header(const std::string& line) {
  if (!starts_with(line, "for ") || !ends_with(line, ":")) {
    return false;
  }
  const std::string head = trim(line.substr(0, line.size() - 1));
  const std::size_t in_pos = head.find(" in ");
  return in_pos != std::string::npos && in_pos > 4 && in_pos + 4 < head.size();
}

static std::string let_binding_name_from_line(const std::string& line) {
  if (!starts_with(line, "let ")) {
    return "";
  }
  auto is_ident_start = [](char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
  };
  auto is_ident_body_local = [](char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
  };
  std::size_t i = 4;
  while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
    ++i;
  }
  if (i >= line.size() || !is_ident_start(line[i])) {
    return "";
  }
  const std::size_t start = i;
  while (i < line.size() && is_ident_body_local(line[i])) {
    ++i;
  }
  return line.substr(start, i - start);
}

static bool is_simple_assignable_target(const std::string& text) {
  auto is_ident_start = [](char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
  };
  auto is_ident_body_local = [](char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
  };
  if (text.empty() || !is_ident_start(text[0])) {
    return false;
  }
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char ch = text[i];
    if (ch == '.') {
      if (i == 0 || i + 1 >= text.size() || !is_ident_start(text[i + 1])) {
        return false;
      }
      continue;
    }
    if (!is_ident_body_local(ch)) {
      return false;
    }
  }
  return true;
}

static bool is_assignment_line(const std::string& line) {
  if (starts_with(line, "let ") || starts_with(line, "if ") || starts_with(line, "while ") ||
      starts_with(line, "for ") || starts_with(line, "match ") || starts_with(line, "return ") ||
      trim(line) == "return") {
    return false;
  }
  const std::size_t eq = line.find('=');
  if (eq == std::string::npos) {
    return false;
  }
  if (eq + 1 < line.size() && line[eq + 1] == '=') {
    return false;
  }
  if (eq > 0) {
    const char prev = line[eq - 1];
    if (prev == '=' || prev == '!' || prev == '<' || prev == '>') {
      return false;
    }
  }
  const std::string lhs = trim(line.substr(0, eq));
  return is_simple_assignable_target(lhs);
}

static void collect_feature_counters(const std::string& line, AstProgram& program) {
  if (starts_with(line, "match ")) {
    ++program.match_count;
  }
  if (starts_with(line, "if ") && line.find(" if ") != std::string::npos) {
    ++program.if_expr_count;
  }
  if (line.find("enum ") != std::string::npos && line.find('(') != std::string::npos &&
      line.find(')') != std::string::npos) {
    ++program.enum_payload_count;
  }
  if (line.find(" |") != std::string::npos || line.find("| ") != std::string::npos) {
    ++program.closure_count;
  }
  if (starts_with(line, "for ") && line.find(" in ") != std::string::npos && line.find("..") != std::string::npos) {
    ++program.range_loop_count;
  }
  if (starts_with(line, "let (")) {
    ++program.tuple_destruct_count;
  }
  if (line.find('[') != std::string::npos && line.find(']') != std::string::npos &&
      line.find(',') != std::string::npos) {
    ++program.array_literal_count;
  }
  if (line.find('[') != std::string::npos && line.find("..") != std::string::npos &&
      line.find(']') != std::string::npos) {
    ++program.slice_expr_count;
  }
  if (ends_with(line, ":") && line.find(':') != std::string::npos && !starts_with(line, "if ") &&
      !starts_with(line, "while ") && !starts_with(line, "for ") && !starts_with(line, "func ") &&
      !starts_with(line, "struct ") && !starts_with(line, "enum ") && !starts_with(line, "impl ") &&
      !starts_with(line, "trait ") && !starts_with(line, "match ") && !starts_with(line, "else")) {
    ++program.loop_label_count;
  }
  if (starts_with(line, "unsafe ") || starts_with(line, "unsafe:")) {
    ++program.unsafe_count;
  }
  if (starts_with(line, "defer ")) {
    ++program.defer_scope_count;
  }
  if (starts_with(line, "comptime ") || starts_with(line, "comptime:")) {
    ++program.comptime_count;
  }
  if (starts_with(line, "impl ")) {
    ++program.extension_impl_count;
  }
  if (starts_with(line, "pub ")) {
    ++program.visibility_count;
  }
  if (line.find("r#\"") != std::string::npos) {
    ++program.raw_string_count;
  }
  if (line.find("v\"") != std::string::npos) {
    ++program.interpolated_string_count;
  }
  if (line.find('?') != std::string::npos) {
    ++program.result_sugar_count;
  }
}

static bool is_identifier_start(char ch) {
  return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}

static bool is_identifier_body(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

static std::vector<ExprToken> tokenize_expression(const std::string& text, std::string& error) {
  std::vector<ExprToken> out;
  for (std::size_t i = 0; i < text.size();) {
    const char ch = text[i];
    if (std::isspace(static_cast<unsigned char>(ch))) {
      ++i;
      continue;
    }
    if (i + 1 < text.size()) {
      const std::string two = text.substr(i, 2);
      if (two == "==" || two == "!=" || two == "<=" || two == ">=") {
        out.push_back(ExprToken{ExprTokenKind::Operator, two});
        i += 2;
        continue;
      }
    }
    if (ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '<' || ch == '>') {
      out.push_back(ExprToken{ExprTokenKind::Operator, std::string(1, ch)});
      ++i;
      continue;
    }
    if (ch == '(') {
      out.push_back(ExprToken{ExprTokenKind::LParen, "("});
      ++i;
      continue;
    }
    if (ch == ')') {
      out.push_back(ExprToken{ExprTokenKind::RParen, ")"});
      ++i;
      continue;
    }
    if (ch == ',') {
      out.push_back(ExprToken{ExprTokenKind::Comma, ","});
      ++i;
      continue;
    }
    if (ch == '"') {
      std::size_t start = i++;
      while (i < text.size() && text[i] != '"') {
        if (text[i] == '\\' && i + 1 < text.size()) {
          i += 2;
          continue;
        }
        ++i;
      }
      if (i >= text.size() || text[i] != '"') {
        error = "unterminated string literal in expression";
        return {};
      }
      ++i;
      out.push_back(ExprToken{ExprTokenKind::Atom, text.substr(start, i - start)});
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(ch))) {
      std::size_t start = i;
      bool has_dot = false;
      while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
        ++i;
      }
      if (i < text.size() && text[i] == '.') {
        has_dot = true;
        ++i;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
          ++i;
        }
      }
      if (has_dot && i == start + 1) {
        error = "invalid numeric literal in expression";
        return {};
      }
      out.push_back(ExprToken{ExprTokenKind::Atom, text.substr(start, i - start)});
      continue;
    }
    if (is_identifier_start(ch)) {
      std::size_t start = i;
      while (i < text.size()) {
        if (is_identifier_body(text[i])) {
          ++i;
          continue;
        }
        if (text[i] == '.' && i + 1 < text.size() && is_identifier_start(text[i + 1])) {
          ++i;
          continue;
        }
        break;
      }
      out.push_back(ExprToken{ExprTokenKind::Atom, text.substr(start, i - start)});
      continue;
    }
    error = std::string("invalid expression token: '") + ch + "'";
    return {};
  }
  out.push_back(ExprToken{ExprTokenKind::End, ""});
  return out;
}

static const ExprToken& current_token(const ExprCursor& cursor) {
  if (cursor.index >= cursor.tokens.size()) {
    static const ExprToken kEnd{ExprTokenKind::End, ""};
    return kEnd;
  }
  return cursor.tokens[cursor.index];
}

static std::string parse_expression_equality(ExprCursor& cursor);

static std::string parse_primary(ExprCursor& cursor) {
  const ExprToken& tok = current_token(cursor);
  if (tok.kind == ExprTokenKind::Operator && tok.text == "-") {
    ++cursor.index;
    const std::string rhs = parse_primary(cursor);
    if (!cursor.error.empty()) {
      return "";
    }
    return "(0 - " + rhs + ")";
  }
  if (tok.kind == ExprTokenKind::Atom) {
    ++cursor.index;
    std::string atom = tok.text;
    if (current_token(cursor).kind != ExprTokenKind::LParen) {
      return atom;
    }

    ++cursor.index;  // '('
    std::vector<std::string> args;
    if (current_token(cursor).kind != ExprTokenKind::RParen) {
      while (true) {
        const std::string arg = parse_expression_equality(cursor);
        if (!cursor.error.empty()) {
          return "";
        }
        args.push_back(arg);
        if (current_token(cursor).kind == ExprTokenKind::Comma) {
          ++cursor.index;
          continue;
        }
        break;
      }
    }
    if (current_token(cursor).kind != ExprTokenKind::RParen) {
      cursor.error = "missing closing ')' in call expression";
      return "";
    }
    ++cursor.index;
    std::string out = atom + "(";
    for (std::size_t i = 0; i < args.size(); ++i) {
      if (i > 0) {
        out += ", ";
      }
      out += args[i];
    }
    out += ")";
    return out;
  }
  if (tok.kind == ExprTokenKind::LParen) {
    ++cursor.index;
    const std::string expr = parse_expression_equality(cursor);
    if (!cursor.error.empty()) {
      return "";
    }
    if (current_token(cursor).kind != ExprTokenKind::RParen) {
      cursor.error = "missing closing ')' in expression";
      return "";
    }
    ++cursor.index;
    return "(" + expr + ")";
  }
  cursor.error = "expected expression atom";
  return "";
}

static std::string parse_multiplicative(ExprCursor& cursor) {
  std::string lhs = parse_primary(cursor);
  if (!cursor.error.empty()) {
    return "";
  }
  while (true) {
    const ExprToken& tok = current_token(cursor);
    if (tok.kind != ExprTokenKind::Operator || (tok.text != "*" && tok.text != "/")) {
      break;
    }
    const std::string op = tok.text;
    ++cursor.index;
    const std::string rhs = parse_primary(cursor);
    if (!cursor.error.empty()) {
      return "";
    }
    lhs = "(" + lhs + " " + op + " " + rhs + ")";
  }
  return lhs;
}

static std::string parse_additive(ExprCursor& cursor) {
  std::string lhs = parse_multiplicative(cursor);
  if (!cursor.error.empty()) {
    return "";
  }
  while (true) {
    const ExprToken& tok = current_token(cursor);
    if (tok.kind != ExprTokenKind::Operator || (tok.text != "+" && tok.text != "-")) {
      break;
    }
    const std::string op = tok.text;
    ++cursor.index;
    const std::string rhs = parse_multiplicative(cursor);
    if (!cursor.error.empty()) {
      return "";
    }
    lhs = "(" + lhs + " " + op + " " + rhs + ")";
  }
  return lhs;
}

static std::string parse_comparison(ExprCursor& cursor) {
  std::string lhs = parse_additive(cursor);
  if (!cursor.error.empty()) {
    return "";
  }
  while (true) {
    const ExprToken& tok = current_token(cursor);
    const bool is_cmp = tok.kind == ExprTokenKind::Operator &&
                        (tok.text == "<" || tok.text == "<=" || tok.text == ">" || tok.text == ">=");
    if (!is_cmp) {
      break;
    }
    const std::string op = tok.text;
    ++cursor.index;
    const std::string rhs = parse_additive(cursor);
    if (!cursor.error.empty()) {
      return "";
    }
    lhs = "(" + lhs + " " + op + " " + rhs + ")";
  }
  return lhs;
}

static std::string parse_expression_equality(ExprCursor& cursor) {
  std::string lhs = parse_comparison(cursor);
  if (!cursor.error.empty()) {
    return "";
  }
  while (true) {
    const ExprToken& tok = current_token(cursor);
    const bool is_eq = tok.kind == ExprTokenKind::Operator && (tok.text == "==" || tok.text == "!=");
    if (!is_eq) {
      break;
    }
    const std::string op = tok.text;
    ++cursor.index;
    const std::string rhs = parse_comparison(cursor);
    if (!cursor.error.empty()) {
      return "";
    }
    lhs = "(" + lhs + " " + op + " " + rhs + ")";
  }
  return lhs;
}

static ParsedExpression parse_expression_text(const std::string& expr_text) {
  ParsedExpression out;
  const std::string clean = trim(expr_text);
  if (clean.empty()) {
    out.error = "expression is empty";
    return out;
  }

  std::string token_error;
  ExprCursor cursor;
  cursor.tokens = tokenize_expression(clean, token_error);
  if (!token_error.empty()) {
    out.error = token_error;
    return out;
  }

  out.normalized = parse_expression_equality(cursor);
  if (!cursor.error.empty()) {
    out.error = cursor.error;
    return out;
  }
  if (current_token(cursor).kind != ExprTokenKind::End) {
    out.error = "unexpected token near '" + current_token(cursor).text + "'";
    return out;
  }
  out.ok = true;
  return out;
}

static std::string expression_from_let(const std::string& line) {
  const std::size_t equal = line.find('=');
  if (equal == std::string::npos) {
    return "";
  }
  return trim(line.substr(equal + 1));
}

static std::string expression_from_assign(const std::string& line) {
  const std::size_t equal = line.find('=');
  if (equal == std::string::npos) {
    return "";
  }
  return trim(line.substr(equal + 1));
}

static std::string assignment_target_from_line(const std::string& line) {
  const std::size_t equal = line.find('=');
  if (equal == std::string::npos) {
    return "";
  }
  return trim(line.substr(0, equal));
}

static std::string expression_from_return(const std::string& line) {
  if (trim(line) == "return") {
    return "";
  }
  if (!starts_with(line, "return ")) {
    return "";
  }
  return trim(line.substr(7));
}

static std::string expression_from_control(const std::string& line) {
  const std::size_t lparen = line.find('(');
  const std::size_t rparen = line.rfind(')');
  if (lparen == std::string::npos || rparen == std::string::npos || lparen >= rparen) {
    return "";
  }
  return trim(line.substr(lparen + 1, rparen - lparen - 1));
}

static std::string expression_after_keyword_colon(const std::string& line, const std::string& keyword) {
  if (!starts_with(line, keyword + " ") || !ends_with(line, ":")) {
    return "";
  }
  return trim(line.substr(keyword.size(), line.size() - keyword.size() - 1));
}

static std::string expression_from_for(const std::string& line) {
  if (!starts_with(line, "for ") || !ends_with(line, ":")) {
    return "";
  }
  const std::string body = trim(line.substr(4, line.size() - 5));
  const std::size_t in_pos = body.find(" in ");
  if (in_pos == std::string::npos) {
    return "";
  }
  return trim(body.substr(in_pos + 4));
}

static void parse_statement_expression(AstProgram& program, AstStatement& st) {
  std::string expr_text;
  if (st.kind == StatementKind::Let) {
    st.target = let_binding_name_from_line(st.text);
    expr_text = expression_from_let(st.text);
    if (expr_text.empty()) {
      st.has_expression = true;
      st.expression_valid = false;
      st.expression_error = "let statement requires assignment expression";
      add_parse_error(program, st.line, st.expression_error);
      return;
    }
  } else if (st.kind == StatementKind::Assign) {
    st.target = assignment_target_from_line(st.text);
    expr_text = expression_from_assign(st.text);
    if (st.target.empty() || !is_simple_assignable_target(st.target) || expr_text.empty()) {
      st.has_expression = true;
      st.expression_valid = false;
      st.expression_error = "assignment statement requires '<target> = <expr>'";
      add_parse_error(program, st.line, st.expression_error);
      return;
    }
  } else if (st.kind == StatementKind::Return) {
    expr_text = expression_from_return(st.text);
  } else if (st.kind == StatementKind::If || st.kind == StatementKind::While) {
    expr_text = expression_from_control(st.text);
    if (expr_text.empty()) {
      st.has_expression = true;
      st.expression_valid = false;
      st.expression_error = "control condition expression is invalid";
      add_parse_error(program, st.line, st.expression_error);
      return;
    }
  } else if (st.kind == StatementKind::For) {
    expr_text = expression_from_for(st.text);
    if (expr_text.empty()) {
      st.has_expression = true;
      st.expression_valid = false;
      st.expression_error = "for statement requires range expression";
      add_parse_error(program, st.line, st.expression_error);
      return;
    }
    st.has_expression = true;
    st.expression_valid = expr_text.find("..") != std::string::npos;
    st.expression_normalized = expr_text;
    if (!st.expression_valid) {
      st.expression_error = "for statement must use '..' range syntax";
      add_parse_error(program, st.line, st.expression_error);
    }
    return;
  } else if (st.kind == StatementKind::Match) {
    expr_text = expression_after_keyword_colon(st.text, "match");
  } else if (st.kind == StatementKind::Else) {
    return;
  } else {
    const std::string line = trim(st.text);
    if (ends_with(line, ":")) {
      return;
    }
    if (starts_with(line, "print(") && ends_with(line, ")")) {
      expr_text = trim(line.substr(6, line.size() - 7));
    } else {
      expr_text = line;
    }
  }

  if (expr_text.empty()) {
    return;
  }

  st.has_expression = true;
  const ParsedExpression parsed = parse_expression_text(expr_text);
  st.expression_valid = parsed.ok;
  st.expression_normalized = parsed.normalized;
  st.expression_error = parsed.error;
  if (!parsed.ok) {
    add_parse_error(program, st.line, "invalid expression: " + parsed.error);
  }
}

static AstStatement build_statement_from_line(AstProgram& program, const SourceLine& body) {
  AstStatement st;
  st.text = body.clean;
  st.line = body.number;
  st.indent = body.indent;
  if (starts_with(body.clean, "if ")) {
    st.kind = StatementKind::If;
    if (!valid_control_header("if", body.clean)) {
      add_parse_error(program, body.number, "if requires parentheses and trailing ':'");
    }
  } else if (starts_with(body.clean, "else")) {
    st.kind = StatementKind::Else;
    if (trim(body.clean) != "else:") {
      add_parse_error(program, body.number, "else must be written as 'else:'");
    }
  } else if (starts_with(body.clean, "while ")) {
    st.kind = StatementKind::While;
    if (!valid_control_header("while", body.clean)) {
      add_parse_error(program, body.number, "while requires parentheses and trailing ':'");
    }
  } else if (starts_with(body.clean, "for ")) {
    st.kind = StatementKind::For;
    if (!valid_for_header(body.clean)) {
      add_parse_error(program, body.number, "for header must follow 'for <name> in <expr>:'");
    }
  } else if (starts_with(body.clean, "match ")) {
    st.kind = StatementKind::Match;
    if (!ends_with(body.clean, ":")) {
      add_parse_error(program, body.number, "match header must be colon-terminated");
    }
  } else if (starts_with(body.clean, "return ") || trim(body.clean) == "return") {
    st.kind = StatementKind::Return;
  } else if (starts_with(body.clean, "let ")) {
    st.kind = StatementKind::Let;
  } else if (is_assignment_line(body.clean)) {
    st.kind = StatementKind::Assign;
  } else {
    st.kind = StatementKind::Expr;
  }
  parse_statement_expression(program, st);
  return st;
}

static int parse_return_literal(const std::string& line) {
  const std::string expr = expression_from_return(line);
  const ParsedExpression parsed = parse_expression_text(expr);
  if (!parsed.ok) {
    return 0;
  }
  const std::string normalized = trim(parsed.normalized);
  if (normalized.empty()) {
    return 0;
  }
  std::size_t index = 0;
  bool neg = false;
  if (normalized[index] == '-') {
    neg = true;
    ++index;
  }
  if (index >= normalized.size()) {
    return 0;
  }
  for (std::size_t i = index; i < normalized.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(normalized[i]))) {
      return 0;
    }
  }
  const int value = std::stoi(normalized.substr(index));
  return neg ? -value : value;
}

AstProgram Parser::parse(const std::vector<Token>& tokens, const std::string& source) const {
  (void)tokens;
  AstProgram program;
  program.source = source;

  std::vector<SourceLine> lines;
  std::istringstream in(source);
  std::string raw_line;
  int line_no = 0;
  while (std::getline(in, raw_line)) {
    ++line_no;
    const std::string stripped = strip_comments(raw_line);
    const std::string clean = trim(stripped);
    if (clean.empty()) {
      continue;
    }
    program.top_level_lines.push_back(clean);
    lines.push_back(SourceLine{line_no, leading_indent(raw_line), clean});
  }

  std::size_t i = 0;
  while (i < lines.size()) {
    const SourceLine& line = lines[i];
    collect_feature_counters(line.clean, program);
    std::string effective_line = line.clean;
    if (starts_with(effective_line, "pub ")) {
      program.public_decls.push_back(line.clean);
      effective_line = trim(effective_line.substr(4));
    }

    if (starts_with(effective_line, "func ")) {
      AstFunction fn;
      fn.name = function_name_from_header(effective_line);
      fn.params = function_params_from_header(effective_line);
      fn.header_line = line.number;
      fn.header_indent = line.indent;

      if (!ends_with(effective_line, ":")) {
        add_parse_error(program, line.number, "function header must be colon-terminated");
      }
      if (fn.name.empty()) {
        add_parse_error(program, line.number, "invalid function header");
      }
      for (const std::string& param : fn.params) {
        if (param.empty() || !is_simple_assignable_target(param) || param.find('.') != std::string::npos) {
          add_parse_error(program, line.number, "invalid function parameter '" + param + "'");
        }
      }
      fn.return_type = function_return_type_from_header(effective_line);
      if (effective_line.find("->") != std::string::npos && fn.return_type.empty()) {
        add_parse_error(program, line.number, "function return annotation '-> type' is not supported");
      }

      if (fn.name == "main") {
        program.has_main = true;
      }

      ++i;
      if (i >= lines.size() || lines[i].indent <= fn.header_indent) {
        add_parse_error(program, line.number, "function body must be indentation-scoped");
      }

      while (i < lines.size() && lines[i].indent > fn.header_indent) {
        const SourceLine& body = lines[i];
        AstStatement st = build_statement_from_line(program, body);
        if (st.kind == StatementKind::Return && fn.name == "main") {
          program.main_return_literal = parse_return_literal(body.clean);
        }
        fn.body.push_back(st);
        ++i;
      }

      program.functions.push_back(std::move(fn));
      continue;
    }

    if (starts_with(effective_line, "import ")) {
      program.imports.push_back(effective_line);
      ++i;
      continue;
    }
    if (starts_with(effective_line, "extern ")) {
      program.extern_decls.push_back(effective_line);
      ++i;
      continue;
    }
    if (starts_with(effective_line, "struct ")) {
      if (!ends_with(effective_line, ":")) {
        add_parse_error(program, line.number, "struct header must be colon-terminated");
      }
      program.structs.push_back(effective_line);
      const std::string struct_name = struct_name_from_header(effective_line);
      if (struct_name.empty()) {
        add_parse_error(program, line.number, "invalid struct header");
      }
      ++i;
      while (i < lines.size() && lines[i].indent > line.indent) {
        collect_feature_counters(lines[i].clean, program);
        if (!struct_name.empty()) {
          const std::string field_name = struct_field_name_from_line(lines[i].clean);
          if (field_name.empty()) {
            add_parse_error(program, lines[i].number,
                            "invalid struct field declaration: '" + lines[i].clean + "'");
          } else {
            program.struct_fields[struct_name].push_back(field_name);
            program.struct_field_types[struct_name + "." + field_name] =
                struct_field_type_from_line(lines[i].clean);
          }
        }
        ++i;
      }
      continue;
    }
    if (starts_with(effective_line, "enum ")) {
      if (!ends_with(effective_line, ":")) {
        add_parse_error(program, line.number, "enum header must be colon-terminated");
      }
      program.enums.push_back(effective_line);
      int variant_index = 0;
      ++i;
      while (i < lines.size() && lines[i].indent > line.indent) {
        collect_feature_counters(lines[i].clean, program);
        const std::string variant = enum_variant_name_from_line(lines[i].clean);
        if (!variant.empty() && program.enum_variant_tags.find(variant) == program.enum_variant_tags.end()) {
          program.enum_variant_tags[variant] = variant_index++;
        }
        ++i;
      }
      continue;
    }
    if (starts_with(effective_line, "type ")) {
      program.type_aliases.push_back(effective_line);
      ++i;
      continue;
    }
    if (starts_with(effective_line, "trait ")) {
      if (!ends_with(effective_line, ":")) {
        add_parse_error(program, line.number, "trait header must be colon-terminated");
      }
      program.traits.push_back(effective_line);
      const std::string trait_name = trim(effective_line.substr(6, effective_line.size() - 7));
      ++i;
      while (i < lines.size() && lines[i].indent > line.indent) {
        collect_feature_counters(lines[i].clean, program);
        const std::string method = method_name_from_line(lines[i].clean);
        if (!method.empty()) {
          program.trait_required_methods[trait_name].push_back(method);
        }
        ++i;
      }
      continue;
    }
    if (starts_with(effective_line, "impl ")) {
      if (!ends_with(effective_line, ":")) {
        add_parse_error(program, line.number, "impl header must be colon-terminated");
      }
      program.impls.push_back(effective_line);
      std::string trait_name;
      std::string type_name;
      const bool is_impl_for = parse_impl_for_header(effective_line, trait_name, type_name);
      const std::string impl_key = trait_name + "|" + type_name;
      if (is_impl_for) {
        program.impl_for_headers.push_back(effective_line);
      }
      ++i;
      while (i < lines.size() && lines[i].indent > line.indent) {
        collect_feature_counters(lines[i].clean, program);
        if (is_impl_for) {
          const std::string method = method_name_from_line(lines[i].clean);
          if (!method.empty()) {
            program.impl_for_methods[impl_key].push_back(method);
          }
        }
        ++i;
      }
      continue;
    }

    if (line.indent != 0) {
      add_parse_error(program, line.number, "top-level executable statements must not be indented");
      ++i;
      continue;
    }

    AstStatement top = build_statement_from_line(program, line);
    if (top.kind == StatementKind::Return) {
      add_parse_error(program, line.number, "top-level return is not allowed");
    }
    program.top_level_statements.push_back(top);

    ++i;
  }

  return program;
}

}  // namespace thagc::syntax
