#include "thagc/frontend/parser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_set>
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

struct ParsedFunctionParam {
  std::string name;
  std::string type;
};

static std::vector<ParsedFunctionParam> function_param_specs_from_header(const std::string& line) {
  std::vector<ParsedFunctionParam> out;
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
      ParsedFunctionParam parsed;
      if (colon == std::string::npos) {
        parsed.name = trim(part);
      } else {
        parsed.name = trim(part.substr(0, colon));
        parsed.type = trim(part.substr(colon + 1));
      }
      if (!parsed.name.empty()) {
        out.push_back(std::move(parsed));
      }
    }
    i = comma + 1;
  }
  return out;
}

static std::vector<std::string> function_params_from_header(const std::string& line) {
  std::vector<std::string> out;
  for (const auto& spec : function_param_specs_from_header(line)) {
    out.push_back(spec.name);
  }
  return out;
}

static std::vector<std::string> function_param_types_from_header(const std::string& line) {
  std::vector<std::string> out;
  for (const auto& spec : function_param_specs_from_header(line)) {
    out.push_back(spec.type);
  }
  return out;
}

static std::string method_name_from_line(const std::string& line) {
  std::string clean = trim(line);
  if (starts_with(clean, "pub ")) {
    clean = trim(clean.substr(4));
  }
  if (starts_with(clean, "async ")) {
    clean = trim(clean.substr(6));
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

static bool parse_impl_type_header(const std::string& line, std::string& type_name) {
  std::string clean = trim(line);
  if (!starts_with(clean, "impl ") || !ends_with(clean, ":")) {
    return false;
  }
  clean = trim(clean.substr(5, clean.size() - 6));
  if (clean.empty() || clean.find(" for ") != std::string::npos) {
    return false;
  }
  type_name = clean;
  return true;
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

static std::string enum_variant_payload_type_from_line(const std::string& line) {
  const std::string clean = trim(line);
  const std::size_t lparen = clean.find('(');
  const std::size_t rparen = clean.rfind(')');
  if (lparen == std::string::npos || rparen == std::string::npos || rparen <= lparen + 1) {
    return "";
  }
  std::string payload = trim(clean.substr(lparen + 1, rparen - lparen - 1));
  if (payload.empty()) {
    return "";
  }
  const std::size_t colon = payload.find(':');
  if (colon != std::string::npos) {
    payload = trim(payload.substr(colon + 1));
  }
  return payload;
}

static std::string enum_name_from_header(const std::string& line) {
  if (!starts_with(line, "enum ") || !ends_with(line, ":")) {
    return "";
  }
  const std::string body = trim(line.substr(5, line.size() - 6));
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

static bool parse_state_header(const std::string& line, std::string& state_name, std::vector<std::string>& variants,
                               std::string& error) {
  auto is_identifier_start_local = [](char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
  };
  auto is_identifier_body_local = [](char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
  };
  state_name.clear();
  variants.clear();
  error.clear();
  if (!starts_with(line, "state ")) {
    return false;
  }
  const std::size_t colon = line.find(':');
  if (colon == std::string::npos || colon <= 6 || colon + 1 >= line.size()) {
    error = "state declaration must follow `state Name: Variant | Variant`";
    return true;
  }
  state_name = trim(line.substr(6, colon - 6));
  if (state_name.empty()) {
    error = "state declaration requires a state set name";
    return true;
  }
  if (!is_identifier_start_local(state_name[0])) {
    error = "state set name must be an identifier";
    return true;
  }
  for (std::size_t i = 1; i < state_name.size(); ++i) {
    if (!is_identifier_body_local(state_name[i])) {
      error = "state set name must be an identifier";
      return true;
    }
  }
  const std::string right = trim(line.substr(colon + 1));
  if (right.empty()) {
    error = "state declaration requires at least one variant";
    return true;
  }
  std::size_t i = 0;
  while (i < right.size()) {
    std::size_t bar = right.find('|', i);
    if (bar == std::string::npos) {
      bar = right.size();
    }
    const std::string variant = trim(right.substr(i, bar - i));
    if (variant.empty()) {
      error = "state declaration contains empty variant";
      return true;
    }
    if (!is_identifier_start_local(variant[0])) {
      error = "state variant '" + variant + "' is not an identifier";
      return true;
    }
    for (std::size_t k = 1; k < variant.size(); ++k) {
      if (!is_identifier_body_local(variant[k])) {
        error = "state variant '" + variant + "' is not an identifier";
        return true;
      }
    }
    variants.push_back(variant);
    i = bar + 1;
  }
  if (variants.size() < 2) {
    error = "state declaration requires at least two variants";
    return true;
  }
  return true;
}

static void add_parse_error(AstProgram& program, int line, const std::string& message) {
  program.parse_errors.push_back("line " + std::to_string(line) + ": " + message);
}

static bool is_interpolated_literal(const std::string& text) {
  const std::string clean = trim(text);
  if (clean.size() >= 3 && clean[0] == 'v' && clean[1] == '"' && clean.back() == '"') {
    return true;
  }
  return clean.size() >= 2 && clean.front() == '"' && clean.back() == '"' && clean.find('{') != std::string::npos &&
         clean.find('}') != std::string::npos;
}

static bool parse_identifier_list(const std::string& text, std::vector<std::string>& out) {
  auto is_ident_start_local = [](char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
  };
  auto is_ident_body_local = [](char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
  };
  out.clear();
  std::size_t i = 0;
  while (i < text.size()) {
    std::size_t comma = text.find(',', i);
    if (comma == std::string::npos) {
      comma = text.size();
    }
    const std::string part = trim(text.substr(i, comma - i));
    if (part.empty() || !is_ident_start_local(part[0])) {
      return false;
    }
    for (std::size_t k = 1; k < part.size(); ++k) {
      if (!is_ident_body_local(part[k])) {
        return false;
      }
    }
    out.push_back(part);
    i = comma + 1;
  }
  return !out.empty();
}

static bool parse_closure_literal(const std::string& text, std::vector<std::string>& params, std::string& body,
                                  bool& block_body) {
  const std::string clean = trim(text);
  if (clean.size() < 4 || clean[0] != '|') {
    return false;
  }
  const std::size_t second_bar = clean.find('|', 1);
  if (second_bar == std::string::npos || second_bar <= 1) {
    return false;
  }
  const std::string param_text = trim(clean.substr(1, second_bar - 1));
  body = trim(clean.substr(second_bar + 1));
  block_body = false;
  if (param_text.empty() || body.empty()) {
    return false;
  }
  if (!parse_identifier_list(param_text, params)) {
    return false;
  }
  if (body.size() >= 2 && body.front() == '{' && body.back() == '}') {
    body = trim(body.substr(1, body.size() - 2));
    if (body.empty()) {
      return false;
    }
    block_body = true;
  }
  return true;
}

static bool parse_interpolated_string_literal(const std::string& text, AstInterpolatedString& out) {
  out = AstInterpolatedString{};
  std::string clean = trim(text);
  if (clean.size() >= 3 && clean[0] == 'v' && clean[1] == '"' && clean.back() == '"') {
    clean = clean.substr(1);
  }
  if (clean.size() < 2 || clean.front() != '"' || clean.back() != '"') {
    return false;
  }
  out.raw = clean;
  const std::string inner = clean.substr(1, clean.size() - 2);
  std::string literal;
  for (std::size_t i = 0; i < inner.size();) {
    if (inner[i] == '{') {
      const std::size_t close = inner.find('}', i + 1);
      if (close == std::string::npos) {
        return false;
      }
      if (!literal.empty()) {
        out.segments.push_back(AstInterpolatedSegment{false, literal});
        literal.clear();
      }
      const std::string expr = trim(inner.substr(i + 1, close - i - 1));
      if (expr.empty()) {
        return false;
      }
      out.segments.push_back(AstInterpolatedSegment{true, expr});
      i = close + 1;
      continue;
    }
    literal.push_back(inner[i]);
    ++i;
  }
  if (!literal.empty()) {
    out.segments.push_back(AstInterpolatedSegment{false, literal});
  }
  return !out.segments.empty();
}

static std::vector<std::string> collect_closure_captures(const std::vector<std::string>& params, const std::string& body) {
  auto is_identifier_start_local = [](char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
  };
  auto is_identifier_body_local = [](char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
  };
  std::unordered_set<std::string> param_set(params.begin(), params.end());
  std::unordered_set<std::string> seen;
  std::vector<std::string> out;
  for (std::size_t i = 0; i < body.size();) {
    if (!is_identifier_start_local(body[i])) {
      ++i;
      continue;
    }
    const std::size_t start = i;
    while (i < body.size() && is_identifier_body_local(body[i])) {
      ++i;
    }
    const std::string name = body.substr(start, i - start);
    if (param_set.find(name) != param_set.end()) {
      continue;
    }
    if (name == "true" || name == "false" || name == "Some" || name == "None" || name == "Ok" || name == "Err") {
      continue;
    }
    if (seen.insert(name).second) {
      out.push_back(name);
    }
  }
  return out;
}

static bool parse_extern_function_declaration(const std::string& line, AstExternFunction& out) {
  const std::string clean = trim(line);
  if (!starts_with(clean, "extern func ")) {
    return false;
  }
  const std::size_t name_start = 12;
  const std::size_t lparen = clean.find('(', name_start);
  const std::size_t rparen = clean.rfind(')');
  if (lparen == std::string::npos || rparen == std::string::npos || rparen < lparen) {
    return false;
  }
  out.name = trim(clean.substr(name_start, lparen - name_start));
  if (out.name.empty()) {
    return false;
  }

  const std::string params = trim(clean.substr(lparen + 1, rparen - lparen - 1));
  out.param_types.clear();
  std::size_t i = 0;
  while (i < params.size()) {
    std::size_t comma = params.find(',', i);
    if (comma == std::string::npos) {
      comma = params.size();
    }
    std::string part = trim(params.substr(i, comma - i));
    if (!part.empty()) {
      const std::size_t colon = part.find(':');
      if (colon != std::string::npos && colon + 1 < part.size()) {
        part = trim(part.substr(colon + 1));
      }
      if (!part.empty()) {
        out.param_types.push_back(part);
      }
    }
    i = comma + 1;
  }

  const std::size_t arrow = clean.find("->", rparen);
  if (arrow == std::string::npos) {
    out.return_type = "i32";
  } else {
    out.return_type = trim(clean.substr(arrow + 2));
  }
  return !out.return_type.empty();
}

static bool valid_control_header(const std::string& keyword, const std::string& line) {
  std::string effective = trim(line);
  const std::size_t colon = effective.find(':');
  if (colon != std::string::npos && colon + 1 < effective.size()) {
    const std::string label = trim(effective.substr(0, colon));
    const std::string after = trim(effective.substr(colon + 1));
    if (!label.empty() && (starts_with(after, "for ") || starts_with(after, "while "))) {
      effective = after;
    }
  }
  if (!starts_with(effective, keyword + " ")) {
    return false;
  }
  if (!ends_with(effective, ":")) {
    return false;
  }
  const std::string head = trim(effective.substr(0, effective.size() - 1));
  const std::size_t lparen = head.find('(');
  const std::size_t rparen = head.rfind(')');
  if (lparen == std::string::npos || rparen == std::string::npos || lparen > rparen) {
    return false;
  }
  return true;
}

static bool valid_for_header(const std::string& line) {
  std::string effective = trim(line);
  const std::size_t colon = effective.find(':');
  if (colon != std::string::npos && colon + 1 < effective.size()) {
    const std::string label = trim(effective.substr(0, colon));
    const std::string after = trim(effective.substr(colon + 1));
    if (!label.empty() && (starts_with(after, "for ") || starts_with(after, "while "))) {
      effective = after;
    }
  }
  if (!starts_with(effective, "for ") || !ends_with(effective, ":")) {
    return false;
  }
  const std::string head = trim(effective.substr(0, effective.size() - 1));
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

static bool is_labeled_loop_header(const std::string& line, std::string& loop_head) {
  auto is_ident_start_local = [](char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
  };
  auto is_ident_body_local = [](char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
  };
  const std::string clean = trim(line);
  const std::size_t colon = clean.find(':');
  if (colon == std::string::npos || colon + 1 >= clean.size()) {
    return false;
  }
  std::string label = trim(clean.substr(0, colon));
  loop_head = trim(clean.substr(colon + 1));
  if (!label.empty() && label.front() == '\'') {
    label = trim(label.substr(1));
  }
  if (label.empty() || !is_ident_start_local(label[0])) {
    return false;
  }
  for (std::size_t i = 1; i < label.size(); ++i) {
    if (!is_ident_body_local(label[i])) {
      return false;
    }
  }
  return starts_with(loop_head, "for ") || starts_with(loop_head, "while ");
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
      !starts_with(line, "async func ") &&
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
    if (ch == '[') {
      out.push_back(ExprToken{ExprTokenKind::LParen, "["});
      ++i;
      continue;
    }
    if (ch == ']') {
      out.push_back(ExprToken{ExprTokenKind::RParen, "]"});
      ++i;
      continue;
    }
    if (ch == '?') {
      out.push_back(ExprToken{ExprTokenKind::Operator, "?"});
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
        if (text[i] == '.' && i + 1 < text.size() &&
            (is_identifier_start(text[i + 1]) || std::isdigit(static_cast<unsigned char>(text[i + 1])))) {
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
      if (current_token(cursor).kind == ExprTokenKind::Operator && current_token(cursor).text == "?") {
        ++cursor.index;
        return atom + "?";
      }
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
    if (current_token(cursor).kind == ExprTokenKind::Operator && current_token(cursor).text == "?") {
      ++cursor.index;
      out += "?";
    }
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
    std::string out = "(" + expr;
    while (current_token(cursor).kind == ExprTokenKind::Comma) {
      ++cursor.index;
      const std::string tuple_item = parse_expression_equality(cursor);
      if (!cursor.error.empty()) {
        return "";
      }
      out += ", " + tuple_item;
    }
    if (current_token(cursor).kind != ExprTokenKind::RParen) {
      cursor.error = "missing closing ')' in expression";
      return "";
    }
    ++cursor.index;
    out += ")";
    if (current_token(cursor).kind == ExprTokenKind::Operator && current_token(cursor).text == "?") {
      ++cursor.index;
      out += "?";
    }
    return out;
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

static std::string strip_await_prefix(const std::string& expr_text) {
  std::string clean = trim(expr_text);
  while (starts_with(clean, "await ")) {
    clean = trim(clean.substr(6));
  }
  return clean;
}

static ParsedExpression parse_expression_text(const std::string& expr_text) {
  ParsedExpression out;
  const std::string clean = strip_await_prefix(expr_text);
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
  std::string effective = trim(line);
  const std::size_t colon = effective.find(':');
  if (colon != std::string::npos && colon + 1 < effective.size()) {
    const std::string label = trim(effective.substr(0, colon));
    const std::string after = trim(effective.substr(colon + 1));
    if (!label.empty() && starts_with(after, "for ")) {
      effective = after;
    }
  }
  if (!starts_with(effective, "for ") || !ends_with(effective, ":")) {
    return "";
  }
  const std::string body = trim(effective.substr(4, effective.size() - 5));
  const std::size_t in_pos = body.find(" in ");
  if (in_pos == std::string::npos) {
    return "";
  }
  return trim(body.substr(in_pos + 4));
}

static bool is_parenthesized_tuple_expression(const std::string& expr) {
  const std::string clean = trim(expr);
  if (clean.size() < 2 || clean.front() != '(' || clean.back() != ')') {
    return false;
  }

  int nested = 0;
  bool in_string = false;
  bool escaping = false;
  for (std::size_t i = 1; i + 1 < clean.size(); ++i) {
    const char ch = clean[i];
    if (in_string) {
      if (escaping) {
        escaping = false;
        continue;
      }
      if (ch == '\\') {
        escaping = true;
        continue;
      }
      if (ch == '"') {
        in_string = false;
      }
      continue;
    }
    if (ch == '"') {
      in_string = true;
      continue;
    }
    if (ch == '(' || ch == '[' || ch == '{') {
      ++nested;
      continue;
    }
    if (ch == ')' || ch == ']' || ch == '}') {
      if (nested > 0) {
        --nested;
      }
      continue;
    }
    if (ch == ',' && nested == 0) {
      return true;
    }
  }
  return false;
}

static bool should_accept_raw_expression(const std::string& expr) {
  const std::string clean = trim(expr);
  if (clean.empty()) {
    return false;
  }
  if (clean.find('[') != std::string::npos || clean.find(']') != std::string::npos) {
    return true;
  }
  if (is_parenthesized_tuple_expression(clean)) {
    return true;
  }
  if (!clean.empty() && clean.back() == '?') {
    return true;
  }
  return false;
}

static void parse_statement_expression(AstProgram& program, AstStatement& st) {
  std::string expr_text;
  std::vector<std::string> closure_params;
  std::string closure_body;
  bool closure_block = false;
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
  } else if (st.kind == StatementKind::Defer) {
    expr_text = trim(st.text.substr(6));
    if (expr_text.empty()) {
      st.has_expression = true;
      st.expression_valid = false;
      st.expression_error = "defer statement requires deferred expression";
      add_parse_error(program, st.line, st.expression_error);
      return;
    }
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
    st.has_await = starts_with(trim(expr_text), "await ");
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
  } else if (st.kind == StatementKind::Break || st.kind == StatementKind::Continue) {
    return;
  } else if (st.kind == StatementKind::Else) {
    return;
  } else {
    const std::string line = trim(st.text);
    if (starts_with(line, "break") || starts_with(line, "continue")) {
      return;
    }
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

  st.has_await = starts_with(trim(expr_text), "await ");
  st.has_expression = true;
  if (parse_closure_literal(expr_text, closure_params, closure_body, closure_block)) {
    st.expression_valid = true;
    st.expression_normalized = trim(expr_text);
    st.expression_error.clear();
    AstClosure closure;
    closure.params = closure_params;
    closure.body = closure_body;
    closure.line = st.line;
    closure.block_body = closure_block;
    closure.captures = collect_closure_captures(closure_params, closure_body);
    program.closures.push_back(std::move(closure));
    return;
  }
  AstInterpolatedString interpolated;
  if (is_interpolated_literal(expr_text) && parse_interpolated_string_literal(expr_text, interpolated)) {
    interpolated.line = st.line;
    program.interpolated_strings.push_back(std::move(interpolated));
    st.expression_valid = true;
    st.expression_normalized = trim(expr_text);
    st.expression_error.clear();
    return;
  }
  if (should_accept_raw_expression(expr_text)) {
    st.expression_valid = true;
    st.expression_normalized = trim(expr_text);
    st.expression_error.clear();
    return;
  }
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
  std::string loop_head;
  const bool labeled_loop = is_labeled_loop_header(body.clean, loop_head);
  if (starts_with(body.clean, "import ") || starts_with(body.clean, "from ")) {
    st.kind = StatementKind::Expr;
    add_parse_error(program, body.number, "import statements are only allowed at top-level scope");
    parse_statement_expression(program, st);
    return st;
  }
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
  } else if (starts_with(body.clean, "while ") || (labeled_loop && starts_with(loop_head, "while "))) {
    st.kind = StatementKind::While;
    if (!valid_control_header("while", body.clean)) {
      add_parse_error(program, body.number, "while requires parentheses and trailing ':'");
    }
  } else if (starts_with(body.clean, "for ") || (labeled_loop && starts_with(loop_head, "for "))) {
    st.kind = StatementKind::For;
    if (!valid_for_header(body.clean)) {
      add_parse_error(program, body.number, "for header must follow 'for <name> in <expr>:'");
    }
  } else if (starts_with(body.clean, "match ")) {
    st.kind = StatementKind::Match;
    if (!ends_with(body.clean, ":")) {
      add_parse_error(program, body.number, "match header must be colon-terminated");
    }
  } else if (starts_with(body.clean, "defer ")) {
    st.kind = StatementKind::Defer;
  } else if (starts_with(body.clean, "break")) {
    st.kind = StatementKind::Break;
  } else if (starts_with(body.clean, "continue")) {
    st.kind = StatementKind::Continue;
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

static bool is_identifier(const std::string& text) {
  if (text.empty() || !(std::isalpha(static_cast<unsigned char>(text[0])) || text[0] == '_')) {
    return false;
  }
  for (std::size_t i = 1; i < text.size(); ++i) {
    if (!(std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_')) {
      return false;
    }
  }
  return true;
}

static bool parse_module_path(const std::string& text, std::vector<std::string>& out, std::string& error) {
  out.clear();
  std::size_t i = 0;
  while (i < text.size()) {
    std::size_t dot = text.find('.', i);
    if (dot == std::string::npos) {
      dot = text.size();
    }
    const std::string segment = trim(text.substr(i, dot - i));
    if (!is_identifier(segment)) {
      error = "invalid import module path segment '" + segment + "'";
      return false;
    }
    out.push_back(segment);
    i = dot + 1;
  }
  if (out.empty()) {
    error = "import module path is empty";
    return false;
  }
  return true;
}

static bool parse_import_symbols(const std::string& text, std::vector<std::string>& out, std::string& error) {
  out.clear();
  std::size_t i = 0;
  while (i < text.size()) {
    std::size_t comma = text.find(',', i);
    if (comma == std::string::npos) {
      comma = text.size();
    }
    const std::string symbol = trim(text.substr(i, comma - i));
    if (symbol == "*") {
      error = "wildcard import is not supported";
      return false;
    }
    if (!is_identifier(symbol)) {
      error = "invalid imported symbol '" + symbol + "'";
      return false;
    }
    out.push_back(symbol);
    i = comma + 1;
  }
  if (out.empty()) {
    error = "from-import requires at least one symbol";
    return false;
  }
  return true;
}

static bool parse_import_decl(const std::string& line, AstImport& out, std::string& error) {
  const std::string clean = trim(line);
  if (starts_with(clean, "import ")) {
    std::string rest = trim(clean.substr(7));
    std::string module_text = rest;
    std::string alias;
    const std::size_t as_pos = rest.find(" as ");
    if (as_pos != std::string::npos) {
      module_text = trim(rest.substr(0, as_pos));
      alias = trim(rest.substr(as_pos + 4));
      if (!is_identifier(alias)) {
        error = "invalid import alias '" + alias + "'";
        return false;
      }
    }
    std::vector<std::string> module_path;
    if (!parse_module_path(module_text, module_path, error)) {
      return false;
    }
    out = AstImport{};
    out.is_from_import = false;
    out.module_path = std::move(module_path);
    out.alias = alias;
    out.raw = clean;
    return true;
  }
  if (starts_with(clean, "from ")) {
    const std::string rest = trim(clean.substr(5));
    const std::size_t import_pos = rest.find(" import ");
    if (import_pos == std::string::npos) {
      error = "from-import must use 'from <module> import <symbol>'";
      return false;
    }
    const std::string module_text = trim(rest.substr(0, import_pos));
    const std::string symbols_text = trim(rest.substr(import_pos + 8));
    std::vector<std::string> module_path;
    if (!parse_module_path(module_text, module_path, error)) {
      return false;
    }
    std::vector<std::string> symbols;
    if (!parse_import_symbols(symbols_text, symbols, error)) {
      return false;
    }
    out = AstImport{};
    out.is_from_import = true;
    out.module_path = std::move(module_path);
    out.symbols = std::move(symbols);
    out.raw = clean;
    return true;
  }
  error = "not an import declaration";
  return false;
}

static std::string flow_name_from_header(const std::string& header) {
  if (!starts_with(header, "flow ") || !ends_with(header, ":")) {
    return "";
  }
  std::string body = trim(header.substr(5, header.size() - 6));
  if (body.empty()) {
    return "";
  }
  const std::size_t lparen = body.find('(');
  if (lparen != std::string::npos) {
    body = trim(body.substr(0, lparen));
  }
  if (!is_identifier(body)) {
    return "";
  }
  return body;
}

static bool parse_non_negative_i32(const std::string& text, int& out) {
  const std::string clean = trim(text);
  if (clean.empty()) {
    return false;
  }
  for (char ch : clean) {
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return false;
    }
  }
  out = std::stoi(clean);
  return true;
}

static bool parse_timeout_ms(const std::string& text, int& out) {
  std::string clean = trim(text);
  if (clean.empty()) {
    return false;
  }
  int multiplier = 1;
  if (ends_with(clean, "ms")) {
    clean = trim(clean.substr(0, clean.size() - 2));
  } else if (ends_with(clean, "s")) {
    clean = trim(clean.substr(0, clean.size() - 1));
    multiplier = 1000;
  }
  int value = 0;
  if (!parse_non_negative_i32(clean, value)) {
    return false;
  }
  out = value * multiplier;
  return true;
}

static bool parse_flow_step_header(const std::string& line, AstFlowStep& out_step, std::string& error) {
  if (!starts_with(line, "step ")) {
    error = "flow step must start with 'step'";
    return false;
  }
  const std::string body = trim(line.substr(5));
  if (body.empty()) {
    error = "flow step requires action expression";
    return false;
  }
  const std::size_t eq = body.find('=');
  if (eq == std::string::npos) {
    out_step.name.clear();
    out_step.action = body;
    return true;
  }
  const std::string name = trim(body.substr(0, eq));
  const std::string action = trim(body.substr(eq + 1));
  if (!is_identifier(name)) {
    error = "flow step name must be an identifier";
    return false;
  }
  if (action.empty()) {
    error = "flow step requires action expression";
    return false;
  }
  out_step.name = name;
  out_step.action = action;
  return true;
}

static void parse_flow_step_directive(AstProgram& program, AstFlowStep& step, const SourceLine& line) {
  const std::string clean = trim(line.clean);
  if (starts_with(clean, "undo ")) {
    if (!step.undo_action.empty()) {
      add_parse_error(program, line.number, "flow step cannot declare multiple undo directives");
      return;
    }
    step.undo_action = trim(clean.substr(5));
    if (step.undo_action.empty()) {
      add_parse_error(program, line.number, "undo directive requires expression");
    }
    return;
  }
  if (starts_with(clean, "retry ")) {
    if (step.has_retry) {
      add_parse_error(program, line.number, "flow step cannot declare multiple retry directives");
      return;
    }
    int retry = 0;
    if (!parse_non_negative_i32(clean.substr(6), retry)) {
      add_parse_error(program, line.number, "retry directive expects non-negative integer");
      return;
    }
    step.retry_count = retry;
    step.has_retry = true;
    return;
  }
  if (starts_with(clean, "timeout ")) {
    if (step.has_timeout) {
      add_parse_error(program, line.number, "flow step cannot declare multiple timeout directives");
      return;
    }
    int timeout_ms = 0;
    if (!parse_timeout_ms(clean.substr(8), timeout_ms)) {
      add_parse_error(program, line.number, "timeout directive expects integer milliseconds or seconds suffix");
      return;
    }
    step.timeout_ms = timeout_ms;
    step.has_timeout = true;
    return;
  }
  if (clean == "idempotent") {
    step.idempotent = true;
    return;
  }
  if (clean == "irreversible") {
    step.irreversible = true;
    return;
  }
  add_parse_error(program, line.number, "unsupported flow step directive: '" + clean + "'");
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
    bool is_pub_decl = false;
    if (starts_with(effective_line, "pub ")) {
      is_pub_decl = true;
      program.public_decls.push_back(line.clean);
      effective_line = trim(effective_line.substr(4));
    }
    if (starts_with(effective_line, "intent ")) {
      program.intents.push_back(effective_line);
    }
    if (starts_with(effective_line, "flow ")) {
      program.flows.push_back(effective_line);
    }
    if (starts_with(effective_line, "intent func ")) {
      effective_line = trim(effective_line.substr(7));
    } else if (starts_with(effective_line, "flow func ")) {
      effective_line = trim(effective_line.substr(5));
    }

    if (starts_with(effective_line, "func ") || starts_with(effective_line, "async func ")) {
      const bool is_async_func = starts_with(effective_line, "async func ");
      const std::string function_header = is_async_func ? trim(effective_line.substr(6)) : effective_line;
      AstFunction fn;
      fn.name = function_name_from_header(function_header);
      fn.params = function_params_from_header(function_header);
      fn.param_types = function_param_types_from_header(function_header);
      fn.header_line = line.number;
      fn.header_indent = line.indent;
      fn.is_pub = is_pub_decl;
      fn.is_async = is_async_func;

      if (!ends_with(function_header, ":")) {
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
      fn.return_type = function_return_type_from_header(function_header);
      if (function_header.find("->") != std::string::npos && fn.return_type.empty()) {
        add_parse_error(program, line.number, "function return annotation '-> type' is not supported");
      }

      if (fn.name == "main") {
        program.has_main = true;
      }
      if (!fn.name.empty() && fn.name.find('.') == std::string::npos) {
        program.function_visibility[fn.name] = fn.is_pub;
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

    if (starts_with(effective_line, "flow ") && !starts_with(effective_line, "flow func ")) {
      AstFlow flow;
      flow.header = effective_line;
      flow.name = flow_name_from_header(effective_line);
      flow.line = line.number;
      flow.indent = line.indent;
      if (!ends_with(effective_line, ":")) {
        add_parse_error(program, line.number, "flow header must be colon-terminated");
      }
      if (flow.name.empty()) {
        add_parse_error(program, line.number, "invalid flow header");
      }

      ++i;
      while (i < lines.size() && lines[i].indent > line.indent) {
        collect_feature_counters(lines[i].clean, program);
        const SourceLine& step_line = lines[i];
        if (!starts_with(step_line.clean, "step ")) {
          add_parse_error(program, step_line.number, "flow block only accepts 'step' entries");
          ++i;
          continue;
        }
        AstFlowStep step;
        step.line = step_line.number;
        std::string step_error;
        if (!parse_flow_step_header(step_line.clean, step, step_error)) {
          add_parse_error(program, step_line.number, step_error);
          ++i;
          while (i < lines.size() && lines[i].indent > step_line.indent) {
            ++i;
          }
          continue;
        }
        ++i;
        while (i < lines.size() && lines[i].indent > step_line.indent) {
          collect_feature_counters(lines[i].clean, program);
          parse_flow_step_directive(program, step, lines[i]);
          ++i;
        }
        flow.steps.push_back(std::move(step));
      }
      if (flow.steps.empty()) {
        add_parse_error(program, line.number, "flow block must contain at least one step");
      }
      program.flow_defs.push_back(std::move(flow));
      continue;
    }

    if (starts_with(effective_line, "import ") || starts_with(effective_line, "from ")) {
      AstImport import_decl;
      std::string import_error;
      if (!parse_import_decl(effective_line, import_decl, import_error)) {
        add_parse_error(program, line.number, import_error);
      } else {
        import_decl.line = line.number;
        import_decl.column = 1;
        program.imports.push_back(std::move(import_decl));
      }
      ++i;
      continue;
    }
    if (starts_with(effective_line, "extern ")) {
      program.extern_decls.push_back(effective_line);
      AstExternFunction ext;
      if (!parse_extern_function_declaration(effective_line, ext)) {
        add_parse_error(program, line.number, "malformed extern declaration");
      } else {
        ext.line = line.number;
        program.extern_functions.push_back(std::move(ext));
      }
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
      } else {
        program.struct_visibility[struct_name] = is_pub_decl;
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
      const std::string enum_name = enum_name_from_header(effective_line);
      if (enum_name.empty()) {
        add_parse_error(program, line.number, "invalid enum header");
      } else {
        program.enum_visibility[enum_name] = is_pub_decl;
      }
      int variant_index = 0;
      ++i;
      while (i < lines.size() && lines[i].indent > line.indent) {
        collect_feature_counters(lines[i].clean, program);
        const std::string variant = enum_variant_name_from_line(lines[i].clean);
        if (!variant.empty() && program.enum_variant_tags.find(variant) == program.enum_variant_tags.end()) {
          program.enum_variant_tags[variant] = variant_index++;
          const std::string payload_type = enum_variant_payload_type_from_line(lines[i].clean);
          if (!payload_type.empty()) {
            program.enum_variant_payload_types[variant] = payload_type;
          }
        }
        if (!enum_name.empty() && !variant.empty()) {
          program.enum_variants[enum_name].push_back(variant);
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
    if (starts_with(effective_line, "state ")) {
      std::string state_name;
      std::vector<std::string> variants;
      std::string state_error;
      const bool parsed = parse_state_header(effective_line, state_name, variants, state_error);
      if (!parsed) {
        add_parse_error(program, line.number, "invalid state declaration");
      } else if (!state_error.empty()) {
        add_parse_error(program, line.number, state_error);
      } else {
        program.state_sets[state_name] = variants;
      }
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
      std::string impl_type_name;
      const bool is_type_impl = parse_impl_type_header(effective_line, impl_type_name);
      const std::string impl_key = trait_name + "|" + type_name;
      if (is_impl_for) {
        program.impl_for_headers.push_back(effective_line);
      }
      ++i;
      while (i < lines.size() && lines[i].indent > line.indent) {
        const SourceLine& member_line = lines[i];
        collect_feature_counters(member_line.clean, program);
        std::string effective_member = member_line.clean;
        if (starts_with(effective_member, "pub ")) {
          effective_member = trim(effective_member.substr(4));
        }
        if (is_impl_for) {
          const std::string method = method_name_from_line(member_line.clean);
          if (!method.empty()) {
            program.impl_for_methods[impl_key].push_back(method);
          }
        }
        if (is_type_impl && (starts_with(effective_member, "func ") || starts_with(effective_member, "async func "))) {
          const bool async_method = starts_with(effective_member, "async func ");
          const std::string method_header = async_method ? trim(effective_member.substr(6)) : effective_member;
          AstFunction fn;
          const std::string method_name = function_name_from_header(method_header);
          if (method_name.empty()) {
            add_parse_error(program, member_line.number, "invalid impl method header");
            ++i;
            while (i < lines.size() && lines[i].indent > member_line.indent) {
              ++i;
            }
            continue;
          }
          fn.name = impl_type_name + "." + method_name;
          fn.params = function_params_from_header(method_header);
          fn.param_types = function_param_types_from_header(method_header);
          if (fn.params.empty() || fn.params.front() != "self") {
            fn.params.insert(fn.params.begin(), "self");
            fn.param_types.insert(fn.param_types.begin(), "");
          }
          fn.header_line = member_line.number;
          fn.header_indent = member_line.indent;
          fn.return_type = function_return_type_from_header(method_header);
          fn.is_async = async_method;
          if (!ends_with(method_header, ":")) {
            add_parse_error(program, member_line.number, "impl method header must be colon-terminated");
          }
          if (method_header.find("->") != std::string::npos && fn.return_type.empty()) {
            add_parse_error(program, member_line.number, "impl method return annotation '-> type' is not supported");
          }
          if (!method_name.empty()) {
            auto& methods = program.struct_methods[impl_type_name];
            if (std::find(methods.begin(), methods.end(), method_name) == methods.end()) {
              methods.push_back(method_name);
            }
          }

          ++i;
          if (i >= lines.size() || lines[i].indent <= fn.header_indent) {
            add_parse_error(program, member_line.number, "impl method body must be indentation-scoped");
          }
          while (i < lines.size() && lines[i].indent > fn.header_indent) {
            AstStatement st = build_statement_from_line(program, lines[i]);
            fn.body.push_back(std::move(st));
            ++i;
          }
          program.functions.push_back(std::move(fn));
          continue;
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
