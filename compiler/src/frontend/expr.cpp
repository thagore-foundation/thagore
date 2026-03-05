#include "internal.hpp"

namespace thagc::syntax {

// ---------------------------------------------------------------------------
// Character classification
// ---------------------------------------------------------------------------

static bool is_identifier_start(char ch) {
  return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}

static bool is_identifier_body(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

// ---------------------------------------------------------------------------
// Statement shape detection
// ---------------------------------------------------------------------------

bool valid_control_header(const std::string& keyword, const std::string& line) {
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

bool valid_for_header(const std::string& line) {
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
  std::size_t i = 4;
  while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
    ++i;
  }
  if (i >= line.size() || !is_identifier_start(line[i])) {
    return "";
  }
  const std::size_t start = i;
  while (i < line.size() && is_identifier_body(line[i])) {
    ++i;
  }
  return line.substr(start, i - start);
}

bool is_simple_assignable_target(const std::string& text) {
  if (text.empty() || !is_identifier_start(text[0])) {
    return false;
  }
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char ch = text[i];
    if (ch == '.') {
      if (i == 0 || i + 1 >= text.size() || !is_identifier_start(text[i + 1])) {
        return false;
      }
      continue;
    }
    if (!is_identifier_body(ch)) {
      return false;
    }
  }
  return true;
}

bool is_assignment_line(const std::string& line) {
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

bool is_labeled_loop_header(const std::string& line, std::string& loop_head) {
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
  if (label.empty() || !is_identifier_start(label[0])) {
    return false;
  }
  for (std::size_t i = 1; i < label.size(); ++i) {
    if (!is_identifier_body(label[i])) {
      return false;
    }
  }
  return starts_with(loop_head, "for ") || starts_with(loop_head, "while ");
}

void collect_feature_counters(const std::string& line, AstProgram& program) {
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
  if (starts_with(line, "for ") && line.find(" in ") != std::string::npos &&
      line.find("..") != std::string::npos) {
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
      !starts_with(line, "async func ") && !starts_with(line, "struct ") &&
      !starts_with(line, "enum ") && !starts_with(line, "impl ") &&
      !starts_with(line, "trait ") && !starts_with(line, "match ") &&
      !starts_with(line, "else")) {
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

// ---------------------------------------------------------------------------
// Expression tokenizer
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Pratt parser for expression normalization
// ---------------------------------------------------------------------------

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
      if (current_token(cursor).kind == ExprTokenKind::Operator &&
          current_token(cursor).text == "?") {
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
    if (current_token(cursor).kind == ExprTokenKind::Operator &&
        current_token(cursor).text == "?") {
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
    if (current_token(cursor).kind == ExprTokenKind::Operator &&
        current_token(cursor).text == "?") {
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
    const bool is_eq =
        tok.kind == ExprTokenKind::Operator && (tok.text == "==" || tok.text == "!=");
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

// ---------------------------------------------------------------------------
// Expression extraction helpers
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Statement-level expression parsing
// ---------------------------------------------------------------------------

static void parse_statement_expression(AstProgram& program, AstStatement& st,
                                       const std::unordered_map<std::string, AstMacro>& macros) {
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
    std::string expanded_expr;
    std::string expansion_error;
    if (!expand_macros(expr_text, macros, expanded_expr, expansion_error)) {
      st.has_expression = true;
      st.expression_valid = false;
      st.expression_error = "macro expansion failed: " + expansion_error;
      add_parse_error(program, st.line, st.expression_error);
      return;
    }
    expr_text = expanded_expr;
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

  std::string expanded_expr;
  std::string expansion_error;
  if (!expand_macros(expr_text, macros, expanded_expr, expansion_error)) {
    st.has_expression = true;
    st.expression_valid = false;
    st.expression_error = "macro expansion failed: " + expansion_error;
    add_parse_error(program, st.line, st.expression_error);
    return;
  }
  expr_text = expanded_expr;

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

AstStatement build_statement_from_line(AstProgram& program, const SourceLine& body,
                                       const std::unordered_map<std::string, AstMacro>& macros) {
  AstStatement st;
  st.text = body.clean;
  st.line = body.number;
  st.indent = body.indent;
  st.span = body.span;
  std::string loop_head;
  const bool labeled_loop = is_labeled_loop_header(body.clean, loop_head);
  if (starts_with(body.clean, "import ") || starts_with(body.clean, "from ")) {
    st.kind = StatementKind::Expr;
    add_parse_error(program, body.number, "import statements are only allowed at top-level scope");
    parse_statement_expression(program, st, macros);
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
  parse_statement_expression(program, st, macros);
  return st;
}

int parse_return_literal(const std::string& line) {
  const std::string expr = [&]() -> std::string {
    if (trim(line) == "return") return "";
    if (!starts_with(line, "return ")) return "";
    return trim(line.substr(7));
  }();
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

}  // namespace thagc::syntax
