#include "internal.hpp"

namespace thagc::syntax {

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

static bool parse_macro_param_list(const std::string& text, std::vector<std::string>& out, std::string& error) {
  auto is_ident_start_local = [](char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
  };
  auto is_ident_body_local = [](char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
  };
  out.clear();
  const std::string clean = trim(text);
  if (clean.empty()) {
    return true;
  }
  std::size_t i = 0;
  while (i < clean.size()) {
    std::size_t comma = clean.find(',', i);
    if (comma == std::string::npos) {
      comma = clean.size();
    }
    const std::string part = trim(clean.substr(i, comma - i));
    if (part.empty() || !is_ident_start_local(part[0])) {
      error = "macro parameter must be identifier";
      return false;
    }
    for (std::size_t k = 1; k < part.size(); ++k) {
      if (!is_ident_body_local(part[k])) {
        error = "macro parameter must be identifier";
        return false;
      }
    }
    out.push_back(part);
    i = comma + 1;
  }
  return true;
}

bool parse_macro_declaration(const std::string& line, AstMacro& out, std::string& error) {
  auto is_identifier_local = [](const std::string& text) {
    if (text.empty() || !(std::isalpha(static_cast<unsigned char>(text[0])) || text[0] == '_')) {
      return false;
    }
    for (std::size_t i = 1; i < text.size(); ++i) {
      if (!(std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_')) {
        return false;
      }
    }
    return true;
  };
  const std::string clean = trim(line);
  if (!starts_with(clean, "macro ")) {
    error = "not a macro declaration";
    return false;
  }
  const std::size_t name_start = 6;
  const std::size_t lparen = clean.find('(', name_start);
  const std::size_t rparen = clean.find(')', lparen == std::string::npos ? 0 : lparen + 1);
  const std::size_t eq = clean.find('=', rparen == std::string::npos ? 0 : rparen + 1);
  if (lparen == std::string::npos || rparen == std::string::npos || eq == std::string::npos ||
      rparen < lparen || eq <= rparen) {
    error = "macro syntax is 'macro name(args) = expression'";
    return false;
  }
  std::string name = trim(clean.substr(name_start, lparen - name_start));
  if (!is_identifier_local(name)) {
    error = "macro name must be identifier";
    return false;
  }
  std::vector<std::string> params;
  std::string param_error;
  if (!parse_macro_param_list(clean.substr(lparen + 1, rparen - lparen - 1), params, param_error)) {
    error = param_error;
    return false;
  }
  std::string body = trim(clean.substr(eq + 1));
  if (body.empty()) {
    error = "macro body cannot be empty";
    return false;
  }
  out = AstMacro{};
  out.name = std::move(name);
  out.params = std::move(params);
  out.body = std::move(body);
  return true;
}

static bool split_call_arguments(const std::string& text, std::vector<std::string>& out, std::string& error) {
  out.clear();
  std::size_t start = 0;
  int depth = 0;
  bool in_string = false;
  bool escape = false;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    const char ch = i < text.size() ? text[i] : ',';
    if (in_string) {
      if (escape) {
        escape = false;
      } else if (ch == '\\') {
        escape = true;
      } else if (ch == '"') {
        in_string = false;
      }
      continue;
    }
    if (ch == '"') {
      in_string = true;
      continue;
    }
    if (ch == '(' || ch == '[' || ch == '{') {
      ++depth;
      continue;
    }
    if (ch == ')' || ch == ']' || ch == '}') {
      if (depth > 0) {
        --depth;
      }
      continue;
    }
    if (ch == ',' && depth == 0) {
      const std::string arg = trim(text.substr(start, i - start));
      if (!arg.empty()) {
        out.push_back(arg);
      } else if (i != text.size()) {
        error = "macro arguments cannot be empty";
        return false;
      }
      start = i + 1;
    }
  }
  if (!trim(text).empty() && out.empty()) {
    error = "invalid macro argument list";
    return false;
  }
  return true;
}

static std::string substitute_macro_params(const AstMacro& macro, const std::vector<std::string>& args) {
  std::unordered_map<std::string, std::string> mapping;
  for (std::size_t i = 0; i < macro.params.size() && i < args.size(); ++i) {
    mapping[macro.params[i]] = args[i];
  }
  std::string out;
  bool in_string = false;
  bool escape = false;
  for (std::size_t i = 0; i < macro.body.size();) {
    const char ch = macro.body[i];
    if (in_string) {
      out.push_back(ch);
      if (escape) {
        escape = false;
      } else if (ch == '\\') {
        escape = true;
      } else if (ch == '"') {
        in_string = false;
      }
      ++i;
      continue;
    }
    if (ch == '"') {
      in_string = true;
      out.push_back(ch);
      ++i;
      continue;
    }
    if (!(std::isalpha(static_cast<unsigned char>(ch)) || ch == '_')) {
      out.push_back(ch);
      ++i;
      continue;
    }
    const std::size_t start = i;
    ++i;
    while (i < macro.body.size() &&
           (std::isalnum(static_cast<unsigned char>(macro.body[i])) || macro.body[i] == '_')) {
      ++i;
    }
    const std::string ident = macro.body.substr(start, i - start);
    auto it = mapping.find(ident);
    if (it != mapping.end()) {
      out += "(" + it->second + ")";
    } else {
      out += ident;
    }
  }
  return out;
}

static bool expand_macros_once(const std::string& expression,
                               const std::unordered_map<std::string, AstMacro>& macros,
                               std::string& out_expression, std::string& error, bool& changed) {
  out_expression.clear();
  error.clear();
  changed = false;
  bool in_string = false;
  bool escape = false;
  for (std::size_t i = 0; i < expression.size();) {
    const char ch = expression[i];
    if (in_string) {
      out_expression.push_back(ch);
      if (escape) {
        escape = false;
      } else if (ch == '\\') {
        escape = true;
      } else if (ch == '"') {
        in_string = false;
      }
      ++i;
      continue;
    }
    if (ch == '"') {
      in_string = true;
      out_expression.push_back(ch);
      ++i;
      continue;
    }
    if (!(std::isalpha(static_cast<unsigned char>(ch)) || ch == '_')) {
      out_expression.push_back(ch);
      ++i;
      continue;
    }

    const std::size_t name_start = i;
    ++i;
    while (i < expression.size() &&
           (std::isalnum(static_cast<unsigned char>(expression[i])) || expression[i] == '_')) {
      ++i;
    }
    const std::string name = expression.substr(name_start, i - name_start);
    std::size_t j = i;
    while (j < expression.size() && std::isspace(static_cast<unsigned char>(expression[j]))) {
      ++j;
    }
    if (j >= expression.size() || expression[j] != '!') {
      out_expression += name;
      continue;
    }
    std::size_t k = j + 1;
    while (k < expression.size() && std::isspace(static_cast<unsigned char>(expression[k]))) {
      ++k;
    }
    if (k >= expression.size() || expression[k] != '(') {
      out_expression += expression.substr(name_start, k - name_start);
      i = k;
      continue;
    }
    auto macro_it = macros.find(name);
    if (macro_it == macros.end()) {
      error = "unknown macro '" + name + "'";
      return false;
    }
    const std::size_t args_start = k + 1;
    int depth = 1;
    bool call_string = false;
    bool call_escape = false;
    std::size_t pos = args_start;
    for (; pos < expression.size(); ++pos) {
      const char c = expression[pos];
      if (call_string) {
        if (call_escape) {
          call_escape = false;
        } else if (c == '\\') {
          call_escape = true;
        } else if (c == '"') {
          call_string = false;
        }
        continue;
      }
      if (c == '"') {
        call_string = true;
        continue;
      }
      if (c == '(') {
        ++depth;
      } else if (c == ')') {
        --depth;
        if (depth == 0) {
          break;
        }
      }
    }
    if (pos >= expression.size() || depth != 0) {
      error = "unterminated macro invocation '" + name + "!('";
      return false;
    }
    std::vector<std::string> args;
    std::string args_error;
    if (!split_call_arguments(expression.substr(args_start, pos - args_start), args, args_error)) {
      error = args_error.empty() ? ("invalid arguments for macro '" + name + "'") : args_error;
      return false;
    }
    const AstMacro& macro = macro_it->second;
    if (args.size() != macro.params.size()) {
      error = "macro '" + name + "' expects " + std::to_string(macro.params.size()) +
              " arguments but got " + std::to_string(args.size());
      return false;
    }
    out_expression += "(" + substitute_macro_params(macro, args) + ")";
    changed = true;
    i = pos + 1;
  }
  return true;
}

bool expand_macros(const std::string& expression,
                   const std::unordered_map<std::string, AstMacro>& macros,
                   std::string& out_expression, std::string& error) {
  out_expression = expression;
  error.clear();
  for (int iter = 0; iter < 16; ++iter) {
    std::string next;
    bool changed = false;
    if (!expand_macros_once(out_expression, macros, next, error, changed)) {
      return false;
    }
    out_expression = std::move(next);
    if (!changed) {
      return true;
    }
  }
  error = "macro expansion exceeded recursion limit";
  return false;
}

bool is_interpolated_literal(const std::string& text) {
  const std::string clean = trim(text);
  if (clean.size() >= 3 && clean[0] == 'v' && clean[1] == '"' && clean.back() == '"') {
    return true;
  }
  return clean.size() >= 2 && clean.front() == '"' && clean.back() == '"' &&
         clean.find('{') != std::string::npos && clean.find('}') != std::string::npos;
}

bool parse_interpolated_string_literal(const std::string& text, AstInterpolatedString& out) {
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

bool parse_closure_literal(const std::string& text, std::vector<std::string>& params,
                           std::string& body, bool& block_body) {
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

std::vector<std::string> collect_closure_captures(const std::vector<std::string>& params,
                                                  const std::string& body) {
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
    if (name == "true" || name == "false" || name == "Some" || name == "None" ||
        name == "Ok" || name == "Err") {
      continue;
    }
    if (seen.insert(name).second) {
      out.push_back(name);
    }
  }
  return out;
}

}  // namespace thagc::syntax
