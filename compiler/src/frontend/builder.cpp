#include "thagc/frontend/typechecker.hpp"

#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

#include "thagc/frontend/types.hpp"

namespace thagc::semantics {

static bool is_supported_type(const std::string& type_name) {
  return type_name == "i32" || type_name == "string" || type_name == "void";
}

static TypeKind type_from_name(const std::string& type_name) {
  if (type_name == "i32") return TypeKind::I32;
  if (type_name == "string") return TypeKind::String;
  if (type_name == "void") return TypeKind::Void;
  return TypeKind::Unknown;
}

static std::string type_name(TypeKind kind) {
  if (kind == TypeKind::I32) return "i32";
  if (kind == TypeKind::String) return "string";
  if (kind == TypeKind::Void) return "void";
  return "unknown";
}

enum class ExprTokKind {
  Atom,
  Op,
  LParen,
  RParen,
  End,
};

struct ExprTok {
  ExprTokKind kind = ExprTokKind::End;
  std::string text;
};

struct ExprTypeCursor {
  std::vector<ExprTok> tokens;
  std::size_t index = 0;
  std::string error;
  int line = 0;
  const std::unordered_map<std::string, TypeKind>* scope = nullptr;
};

static bool is_ident_start(char ch) {
  return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}

static bool is_ident_body(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

static std::vector<ExprTok> tokenize_expr(const std::string& text, std::string& error) {
  std::vector<ExprTok> out;
  for (std::size_t i = 0; i < text.size();) {
    const char ch = text[i];
    if (std::isspace(static_cast<unsigned char>(ch))) {
      ++i;
      continue;
    }
    if (i + 1 < text.size()) {
      const std::string two = text.substr(i, 2);
      if (two == "==" || two == "!=" || two == "<=" || two == ">=") {
        out.push_back(ExprTok{ExprTokKind::Op, two});
        i += 2;
        continue;
      }
    }
    if (ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '<' || ch == '>') {
      out.push_back(ExprTok{ExprTokKind::Op, std::string(1, ch)});
      ++i;
      continue;
    }
    if (ch == '(') {
      out.push_back(ExprTok{ExprTokKind::LParen, "("});
      ++i;
      continue;
    }
    if (ch == ')') {
      out.push_back(ExprTok{ExprTokKind::RParen, ")"});
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
        error = "unterminated string literal";
        return {};
      }
      ++i;
      out.push_back(ExprTok{ExprTokKind::Atom, text.substr(start, i - start)});
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(ch))) {
      std::size_t start = i;
      while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
        ++i;
      }
      out.push_back(ExprTok{ExprTokKind::Atom, text.substr(start, i - start)});
      continue;
    }
    if (is_ident_start(ch)) {
      std::size_t start = i;
      while (i < text.size() && is_ident_body(text[i])) {
        ++i;
      }
      out.push_back(ExprTok{ExprTokKind::Atom, text.substr(start, i - start)});
      continue;
    }
    error = std::string("invalid expression token '") + ch + "'";
    return {};
  }
  out.push_back(ExprTok{ExprTokKind::End, ""});
  return out;
}

static const ExprTok& cur(const ExprTypeCursor& cursor) {
  if (cursor.index >= cursor.tokens.size()) {
    static const ExprTok end{ExprTokKind::End, ""};
    return end;
  }
  return cursor.tokens[cursor.index];
}

static bool is_numeric_atom(const std::string& atom) {
  if (atom.empty()) return false;
  for (char ch : atom) {
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return false;
    }
  }
  return true;
}

static bool is_string_atom(const std::string& atom) {
  return atom.size() >= 2 && atom.front() == '"' && atom.back() == '"';
}

static TypeKind parse_expr_type(ExprTypeCursor& cursor);

static TypeKind parse_atom_type(ExprTypeCursor& cursor) {
  const ExprTok& tok = cur(cursor);
  if (tok.kind == ExprTokKind::Atom) {
    ++cursor.index;
    if (is_numeric_atom(tok.text)) {
      return TypeKind::I32;
    }
    if (is_string_atom(tok.text)) {
      return TypeKind::String;
    }
    auto it = cursor.scope->find(tok.text);
    if (it != cursor.scope->end()) {
      return it->second;
    }
    cursor.error = "unknown identifier '" + tok.text + "'";
    return TypeKind::Unknown;
  }
  if (tok.kind == ExprTokKind::LParen) {
    ++cursor.index;
    const TypeKind inner = parse_expr_type(cursor);
    if (inner == TypeKind::Unknown) {
      return TypeKind::Unknown;
    }
    if (cur(cursor).kind != ExprTokKind::RParen) {
      cursor.error = "missing closing ')' in expression";
      return TypeKind::Unknown;
    }
    ++cursor.index;
    return inner;
  }
  cursor.error = "expected expression atom";
  return TypeKind::Unknown;
}

static bool is_equality_op(const std::string& op) {
  return op == "==" || op == "!=";
}

static bool is_comparison_op(const std::string& op) {
  return op == "<" || op == "<=" || op == ">" || op == ">=";
}

static bool is_arithmetic_op(const std::string& op) {
  return op == "+" || op == "-" || op == "*" || op == "/";
}

static TypeKind parse_expr_type(ExprTypeCursor& cursor) {
  TypeKind lhs = parse_atom_type(cursor);
  if (lhs == TypeKind::Unknown) {
    return TypeKind::Unknown;
  }

  while (cur(cursor).kind == ExprTokKind::Op) {
    const std::string op = cur(cursor).text;
    ++cursor.index;
    const TypeKind rhs = parse_atom_type(cursor);
    if (rhs == TypeKind::Unknown) {
      return TypeKind::Unknown;
    }

    if (is_arithmetic_op(op)) {
      if (lhs != TypeKind::I32 || rhs != TypeKind::I32) {
        cursor.error = "operator '" + op + "' requires i32 operands";
        return TypeKind::Unknown;
      }
      lhs = TypeKind::I32;
      continue;
    }
    if (is_comparison_op(op)) {
      if (lhs != TypeKind::I32 || rhs != TypeKind::I32) {
        cursor.error = "operator '" + op + "' requires i32 operands";
        return TypeKind::Unknown;
      }
      lhs = TypeKind::I32;
      continue;
    }
    if (is_equality_op(op)) {
      if (lhs != rhs) {
        cursor.error = "operator '" + op + "' requires operands of same type";
        return TypeKind::Unknown;
      }
      lhs = TypeKind::I32;
      continue;
    }
    cursor.error = "unsupported operator '" + op + "'";
    return TypeKind::Unknown;
  }

  return lhs;
}

static std::string parse_let_name(const std::string& line) {
  if (line.rfind("let ", 0) != 0) {
    return "";
  }
  std::size_t i = 4;
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
  if (start == i) {
    return "";
  }
  return line.substr(start, i - start);
}

static bool typecheck_statement_expression(const syntax::AstStatement& st, int line,
                                           const std::unordered_map<std::string, TypeKind>& scope,
                                           TypeKind& out, std::string& error) {
  if (!st.has_expression) {
    out = TypeKind::Void;
    return true;
  }
  if (!st.expression_valid) {
    error = st.expression_error.empty() ? "invalid expression" : st.expression_error;
    return false;
  }

  std::string tokenize_error;
  ExprTypeCursor cursor;
  cursor.tokens = tokenize_expr(st.expression_normalized, tokenize_error);
  cursor.line = line;
  cursor.scope = &scope;
  if (!tokenize_error.empty()) {
    error = tokenize_error;
    return false;
  }

  out = parse_expr_type(cursor);
  if (out == TypeKind::Unknown) {
    error = cursor.error.empty() ? "cannot infer expression type" : cursor.error;
    return false;
  }
  if (cur(cursor).kind != ExprTokKind::End) {
    error = "unexpected trailing token in expression";
    return false;
  }
  return true;
}

bool TypeChecker::check(const syntax::AstProgram& program, support::DiagnosticSink& diag) const {
  if (program.source.empty()) {
    diag.error("E0001", "source is empty");
    return false;
  }
  if (!program.parse_errors.empty()) {
    for (const std::string& message : program.parse_errors) {
      diag.error("E0010", "syntax error: " + message);
    }
    return false;
  }
  if (!program.has_main) {
    diag.error("E0002", "missing entry function: func main() -> i32");
    return false;
  }
  for (const auto& fn : program.functions) {
    if (fn.name.empty()) {
      diag.error("E0003", "invalid function header at line " + std::to_string(fn.header_line));
      return false;
    }
    if (fn.return_type.empty()) {
      diag.error("E0004", "missing return type for function '" + fn.name + "'");
      return false;
    }
    if (!is_supported_type(fn.return_type)) {
      diag.error("E0006", "unsupported return type '" + fn.return_type + "' in function '" + fn.name + "'");
      return false;
    }
  }
  for (const auto& fn : program.functions) {
    if (fn.name == "main" && fn.return_type != "i32") {
      diag.error("E0005", "main must return i32");
      return false;
    }
  }

  for (const auto& fn : program.functions) {
    const TypeKind fn_return = type_from_name(fn.return_type);
    std::unordered_map<std::string, TypeKind> scope;
    for (const auto& st : fn.body) {
      TypeKind expr_type = TypeKind::Void;
      std::string expr_error;
      if (!typecheck_statement_expression(st, st.line, scope, expr_type, expr_error)) {
        diag.error("E0011", "line " + std::to_string(st.line) + ": " + expr_error);
        return false;
      }

      if (st.kind == syntax::StatementKind::Let) {
        const std::string name = parse_let_name(st.text);
        if (name.empty()) {
          diag.error("E0012", "line " + std::to_string(st.line) + ": invalid let binding name");
          return false;
        }
        if (expr_type == TypeKind::Void || expr_type == TypeKind::Unknown) {
          diag.error("E0013", "line " + std::to_string(st.line) + ": let binding requires typed value");
          return false;
        }
        scope[name] = expr_type;
      }

      if (st.kind == syntax::StatementKind::If || st.kind == syntax::StatementKind::While) {
        if (expr_type != TypeKind::I32) {
          diag.error("E0014", "line " + std::to_string(st.line) + ": condition expression must be i32");
          return false;
        }
      }

      if (st.kind == syntax::StatementKind::Return) {
        if (fn_return == TypeKind::Void && st.has_expression) {
          diag.error("E0015",
                     "line " + std::to_string(st.line) + ": void function '" + fn.name + "' cannot return value");
          return false;
        }
        if (fn_return != TypeKind::Void && (!st.has_expression || expr_type != fn_return)) {
          diag.error("E0016", "line " + std::to_string(st.line) + ": return type mismatch in function '" + fn.name +
                                  "' (expected " + type_name(fn_return) + ", got " + type_name(expr_type) + ")");
          return false;
        }
      }
    }
  }
  return true;
}

}  // namespace thagc::semantics
