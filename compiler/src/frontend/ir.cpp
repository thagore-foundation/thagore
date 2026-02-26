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

static std::string return_type_from_header(const std::string& line) {
  const std::size_t arrow = line.find("->");
  if (arrow == std::string::npos) {
    return "";
  }
  std::size_t start = arrow + 2;
  while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) {
    ++start;
  }
  std::size_t end = line.find(':', start);
  if (end == std::string::npos) {
    end = line.size();
  }
  return trim(line.substr(start, end - start));
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
      while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
        ++i;
      }
      out.push_back(ExprToken{ExprTokenKind::Atom, text.substr(start, i - start)});
      continue;
    }
    if (is_identifier_start(ch)) {
      std::size_t start = i;
      while (i < text.size() && is_identifier_body(text[i])) {
        ++i;
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
    return tok.text;
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

static std::string expression_from_return(const std::string& line) {
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

static void parse_statement_expression(AstProgram& program, AstStatement& st) {
  std::string expr_text;
  if (st.kind == StatementKind::Let) {
    expr_text = expression_from_let(st.text);
    if (expr_text.empty()) {
      st.has_expression = true;
      st.expression_valid = false;
      st.expression_error = "let statement requires assignment expression";
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
  } else {
    expr_text = trim(st.text);
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
    if (starts_with(line.clean, "func ")) {
      AstFunction fn;
      fn.name = function_name_from_header(line.clean);
      fn.return_type = return_type_from_header(line.clean);
      fn.header_line = line.number;
      fn.header_indent = line.indent;

      if (!ends_with(line.clean, ":")) {
        add_parse_error(program, line.number, "function header must be colon-terminated");
      }
      if (fn.name.empty()) {
        add_parse_error(program, line.number, "invalid function header");
      }
      if (fn.return_type.empty()) {
        add_parse_error(program, line.number, "function header requires explicit return type");
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
        AstStatement st;
        st.text = body.clean;
        st.line = body.number;
        if (starts_with(body.clean, "if ")) {
          st.kind = StatementKind::If;
          if (!valid_control_header("if", body.clean)) {
            add_parse_error(program, body.number, "if requires parentheses and trailing ':'");
          }
        } else if (starts_with(body.clean, "while ")) {
          st.kind = StatementKind::While;
          if (!valid_control_header("while", body.clean)) {
            add_parse_error(program, body.number, "while requires parentheses and trailing ':'");
          }
        } else if (starts_with(body.clean, "return ")) {
          st.kind = StatementKind::Return;
          if (fn.name == "main") {
            program.main_return_literal = parse_return_literal(body.clean);
          }
        } else if (starts_with(body.clean, "let ")) {
          st.kind = StatementKind::Let;
        } else {
          st.kind = StatementKind::Expr;
        }
        parse_statement_expression(program, st);
        fn.body.push_back(st);
        ++i;
      }

      program.functions.push_back(std::move(fn));
      continue;
    }

    if (starts_with(line.clean, "import ")) {
      program.imports.push_back(line.clean);
      ++i;
      continue;
    }
    if (starts_with(line.clean, "extern ")) {
      program.extern_decls.push_back(line.clean);
      ++i;
      continue;
    }
    if (starts_with(line.clean, "struct ")) {
      if (!ends_with(line.clean, ":")) {
        add_parse_error(program, line.number, "struct header must be colon-terminated");
      }
      program.structs.push_back(line.clean);
      ++i;
      continue;
    }
    if (starts_with(line.clean, "impl ")) {
      if (!ends_with(line.clean, ":")) {
        add_parse_error(program, line.number, "impl header must be colon-terminated");
      }
      program.impls.push_back(line.clean);
      ++i;
      continue;
    }

    ++i;
  }

  return program;
}

}  // namespace thagc::syntax
