#include "thagc/frontend/typechecker.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "thagc/frontend/types.hpp"

namespace thagc::semantics {

static bool starts_with(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

static bool is_supported_type(const std::string& type_name) {
  return type_name == "i32" || type_name == "f32" || type_name == "f64" || type_name == "bool" ||
         type_name == "string" || type_name == "String" || type_name == "ptr" || type_name == "void";
}

static std::string type_name(TypeKind kind) {
  if (kind == TypeKind::I32) return "i32";
  if (kind == TypeKind::F32) return "f32";
  if (kind == TypeKind::F64) return "f64";
  if (kind == TypeKind::Bool) return "bool";
  if (kind == TypeKind::String) return "String";
  if (kind == TypeKind::Ptr) return "ptr";
  if (kind == TypeKind::Void) return "void";
  return "unknown";
}

static TypeKind parse_type_name(const std::string& type_name) {
  if (type_name == "i32") return TypeKind::I32;
  if (type_name == "f32") return TypeKind::F32;
  if (type_name == "f64") return TypeKind::F64;
  if (type_name == "bool") return TypeKind::Bool;
  if (type_name == "string" || type_name == "String") return TypeKind::String;
  if (type_name == "ptr") return TypeKind::Ptr;
  if (type_name == "void") return TypeKind::Void;
  return TypeKind::Unknown;
}

static std::unordered_map<std::string, TypeKind> collect_type_aliases(const syntax::AstProgram& program) {
  std::unordered_map<std::string, TypeKind> aliases;
  for (const std::string& line : program.type_aliases) {
    if (!starts_with(line, "type ")) {
      continue;
    }
    const std::size_t eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    std::string name = line.substr(5, eq - 5);
    std::string target = line.substr(eq + 1);
    name.erase(name.begin(), std::find_if(name.begin(), name.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    name.erase(std::find_if(name.rbegin(), name.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(),
               name.end());
    target.erase(target.begin(),
                 std::find_if(target.begin(), target.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    target.erase(std::find_if(target.rbegin(), target.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(),
                 target.end());
    const TypeKind tk = parse_type_name(target);
    if (!name.empty() && tk != TypeKind::Unknown) {
      aliases[name] = tk;
    }
  }
  return aliases;
}

static TypeKind resolve_declared_type(const std::string& type_name,
                                      const std::unordered_map<std::string, TypeKind>& aliases) {
  const TypeKind direct = parse_type_name(type_name);
  if (direct != TypeKind::Unknown) {
    return direct;
  }
  auto it = aliases.find(type_name);
  if (it != aliases.end()) {
    return it->second;
  }
  return TypeKind::Unknown;
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
  const std::unordered_map<std::string, int>* enum_variants = nullptr;
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
        error = "invalid numeric literal";
        return {};
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

static bool is_integer_atom(const std::string& atom) {
  if (atom.empty()) return false;
  for (char ch : atom) {
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return false;
    }
  }
  return true;
}

static bool is_float_atom(const std::string& atom) {
  if (atom.empty()) {
    return false;
  }
  bool seen_dot = false;
  bool seen_digit = false;
  for (char ch : atom) {
    if (std::isdigit(static_cast<unsigned char>(ch))) {
      seen_digit = true;
      continue;
    }
    if (ch == '.' && !seen_dot) {
      seen_dot = true;
      continue;
    }
    return false;
  }
  return seen_dot && seen_digit;
}

static bool is_string_atom(const std::string& atom) {
  return atom.size() >= 2 && atom.front() == '"' && atom.back() == '"';
}

static TypeKind parse_expr_type(ExprTypeCursor& cursor);

static TypeKind parse_atom_type(ExprTypeCursor& cursor) {
  const ExprTok& tok = cur(cursor);
  if (tok.kind == ExprTokKind::Atom) {
    ++cursor.index;
    if (tok.text == "true" || tok.text == "false") {
      return TypeKind::Bool;
    }
    if (is_integer_atom(tok.text)) {
      return TypeKind::I32;
    }
    if (is_float_atom(tok.text)) {
      return TypeKind::F32;
    }
    if (is_string_atom(tok.text)) {
      return TypeKind::String;
    }
    auto it = cursor.scope->find(tok.text);
    if (it != cursor.scope->end()) {
      return it->second;
    }
    if (cursor.enum_variants != nullptr) {
      auto enum_it = cursor.enum_variants->find(tok.text);
      if (enum_it != cursor.enum_variants->end()) {
        return TypeKind::I32;
      }
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

static bool is_numeric_type(TypeKind kind) {
  return kind == TypeKind::I32 || kind == TypeKind::F32 || kind == TypeKind::F64;
}

static TypeKind combine_numeric(TypeKind lhs, TypeKind rhs) {
  if (lhs == TypeKind::F64 || rhs == TypeKind::F64) {
    return TypeKind::F64;
  }
  if (lhs == TypeKind::F32 || rhs == TypeKind::F32) {
    return TypeKind::F32;
  }
  if (lhs == TypeKind::I32 && rhs == TypeKind::I32) {
    return TypeKind::I32;
  }
  return TypeKind::Unknown;
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
      if (!is_numeric_type(lhs) || !is_numeric_type(rhs)) {
        cursor.error = "operator '" + op + "' requires numeric operands";
        return TypeKind::Unknown;
      }
      lhs = combine_numeric(lhs, rhs);
      if (lhs == TypeKind::Unknown) {
        cursor.error = "cannot combine numeric operands for operator '" + op + "'";
        return TypeKind::Unknown;
      }
      continue;
    }
    if (is_comparison_op(op)) {
      if (!is_numeric_type(lhs) || !is_numeric_type(rhs)) {
        cursor.error = "operator '" + op + "' requires numeric operands";
        return TypeKind::Unknown;
      }
      lhs = TypeKind::Bool;
      continue;
    }
    if (is_equality_op(op)) {
      if (lhs != rhs) {
        cursor.error = "operator '" + op + "' requires operands of same type";
        return TypeKind::Unknown;
      }
      lhs = TypeKind::Bool;
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

static std::string classify_parse_error(const std::string& message) {
  if (message.find("for header") != std::string::npos) {
    return "E_TYPE_RANGE_HEADER";
  }
  if (message.find("if requires") != std::string::npos) {
    return "E_TYPE_IF_EXPR";
  }
  if (message.find("closure") != std::string::npos) {
    return "E_TYPE_CLOSURE";
  }
  return "E0010";
}

static bool validate_feature_edges(const syntax::AstProgram& program, support::DiagnosticSink& diag) {
  if (program.match_count > 0 && program.enums.empty()) {
    diag.error("E_TYPE_MATCH_MISSING_ENUM", "match expression requires at least one enum declaration");
    return false;
  }

  if (program.range_loop_count > 0) {
    bool has_valid_range_loop = false;
    for (const std::string& line : program.top_level_lines) {
      if (starts_with(line, "for ") && line.find(" in ") != std::string::npos && line.find("..") != std::string::npos &&
          line.find(':') != std::string::npos) {
        has_valid_range_loop = true;
        break;
      }
    }
    if (!has_valid_range_loop) {
      diag.error("E_TYPE_RANGE_HEADER", "malformed for-range header");
      return false;
    }
  }

  if (!program.impl_for_headers.empty() && program.traits.empty()) {
    diag.error("E_TYPE_TRAIT_CONSTRAINT", "impl for requires at least one trait declaration");
    return false;
  }

  for (const std::string& header : program.impl_for_headers) {
    std::string clean = header;
    if (starts_with(clean, "impl ") && clean.size() > 6 && clean.back() == ':') {
      clean = clean.substr(5, clean.size() - 6);
    }
    const std::size_t for_pos = clean.find(" for ");
    if (for_pos == std::string::npos) {
      continue;
    }
    std::string trait_name = clean.substr(0, for_pos);
    std::string type_name = clean.substr(for_pos + 5);
    trait_name.erase(trait_name.begin(),
                     std::find_if(trait_name.begin(), trait_name.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    trait_name.erase(
        std::find_if(trait_name.rbegin(), trait_name.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(),
        trait_name.end());
    type_name.erase(type_name.begin(),
                    std::find_if(type_name.begin(), type_name.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    type_name.erase(
        std::find_if(type_name.rbegin(), type_name.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(),
        type_name.end());
    const std::string key = trait_name + "|" + type_name;

    auto trait_it = program.trait_required_methods.find(trait_name);
    if (trait_it == program.trait_required_methods.end()) {
      diag.error("E_TYPE_TRAIT_CONSTRAINT", "impl references unknown trait '" + trait_name + "'");
      return false;
    }
    auto impl_it = program.impl_for_methods.find(key);
    if (impl_it == program.impl_for_methods.end()) {
      diag.error("E_TYPE_TRAIT_CONSTRAINT",
                 "impl for '" + trait_name + "' on '" + type_name + "' does not define methods");
      return false;
    }
    for (const std::string& required : trait_it->second) {
      if (std::find(impl_it->second.begin(), impl_it->second.end(), required) == impl_it->second.end()) {
        diag.error("E_TYPE_TRAIT_CONSTRAINT",
                   "impl for '" + trait_name + "' on '" + type_name + "' missing method '" + required + "'");
        return false;
      }
    }
  }

  for (const std::string& line : program.top_level_lines) {
    const std::string key = "goal:";
    const std::size_t pos = line.find(key);
    if (pos == std::string::npos) {
      continue;
    }
    std::string goal = line.substr(pos + key.size());
    goal.erase(goal.begin(),
               std::find_if(goal.begin(), goal.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    goal.erase(std::find_if(goal.rbegin(), goal.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(),
               goal.end());
    if (goal == "auto_plan" || goal == "reduce_sum" || goal == "off") {
      continue;
    }
    diag.error("E_TYPE_INTENT_GOAL", "unsupported intent goal: '" + goal + "'");
    return false;
  }

  return true;
}

static bool validate_extern_declarations(const syntax::AstProgram& program, support::DiagnosticSink& diag) {
  const auto aliases = collect_type_aliases(program);
  for (const std::string& decl : program.extern_decls) {
    if (!starts_with(decl, "extern func ")) {
      diag.error("E0007", "unsupported extern declaration: '" + decl + "'");
      return false;
    }
    const std::size_t lparen = decl.find('(');
    const std::size_t rparen = decl.rfind(')');
    if (lparen == std::string::npos || rparen == std::string::npos || lparen >= rparen) {
      diag.error("E0008", "malformed extern declaration: '" + decl + "'");
      return false;
    }
    const std::size_t arrow = decl.find("->", rparen);
    if (arrow == std::string::npos) {
      diag.error("E0009", "extern declaration missing return type: '" + decl + "'");
      return false;
    }
    std::string ret = decl.substr(arrow + 2);
    ret.erase(ret.begin(), std::find_if(ret.begin(), ret.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    ret.erase(std::find_if(ret.rbegin(), ret.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(),
              ret.end());
    if (resolve_declared_type(ret, aliases) == TypeKind::Unknown) {
      diag.error("E0006", "unsupported extern return type '" + ret + "'");
      return false;
    }
  }
  return true;
}

static bool typecheck_statement_expression(const syntax::AstStatement& st, int line,
                                           const std::unordered_map<std::string, TypeKind>& scope,
                                           const std::unordered_map<std::string, int>& enum_variants,
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
  cursor.enum_variants = &enum_variants;
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

static int previous_statement_same_indent(const std::vector<syntax::AstStatement>& statements, std::size_t from_index,
                                          int indent) {
  if (from_index == 0) {
    return -1;
  }
  for (std::size_t i = from_index; i > 0; --i) {
    const std::size_t prev = i - 1;
    if (statements[prev].indent == indent) {
      return static_cast<int>(prev);
    }
  }
  return -1;
}

static bool validate_for_range_expression(const syntax::AstStatement& st,
                                          const std::unordered_map<std::string, TypeKind>& scope,
                                          std::string& error) {
  if (!st.has_expression || !st.expression_valid) {
    error = st.expression_error.empty() ? "invalid for-range expression" : st.expression_error;
    return false;
  }
  const std::string expr = st.expression_normalized;
  const std::size_t dots = expr.find("..");
  if (dots == std::string::npos) {
    error = "for-range expression must contain '..'";
    return false;
  }
  const std::string left = expr.substr(0, dots);
  const std::string right = expr.substr(dots + 2);

  auto infer = [&](const std::string& part, TypeKind& out_type) {
    std::string tok_error;
    ExprTypeCursor cursor;
    cursor.tokens = tokenize_expr(part, tok_error);
    cursor.scope = &scope;
    if (!tok_error.empty()) {
      error = tok_error;
      return false;
    }
    out_type = parse_expr_type(cursor);
    if (out_type == TypeKind::Unknown) {
      error = cursor.error.empty() ? "cannot infer range bound type" : cursor.error;
      return false;
    }
    if (cur(cursor).kind != ExprTokKind::End) {
      error = "unexpected token in range bound";
      return false;
    }
    return true;
  };

  TypeKind left_type = TypeKind::Unknown;
  TypeKind right_type = TypeKind::Unknown;
  if (!infer(left, left_type) || !infer(right, right_type)) {
    return false;
  }
  if (left_type != TypeKind::I32 || right_type != TypeKind::I32) {
    error = "for-range bounds must be i32";
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
      diag.error(classify_parse_error(message), "syntax error: " + message);
    }
    return false;
  }
  if (!validate_feature_edges(program, diag)) {
    return false;
  }
  if (!validate_extern_declarations(program, diag)) {
    return false;
  }
  const auto aliases = collect_type_aliases(program);
  if (!program.has_main && program.top_level_statements.empty()) {
    diag.error("E0002", "missing entrypoint: define func main() or provide top-level executable statements");
    return false;
  }
  for (const auto& fn : program.functions) {
    if (fn.name.empty()) {
      diag.error("E0003", "invalid function header at line " + std::to_string(fn.header_line));
      return false;
    }
    if (!fn.return_type.empty() && resolve_declared_type(fn.return_type, aliases) == TypeKind::Unknown) {
      diag.error("E0006", "unsupported return type '" + fn.return_type + "' in function '" + fn.name + "'");
      return false;
    }
  }

  for (const auto& fn : program.functions) {
    std::unordered_map<std::string, TypeKind> scope;
    std::optional<TypeKind> inferred_return;
    bool has_return = false;

    for (std::size_t st_index = 0; st_index < fn.body.size(); ++st_index) {
      const auto& st = fn.body[st_index];
      TypeKind expr_type = TypeKind::Void;
      std::string expr_error;
      if (!typecheck_statement_expression(st, st.line, scope, program.enum_variant_tags, expr_type, expr_error)) {
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
        if (expr_type != TypeKind::Bool) {
          diag.error("E0014", "line " + std::to_string(st.line) + ": condition expression must be bool");
          return false;
        }
      }
      if (st.kind == syntax::StatementKind::Match) {
        if (expr_type != TypeKind::I32 && expr_type != TypeKind::Bool) {
          diag.error("E_TYPE_MATCH_MISSING_ENUM",
                     "line " + std::to_string(st.line) + ": match expression must be i32/bool-compatible");
          return false;
        }
      }
      if (st.kind == syntax::StatementKind::Else) {
        const int prev = previous_statement_same_indent(fn.body, st_index, st.indent);
        if (prev < 0 || fn.body[static_cast<std::size_t>(prev)].kind != syntax::StatementKind::If) {
          diag.error("E0019", "line " + std::to_string(st.line) + ": else must follow if at same indentation");
          return false;
        }
      }
      if (st.kind == syntax::StatementKind::For) {
        if (!validate_for_range_expression(st, scope, expr_error)) {
          diag.error("E_TYPE_RANGE_HEADER", "line " + std::to_string(st.line) + ": " + expr_error);
          return false;
        }
      }

      if (st.kind == syntax::StatementKind::Return) {
        has_return = true;
        const TypeKind ret_type = st.has_expression ? expr_type : TypeKind::Void;
        if (!inferred_return.has_value()) {
          inferred_return = ret_type;
        } else if (*inferred_return != ret_type) {
          diag.error("E0016", "line " + std::to_string(st.line) + ": inconsistent return types in function '" +
                                  fn.name + "' (expected " + type_name(*inferred_return) + ", got " +
                                  type_name(ret_type) + ")");
          return false;
        }
      }
    }

    const TypeKind effective_return = inferred_return.value_or(TypeKind::Void);
    const TypeKind declared_return =
        fn.return_type.empty() ? TypeKind::Unknown : resolve_declared_type(fn.return_type, aliases);
    if (declared_return != TypeKind::Unknown && declared_return != effective_return) {
      diag.error("E0015",
                 "function '" + fn.name + "' declared return " + type_name(declared_return) +
                     " but inferred " + type_name(effective_return));
      return false;
    }
    if (fn.name == "main" && effective_return != TypeKind::I32) {
      diag.error("E0005", "main must return i32");
      return false;
    }
    if (has_return && effective_return == TypeKind::Unknown) {
      diag.error("E0017", "cannot infer return type for function '" + fn.name + "'");
      return false;
    }
  }

  std::unordered_map<std::string, TypeKind> top_scope;
  for (const auto& st : program.top_level_statements) {
    TypeKind expr_type = TypeKind::Void;
    std::string expr_error;
    if (!typecheck_statement_expression(st, st.line, top_scope, program.enum_variant_tags, expr_type, expr_error)) {
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
      top_scope[name] = expr_type;
    }
    if (st.kind == syntax::StatementKind::If || st.kind == syntax::StatementKind::While) {
      if (expr_type != TypeKind::Bool) {
        diag.error("E0014", "line " + std::to_string(st.line) + ": condition expression must be bool");
        return false;
      }
    }
    if (st.kind == syntax::StatementKind::Match) {
      if (expr_type != TypeKind::I32 && expr_type != TypeKind::Bool) {
        diag.error("E_TYPE_MATCH_MISSING_ENUM",
                   "line " + std::to_string(st.line) + ": match expression must be i32/bool-compatible");
        return false;
      }
    }
    if (st.kind == syntax::StatementKind::For) {
      if (!validate_for_range_expression(st, top_scope, expr_error)) {
        diag.error("E_TYPE_RANGE_HEADER", "line " + std::to_string(st.line) + ": " + expr_error);
        return false;
      }
    }
    if (st.kind == syntax::StatementKind::Return) {
      diag.error("E0018", "line " + std::to_string(st.line) + ": top-level return is not allowed");
      return false;
    }
  }

  return true;
}

}  // namespace thagc::semantics
