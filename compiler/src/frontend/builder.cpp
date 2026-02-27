#include "thagc/frontend/typechecker.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "thagc/frontend/types.hpp"

namespace thagc::semantics {

static bool starts_with(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

static std::string trim_copy(const std::string& text) {
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

static bool parse_generic_parts(const std::string& type_name, std::string& base, std::vector<std::string>& args) {
  args.clear();
  const std::string clean = trim_copy(type_name);
  const std::size_t lt = clean.find('<');
  const std::size_t gt = clean.rfind('>');
  if (lt == std::string::npos || gt == std::string::npos || gt <= lt + 1) {
    return false;
  }
  base = trim_copy(clean.substr(0, lt));
  const std::string inner = clean.substr(lt + 1, gt - lt - 1);
  std::size_t i = 0;
  while (i < inner.size()) {
    std::size_t comma = inner.find(',', i);
    if (comma == std::string::npos) {
      comma = inner.size();
    }
    const std::string arg = trim_copy(inner.substr(i, comma - i));
    if (!arg.empty()) {
      args.push_back(arg);
    }
    i = comma + 1;
  }
  return !base.empty() && !args.empty();
}

static bool is_tuple_type_syntax(const std::string& type_name) {
  const std::string clean = trim_copy(type_name);
  return clean.size() >= 5 && clean.front() == '(' && clean.back() == ')' && clean.find(',') != std::string::npos;
}

static bool is_array_type_syntax(const std::string& type_name) {
  const std::string clean = trim_copy(type_name);
  return clean.size() >= 5 && clean.front() == '[' && clean.back() == ']' && clean.find(';') != std::string::npos;
}

static bool is_supported_type(const std::string& type_name) {
  if (type_name == "i32" || type_name == "f32" || type_name == "f64" || type_name == "bool" ||
      type_name == "string" || type_name == "String" || type_name == "ptr" || type_name == "void" ||
      type_name == "Option" || type_name == "Result" || type_name == "List" || type_name == "Rc" ||
      type_name == "Arc" || type_name == "Fn") {
    return true;
  }
  if (is_tuple_type_syntax(type_name) || is_array_type_syntax(type_name)) {
    return true;
  }
  std::string base;
  std::vector<std::string> args;
  if (!parse_generic_parts(type_name, base, args)) {
    return false;
  }
  return base == "Option" || base == "Result" || base == "List" || base == "Rc" || base == "Arc";
}

static std::string type_name(TypeKind kind) {
  if (kind == TypeKind::I32) return "i32";
  if (kind == TypeKind::F32) return "f32";
  if (kind == TypeKind::F64) return "f64";
  if (kind == TypeKind::Bool) return "bool";
  if (kind == TypeKind::String) return "String";
  if (kind == TypeKind::Option) return "Option";
  if (kind == TypeKind::Result) return "Result";
  if (kind == TypeKind::List) return "List";
  if (kind == TypeKind::Rc) return "Rc";
  if (kind == TypeKind::Arc) return "Arc";
  if (kind == TypeKind::FunctionType) return "fn";
  if (kind == TypeKind::Ptr) return "ptr";
  if (kind == TypeKind::StructType) return "struct";
  if (kind == TypeKind::EnumType) return "enum";
  if (kind == TypeKind::TupleType) return "tuple";
  if (kind == TypeKind::ArrayType) return "array";
  if (kind == TypeKind::Void) return "void";
  return "unknown";
}

static TypeKind parse_type_name(const std::string& type_name) {
  const std::string clean = trim_copy(type_name);
  if (clean == "i32") return TypeKind::I32;
  if (clean == "f32") return TypeKind::F32;
  if (clean == "f64") return TypeKind::F64;
  if (clean == "bool") return TypeKind::Bool;
  if (clean == "string" || clean == "String") return TypeKind::String;
  if (clean == "Option") return TypeKind::Option;
  if (clean == "Result") return TypeKind::Result;
  if (clean == "List") return TypeKind::List;
  if (clean == "Rc") return TypeKind::Rc;
  if (clean == "Arc") return TypeKind::Arc;
  if (clean == "Fn" || clean == "fn") return TypeKind::FunctionType;
  if (clean == "ptr") return TypeKind::Ptr;
  if (clean == "void") return TypeKind::Void;
  if (is_tuple_type_syntax(clean)) return TypeKind::TupleType;
  if (is_array_type_syntax(clean)) return TypeKind::ArrayType;
  std::string base;
  std::vector<std::string> args;
  if (parse_generic_parts(clean, base, args)) {
    if (base == "Option") return TypeKind::Option;
    if (base == "Result") return TypeKind::Result;
    if (base == "List") return TypeKind::List;
    if (base == "Rc") return TypeKind::Rc;
    if (base == "Arc") return TypeKind::Arc;
  }
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

static std::unordered_set<std::string> collect_struct_names(const syntax::AstProgram& program) {
  std::unordered_set<std::string> out;
  for (const std::string& header : program.structs) {
    if (!starts_with(header, "struct ")) {
      continue;
    }
    std::string clean = header.substr(7);
    if (!clean.empty() && clean.back() == ':') {
      clean.pop_back();
    }
    clean.erase(clean.begin(), std::find_if(clean.begin(), clean.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    clean.erase(std::find_if(clean.rbegin(), clean.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(),
               clean.end());
    if (!clean.empty()) {
      out.insert(clean);
    }
  }
  return out;
}

static std::unordered_set<std::string> collect_enum_names(const syntax::AstProgram& program) {
  std::unordered_set<std::string> out;
  for (const std::string& header : program.enums) {
    if (!starts_with(header, "enum ")) {
      continue;
    }
    std::string clean = header.substr(5);
    if (!clean.empty() && clean.back() == ':') {
      clean.pop_back();
    }
    clean.erase(clean.begin(), std::find_if(clean.begin(), clean.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    clean.erase(std::find_if(clean.rbegin(), clean.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(),
               clean.end());
    if (!clean.empty()) {
      out.insert(clean);
    }
  }
  return out;
}

static std::unordered_map<std::string, TypeKind> collect_function_return_types(
    const syntax::AstProgram& program, const std::unordered_map<std::string, TypeKind>& aliases,
    const std::unordered_set<std::string>& struct_names,
    const std::unordered_set<std::string>& enum_names) {
  std::unordered_map<std::string, TypeKind> out;
  for (const auto& fn : program.functions) {
    if (fn.return_type.empty()) {
      out[fn.name] = fn.name == "main" ? TypeKind::I32 : TypeKind::I32;
      continue;
    }
    const TypeKind direct = resolve_declared_type(fn.return_type, aliases);
    if (direct != TypeKind::Unknown) {
      out[fn.name] = direct;
      continue;
    }
    if (struct_names.find(fn.return_type) != struct_names.end()) {
      out[fn.name] = TypeKind::StructType;
      continue;
    }
    if (enum_names.find(fn.return_type) != enum_names.end()) {
      out[fn.name] = TypeKind::EnumType;
      continue;
    }
    out[fn.name] = TypeKind::Unknown;
  }
  for (const auto& ext : program.extern_functions) {
    const TypeKind direct = resolve_declared_type(ext.return_type, aliases);
    if (direct != TypeKind::Unknown) {
      out[ext.name] = direct;
    } else if (struct_names.find(ext.return_type) != struct_names.end()) {
      out[ext.name] = TypeKind::StructType;
    } else if (enum_names.find(ext.return_type) != enum_names.end()) {
      out[ext.name] = TypeKind::EnumType;
    } else {
      out[ext.name] = TypeKind::Unknown;
    }
  }
  return out;
}

static std::unordered_map<std::string, std::size_t> collect_function_arity(const syntax::AstProgram& program) {
  std::unordered_map<std::string, std::size_t> out;
  for (const auto& fn : program.functions) {
    out[fn.name] = fn.params.size();
  }
  for (const auto& ext : program.extern_functions) {
    out[ext.name] = ext.param_types.size();
  }
  return out;
}

enum class ExprTokKind {
  Atom,
  Op,
  LParen,
  RParen,
  Comma,
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
  const std::unordered_map<std::string, TypeKind>* function_returns = nullptr;
  const std::unordered_map<std::string, std::size_t>* function_arity = nullptr;
  const std::unordered_set<std::string>* struct_names = nullptr;
  const std::unordered_map<std::string, std::string>* struct_bindings = nullptr;
  const std::unordered_map<std::string, std::vector<std::string>>* struct_fields = nullptr;
  const std::unordered_map<std::string, std::string>* struct_field_types = nullptr;
  const std::unordered_map<std::string, std::vector<std::string>>* struct_methods = nullptr;
};

static bool is_ident_start(char ch) {
  return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}

static bool is_ident_body(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

static bool split_dotted_name(const std::string& value, std::string& base, std::string& member) {
  const std::size_t dot = value.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= value.size()) {
    return false;
  }
  if (value.find('.', dot + 1) != std::string::npos) {
    return false;
  }
  base = value.substr(0, dot);
  member = value.substr(dot + 1);
  return true;
}

static bool is_interpolated_literal(const std::string& text) {
  const std::string clean = trim_copy(text);
  if (clean.size() >= 3 && clean[0] == 'v' && clean[1] == '"' && clean.back() == '"') {
    return true;
  }
  return clean.size() >= 2 && clean.front() == '"' && clean.back() == '"' && clean.find('{') != std::string::npos &&
         clean.find('}') != std::string::npos;
}

static bool parse_closure_literal(const std::string& text, std::vector<std::string>& params, std::string& body) {
  const std::string clean = trim_copy(text);
  if (clean.size() < 4 || clean[0] != '|') {
    return false;
  }
  const std::size_t second_bar = clean.find('|', 1);
  if (second_bar == std::string::npos || second_bar <= 1) {
    return false;
  }
  const std::string param_text = trim_copy(clean.substr(1, second_bar - 1));
  body = trim_copy(clean.substr(second_bar + 1));
  params.clear();
  std::size_t i = 0;
  while (i < param_text.size()) {
    std::size_t comma = param_text.find(',', i);
    if (comma == std::string::npos) {
      comma = param_text.size();
    }
    const std::string part = trim_copy(param_text.substr(i, comma - i));
    if (part.empty()) {
      return false;
    }
    params.push_back(part);
    i = comma + 1;
  }
  if (params.empty() || body.empty()) {
    return false;
  }
  for (const std::string& param : params) {
    if (!is_ident_start(param[0])) {
      return false;
    }
    for (std::size_t k = 1; k < param.size(); ++k) {
      if (!is_ident_body(param[k])) {
        return false;
      }
    }
  }
  if (body.size() >= 2 && body.front() == '{' && body.back() == '}') {
    body = trim_copy(body.substr(1, body.size() - 2));
    if (body.empty()) {
      return false;
    }
  }
  return true;
}

static std::vector<ExprTok> tokenize_expr(const std::string& text, std::string& error) {
  std::vector<ExprTok> out;
  std::vector<std::string> closure_params;
  std::string closure_body;
  if (parse_closure_literal(text, closure_params, closure_body)) {
    out.push_back(ExprTok{ExprTokKind::Atom, trim_copy(text)});
    out.push_back(ExprTok{ExprTokKind::End, ""});
    return out;
  }
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
    if (ch == ',') {
      out.push_back(ExprTok{ExprTokKind::Comma, ","});
      ++i;
      continue;
    }
    if (ch == '[') {
      out.push_back(ExprTok{ExprTokKind::LParen, "["});
      ++i;
      continue;
    }
    if (ch == ']') {
      out.push_back(ExprTok{ExprTokKind::RParen, "]"});
      ++i;
      continue;
    }
    if (ch == '?') {
      out.push_back(ExprTok{ExprTokKind::Op, "?"});
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
      if (ch == 'v' && i + 1 < text.size() && text[i + 1] == '"') {
        const std::size_t start = i;
        i += 2;
        while (i < text.size() && text[i] != '"') {
          if (text[i] == '\\' && i + 1 < text.size()) {
            i += 2;
            continue;
          }
          ++i;
        }
        if (i >= text.size() || text[i] != '"') {
          error = "unterminated interpolated string literal";
          return {};
        }
        ++i;
        out.push_back(ExprTok{ExprTokKind::Atom, text.substr(start, i - start)});
        continue;
      }
      std::size_t start = i;
      while (i < text.size()) {
        if (is_ident_body(text[i])) {
          ++i;
          continue;
        }
        if (text[i] == '.' && i + 1 < text.size() &&
            (is_ident_start(text[i + 1]) || std::isdigit(static_cast<unsigned char>(text[i + 1])))) {
          ++i;
          continue;
        }
        break;
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
    if (cur(cursor).kind == ExprTokKind::LParen) {
      ++cursor.index;  // '('
      std::vector<TypeKind> args;
      if (cur(cursor).kind != ExprTokKind::RParen) {
        while (true) {
          const TypeKind arg_type = parse_expr_type(cursor);
          if (arg_type == TypeKind::Unknown) {
            return TypeKind::Unknown;
          }
          args.push_back(arg_type);
          if (cur(cursor).kind == ExprTokKind::Comma) {
            ++cursor.index;
            continue;
          }
          break;
        }
      }
      if (cur(cursor).kind != ExprTokKind::RParen) {
        cursor.error = "missing closing ')' in call expression";
        return TypeKind::Unknown;
      }
      ++cursor.index;
      if (tok.text == "Some" || tok.text == "None") {
        return TypeKind::Option;
      }
      if (tok.text == "Ok" || tok.text == "Err") {
        return TypeKind::Result;
      }
      if (tok.text == "is_some" || tok.text == "is_none" || tok.text == "is_ok" || tok.text == "is_err") {
        if (args.size() != 1) {
          cursor.error = "predicate '" + tok.text + "' expects 1 argument";
          return TypeKind::Unknown;
        }
        return TypeKind::Bool;
      }
      if (tok.text == "unwrap") {
        if (args.size() != 1) {
          cursor.error = "unwrap() expects 1 argument";
          return TypeKind::Unknown;
        }
        return TypeKind::I32;
      }
      if (tok.text == "unwrap_or") {
        if (args.size() != 2) {
          cursor.error = "unwrap_or() expects 2 arguments";
          return TypeKind::Unknown;
        }
        return args[1];
      }
      if (tok.text == "open" || tok.text == "close" || tok.text == "read" || tok.text == "write") {
        return TypeKind::I32;
      }
      if (tok.text == "len") {
        if (args.size() != 1) {
          cursor.error = "len() expects 1 argument";
          return TypeKind::Unknown;
        }
        return TypeKind::I32;
      }
      if (tok.text == "print") {
        return TypeKind::I32;
      }
      if (cursor.enum_variants != nullptr) {
        auto enum_it = cursor.enum_variants->find(tok.text);
        if (enum_it != cursor.enum_variants->end()) {
          if (args.size() > 1) {
            cursor.error = "enum payload constructor '" + tok.text + "' expects at most 1 argument";
            return TypeKind::Unknown;
          }
          return TypeKind::EnumType;
        }
      }
      std::string call_base;
      std::string call_member;
      if (split_dotted_name(tok.text, call_base, call_member) && cursor.struct_bindings != nullptr &&
          cursor.struct_methods != nullptr && cursor.function_returns != nullptr) {
        auto binding = cursor.struct_bindings->find(call_base);
        if (binding != cursor.struct_bindings->end()) {
          const std::string& struct_name = binding->second;
          auto methods_it = cursor.struct_methods->find(struct_name);
          if (methods_it == cursor.struct_methods->end() ||
              std::find(methods_it->second.begin(), methods_it->second.end(), call_member) ==
                  methods_it->second.end()) {
            cursor.error = "unknown method '" + call_member + "' on struct '" + struct_name + "'";
            return TypeKind::Unknown;
          }
          const std::string method_symbol = struct_name + "." + call_member;
          auto ret = cursor.function_returns->find(method_symbol);
          if (ret == cursor.function_returns->end()) {
            cursor.error = "method '" + method_symbol + "' is declared but has no implementation";
            return TypeKind::Unknown;
          }
          if (cursor.function_arity != nullptr) {
            auto arity = cursor.function_arity->find(method_symbol);
            if (arity != cursor.function_arity->end() && args.size() + 1 != arity->second) {
              cursor.error = "method '" + call_member + "' expects " + std::to_string(arity->second - 1) +
                             " arguments but got " + std::to_string(args.size());
              return TypeKind::Unknown;
            }
          }
          return ret->second;
        }
      }
      if (cursor.function_returns != nullptr) {
        auto fn = cursor.function_returns->find(tok.text);
        if (fn != cursor.function_returns->end()) {
          if (cursor.function_arity != nullptr) {
            auto arity = cursor.function_arity->find(tok.text);
            if (arity != cursor.function_arity->end() && args.size() != arity->second) {
              cursor.error = "function '" + tok.text + "' expects " + std::to_string(arity->second) +
                             " arguments but got " + std::to_string(args.size());
              return TypeKind::Unknown;
            }
          }
          return fn->second;
        }
      }
      if (cursor.scope != nullptr) {
        auto local_callable = cursor.scope->find(tok.text);
        if (local_callable != cursor.scope->end() && local_callable->second == TypeKind::FunctionType) {
          return TypeKind::I32;
        }
      }
      if (cursor.struct_names != nullptr && cursor.struct_names->find(tok.text) != cursor.struct_names->end()) {
        return TypeKind::StructType;
      }
      cursor.error = "unknown callable '" + tok.text + "'";
      return TypeKind::Unknown;
    }
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
    if (is_interpolated_literal(tok.text)) {
      return TypeKind::String;
    }
    std::vector<std::string> closure_params;
    std::string closure_body;
    if (parse_closure_literal(tok.text, closure_params, closure_body)) {
      return TypeKind::FunctionType;
    }
    std::string field_base;
    std::string field_name;
    if (split_dotted_name(tok.text, field_base, field_name) && cursor.struct_bindings != nullptr &&
        cursor.struct_fields != nullptr) {
      auto binding = cursor.struct_bindings->find(field_base);
      if (binding != cursor.struct_bindings->end()) {
        const std::string& struct_name = binding->second;
        auto fields_it = cursor.struct_fields->find(struct_name);
        if (fields_it == cursor.struct_fields->end() ||
            std::find(fields_it->second.begin(), fields_it->second.end(), field_name) == fields_it->second.end()) {
          cursor.error = "unknown field '" + field_name + "' on struct '" + struct_name + "'";
          return TypeKind::Unknown;
        }
        if (cursor.scope != nullptr) {
          auto known = cursor.scope->find(tok.text);
          if (known != cursor.scope->end()) {
            return known->second;
          }
        }
        if (cursor.struct_field_types != nullptr) {
          auto field_type_it = cursor.struct_field_types->find(struct_name + "." + field_name);
          if (field_type_it != cursor.struct_field_types->end()) {
            const TypeKind resolved = parse_type_name(field_type_it->second);
            return resolved == TypeKind::Unknown ? TypeKind::I32 : resolved;
          }
        }
        return TypeKind::I32;
      }
    }
    if (split_dotted_name(tok.text, field_base, field_name) && cursor.scope != nullptr) {
      auto base_it = cursor.scope->find(field_base);
      if (base_it != cursor.scope->end() && base_it->second == TypeKind::TupleType) {
        bool all_digit = !field_name.empty();
        for (char ch : field_name) {
          if (!std::isdigit(static_cast<unsigned char>(ch))) {
            all_digit = false;
            break;
          }
        }
        if (all_digit) {
          return TypeKind::I32;
        }
      }
    }
    if (cursor.scope != nullptr) {
      auto it = cursor.scope->find(tok.text);
      if (it != cursor.scope->end()) {
        return it->second;
      }
    }
    if (cursor.enum_variants != nullptr) {
      auto enum_it = cursor.enum_variants->find(tok.text);
      if (enum_it != cursor.enum_variants->end()) {
        return TypeKind::EnumType;
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
    bool is_tuple = false;
    while (cur(cursor).kind == ExprTokKind::Comma) {
      is_tuple = true;
      ++cursor.index;
      const TypeKind tuple_item = parse_expr_type(cursor);
      if (tuple_item == TypeKind::Unknown) {
        return TypeKind::Unknown;
      }
    }
    if (cur(cursor).kind != ExprTokKind::RParen) {
      cursor.error = "missing closing ')' in expression";
      return TypeKind::Unknown;
    }
    ++cursor.index;
    if (is_tuple) {
      return TypeKind::TupleType;
    }
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

  while (cur(cursor).kind == ExprTokKind::Op && cur(cursor).text == "?") {
    ++cursor.index;
    if (lhs != TypeKind::Result && lhs != TypeKind::Option) {
      cursor.error = "operator '?' requires Result/Option operand";
      return TypeKind::Unknown;
    }
    lhs = TypeKind::I32;
  }

  while (cur(cursor).kind == ExprTokKind::Op) {
    const std::string op = cur(cursor).text;
    if (op == "?") {
      ++cursor.index;
      if (lhs != TypeKind::Result && lhs != TypeKind::Option) {
        cursor.error = "operator '?' requires Result/Option operand";
        return TypeKind::Unknown;
      }
      lhs = TypeKind::I32;
      continue;
    }
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

static bool parse_let_tuple_bindings(const std::string& line, std::vector<std::string>& names) {
  names.clear();
  if (line.rfind("let ", 0) != 0) {
    return false;
  }
  std::size_t i = 4;
  while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
    ++i;
  }
  if (i >= line.size() || line[i] != '(') {
    return false;
  }
  const std::size_t close = line.find(')', i + 1);
  const std::size_t eq = line.find('=', close == std::string::npos ? i : close + 1);
  if (close == std::string::npos || eq == std::string::npos || close >= eq) {
    return false;
  }
  std::string content = line.substr(i + 1, close - i - 1);
  std::size_t from = 0;
  while (from < content.size()) {
    std::size_t comma = content.find(',', from);
    if (comma == std::string::npos) {
      comma = content.size();
    }
    std::string part = trim_copy(content.substr(from, comma - from));
    if (part.empty() || !is_ident_start(part[0])) {
      return false;
    }
    for (std::size_t k = 1; k < part.size(); ++k) {
      if (!is_ident_body(part[k])) {
        return false;
      }
    }
    names.push_back(part);
    from = comma + 1;
  }
  return !names.empty();
}

static std::string parse_assignment_target(const std::string& line) {
  const std::size_t eq = line.find('=');
  if (eq == std::string::npos) {
    return "";
  }
  std::string out = line.substr(0, eq);
  out.erase(out.begin(), std::find_if(out.begin(), out.end(), [](unsigned char ch) { return !std::isspace(ch); }));
  out.erase(std::find_if(out.rbegin(), out.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(),
            out.end());
  if (out.empty() || !is_ident_start(out[0])) {
    return "";
  }
  for (std::size_t i = 1; i < out.size(); ++i) {
    const char ch = out[i];
    if (ch == '.') {
      if (i + 1 >= out.size() || !is_ident_start(out[i + 1])) {
        return "";
      }
      continue;
    }
    if (!is_ident_body(ch)) {
      return "";
    }
  }
  return out;
}

static bool split_field_target(const std::string& target, std::string& base, std::string& field) {
  const std::size_t dot = target.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= target.size()) {
    return false;
  }
  if (target.find('.', dot + 1) != std::string::npos) {
    return false;
  }
  base = target.substr(0, dot);
  field = target.substr(dot + 1);
  return true;
}

static std::string parse_match_payload_binding_label(const std::string& line) {
  const std::string clean = trim_copy(line);
  if (clean.empty() || clean.back() != ':') {
    return "";
  }
  const std::string label = trim_copy(clean.substr(0, clean.size() - 1));
  const std::size_t lparen = label.find('(');
  const std::size_t rparen = label.rfind(')');
  if (lparen == std::string::npos || rparen == std::string::npos || rparen <= lparen + 1) {
    return "";
  }
  std::string binding = trim_copy(label.substr(lparen + 1, rparen - lparen - 1));
  if (binding.empty() || binding == "_") {
    return "";
  }
  if (!is_ident_start(binding[0])) {
    return "";
  }
  for (std::size_t i = 1; i < binding.size(); ++i) {
    if (!is_ident_body(binding[i])) {
      return "";
    }
  }
  return binding;
}

enum class OwnershipKind {
  None,
  Rc,
  Arc,
};

static bool is_word_boundary(char ch) {
  return !std::isalnum(static_cast<unsigned char>(ch)) && ch != '_';
}

static bool contains_word(const std::string& text, const std::string& word) {
  if (word.empty() || text.size() < word.size()) {
    return false;
  }
  std::size_t pos = text.find(word);
  while (pos != std::string::npos) {
    const bool left_ok = (pos == 0) || is_word_boundary(text[pos - 1]);
    const std::size_t right = pos + word.size();
    const bool right_ok = (right >= text.size()) || is_word_boundary(text[right]);
    if (left_ok && right_ok) {
      return true;
    }
    pos = text.find(word, pos + 1);
  }
  return false;
}

static OwnershipKind detect_ownership_from_line(const std::string& line) {
  if (line.find("Arc<") != std::string::npos || line.find(": Arc") != std::string::npos ||
      line.find("Arc::new(") != std::string::npos ||
      line.find("Arc.new(") != std::string::npos) {
    return OwnershipKind::Arc;
  }
  if (line.find("Rc<") != std::string::npos || line.find(": Rc") != std::string::npos ||
      line.find("Rc::new(") != std::string::npos ||
      line.find("Rc.new(") != std::string::npos) {
    return OwnershipKind::Rc;
  }
  return OwnershipKind::None;
}

static bool is_spawn_statement(const std::string& line) {
  return line.find("spawn(") != std::string::npos || starts_with(line, "spawn ");
}

static std::string extract_call_arg_ident(const std::string& line, const std::string& callee) {
  const std::size_t call = line.find(callee + "(");
  if (call == std::string::npos) {
    return "";
  }
  std::size_t i = call + callee.size() + 1;
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

static std::string parse_let_annotation(const std::string& line) {
  if (!starts_with(line, "let ")) {
    return "";
  }
  const std::size_t eq = line.find('=');
  if (eq == std::string::npos) {
    return "";
  }
  const std::size_t colon = line.find(':');
  if (colon == std::string::npos || colon > eq) {
    return "";
  }
  std::string out = line.substr(colon + 1, eq - colon - 1);
  out.erase(out.begin(), std::find_if(out.begin(), out.end(), [](unsigned char ch) { return !std::isspace(ch); }));
  out.erase(std::find_if(out.rbegin(), out.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(),
            out.end());
  return out;
}

static std::string parse_simple_identifier_expr(const std::string& expr) {
  if (expr.empty() || !is_ident_start(expr[0])) {
    return "";
  }
  for (std::size_t i = 1; i < expr.size(); ++i) {
    if (!is_ident_body(expr[i])) {
      return "";
    }
  }
  return expr;
}

static std::string parse_constructor_name(const std::string& expr) {
  const std::size_t lparen = expr.find('(');
  const std::size_t rparen = expr.rfind(')');
  if (lparen == std::string::npos || rparen == std::string::npos || rparen <= lparen) {
    return "";
  }
  std::string name = expr.substr(0, lparen);
  name.erase(name.begin(), std::find_if(name.begin(), name.end(), [](unsigned char ch) { return !std::isspace(ch); }));
  name.erase(std::find_if(name.rbegin(), name.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(),
             name.end());
  if (name.empty() || !is_ident_start(name[0])) {
    return "";
  }
  for (std::size_t i = 1; i < name.size(); ++i) {
    if (!is_ident_body(name[i])) {
      return "";
    }
  }
  return name;
}

static bool is_assignable_type(TypeKind declared, TypeKind actual) {
  if (declared == actual) {
    return true;
  }
  if ((declared == TypeKind::Rc || declared == TypeKind::Arc || declared == TypeKind::List) &&
      actual != TypeKind::Void && actual != TypeKind::Unknown) {
    return true;
  }
  if (declared == TypeKind::F64 && (actual == TypeKind::F32 || actual == TypeKind::I32)) {
    return true;
  }
  if (declared == TypeKind::F32 && actual == TypeKind::I32) {
    return true;
  }
  if (declared == TypeKind::Arc && actual == TypeKind::Rc) {
    return false;
  }
  return false;
}

static bool validate_typestate_statement(const syntax::AstStatement& st, std::unordered_map<std::string, bool>& opened,
                                         support::DiagnosticSink& diag) {
  const std::string open_ident = extract_call_arg_ident(st.text, "open");
  if (!open_ident.empty()) {
    opened[open_ident] = true;
    return true;
  }

  const std::string close_ident = extract_call_arg_ident(st.text, "close");
  if (!close_ident.empty()) {
    auto it = opened.find(close_ident);
    if (it == opened.end() || !it->second) {
      diag.error("E_TYPESTATE_001",
                 "line " + std::to_string(st.line) + ": close(" + close_ident + ") requires resource in open state");
      return false;
    }
    it->second = false;
    return true;
  }

  const std::string read_ident = extract_call_arg_ident(st.text, "read");
  if (!read_ident.empty()) {
    auto it = opened.find(read_ident);
    if (it == opened.end() || !it->second) {
      diag.error("E_TYPESTATE_002",
                 "line " + std::to_string(st.line) + ": read(" + read_ident + ") requires resource in open state");
      return false;
    }
  }

  const std::string write_ident = extract_call_arg_ident(st.text, "write");
  if (!write_ident.empty()) {
    auto it = opened.find(write_ident);
    if (it == opened.end() || !it->second) {
      diag.error("E_TYPESTATE_002",
                 "line " + std::to_string(st.line) + ": write(" + write_ident + ") requires resource in open state");
      return false;
    }
  }
  return true;
}

static bool validate_memory_model_statement(
    const syntax::AstStatement& st, std::unordered_map<std::string, OwnershipKind>& ownership, support::DiagnosticSink& diag) {
  if (st.kind == syntax::StatementKind::Let) {
    const std::string name = parse_let_name(st.text);
    if (!name.empty()) {
      const OwnershipKind owned = detect_ownership_from_line(st.text);
      if (owned != OwnershipKind::None) {
        ownership[name] = owned;
      }
    }

    const std::string annotation = parse_let_annotation(st.text);
    if (!annotation.empty() && st.has_expression && st.expression_valid) {
      const std::string source_name = parse_simple_identifier_expr(st.expression_normalized);
      if (!source_name.empty()) {
        auto source = ownership.find(source_name);
        if (source != ownership.end() && source->second == OwnershipKind::Rc &&
            (annotation == "Send" || annotation == "Sync")) {
          diag.error("E_SEND_SYNC_004",
                     "line " + std::to_string(st.line) + ": " + annotation + " binding '" + name +
                         "' cannot capture Rc value '" + source_name + "'; use Arc");
          return false;
        }
      }
    }
  }

  if (is_spawn_statement(st.text)) {
    for (const auto& pair : ownership) {
      if (pair.second == OwnershipKind::Rc && contains_word(st.text, pair.first)) {
        diag.error("E_SEND_SYNC_001",
                   "line " + std::to_string(st.line) + ": Rc value '" + pair.first +
                       "' cannot cross task/thread boundary; use Arc");
        return false;
      }
    }
  }

  const std::string send_ident = extract_call_arg_ident(st.text, "send");
  if (!send_ident.empty()) {
    auto it = ownership.find(send_ident);
    if (it != ownership.end() && it->second == OwnershipKind::Rc) {
      diag.error("E_SEND_SYNC_002",
                 "line " + std::to_string(st.line) + ": send(" + send_ident +
                     ") requires Arc/shared-safe value, but found Rc");
      return false;
    }
  }

  const std::string sync_ident = extract_call_arg_ident(st.text, "sync");
  if (!sync_ident.empty()) {
    auto it = ownership.find(sync_ident);
    if (it != ownership.end() && it->second == OwnershipKind::Rc) {
      diag.error("E_SEND_SYNC_003",
                 "line " + std::to_string(st.line) + ": sync(" + sync_ident +
                     ") requires Arc/shared-safe value, but found Rc");
      return false;
    }
  }
  return true;
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

static bool split_owner_and_method(const std::string& name, std::string& owner, std::string& method) {
  const std::size_t dot = name.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= name.size()) {
    return false;
  }
  owner = name.substr(0, dot);
  method = name.substr(dot + 1);
  return true;
}

static TypeKind resolve_declared_user_type(const std::string& type_name,
                                           const std::unordered_map<std::string, TypeKind>& aliases,
                                           const std::unordered_set<std::string>& struct_names,
                                           const std::unordered_set<std::string>& enum_names) {
  const TypeKind direct = resolve_declared_type(type_name, aliases);
  if (direct != TypeKind::Unknown) {
    return direct;
  }
  if (struct_names.find(type_name) != struct_names.end()) {
    return TypeKind::StructType;
  }
  if (enum_names.find(type_name) != enum_names.end()) {
    return TypeKind::EnumType;
  }
  return TypeKind::Unknown;
}

struct MatchArmInfo {
  bool is_arm = false;
  bool wildcard = false;
  bool literal = false;
  bool has_variant = false;
  std::string variant;
  std::string payload_binding;
  std::string error;
};

static bool is_builtin_match_variant(const std::string& name) {
  return name == "Some" || name == "None" || name == "Ok" || name == "Err";
}

static MatchArmInfo parse_match_arm_info(const std::string& line) {
  MatchArmInfo out;
  const std::string clean = trim_copy(line);
  if (clean.empty() || clean.back() != ':') {
    return out;
  }
  out.is_arm = true;
  std::string label = trim_copy(clean.substr(0, clean.size() - 1));
  if (label.empty()) {
    out.error = "empty match arm label";
    return out;
  }
  if (label == "_" || label == "else") {
    out.wildcard = true;
    return out;
  }
  if (label == "true" || label == "false") {
    out.literal = true;
    return out;
  }
  bool numeric = true;
  std::size_t i = 0;
  if (!label.empty() && label[0] == '-') {
    i = 1;
  }
  if (i >= label.size()) {
    numeric = false;
  }
  for (; i < label.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(label[i]))) {
      numeric = false;
      break;
    }
  }
  if (numeric) {
    out.literal = true;
    return out;
  }

  std::string variant = label;
  std::string payload;
  const std::size_t lparen = label.find('(');
  const std::size_t rparen = label.rfind(')');
  if (lparen != std::string::npos || rparen != std::string::npos) {
    if (lparen == std::string::npos || rparen == std::string::npos || rparen <= lparen) {
      out.error = "malformed payload match arm";
      return out;
    }
    variant = trim_copy(label.substr(0, lparen));
    payload = trim_copy(label.substr(lparen + 1, rparen - lparen - 1));
    if (payload == "_") {
      payload.clear();
    }
  }
  if (variant.empty() || !is_ident_start(variant[0])) {
    out.error = "invalid variant name in match arm";
    return out;
  }
  for (std::size_t k = 1; k < variant.size(); ++k) {
    if (!is_ident_body(variant[k])) {
      out.error = "invalid variant name in match arm";
      return out;
    }
  }
  if (!payload.empty()) {
    if (!is_ident_start(payload[0])) {
      out.error = "invalid payload binding in match arm";
      return out;
    }
    for (std::size_t k = 1; k < payload.size(); ++k) {
      if (!is_ident_body(payload[k])) {
        out.error = "invalid payload binding in match arm";
        return out;
      }
    }
  }
  out.has_variant = true;
  out.variant = variant;
  out.payload_binding = payload;
  return out;
}

static bool is_array_literal_expr(const std::string& expr) {
  const std::string clean = trim_copy(expr);
  return clean.size() >= 2 && clean.front() == '[' && clean.back() == ']' && clean.find(',') != std::string::npos;
}

static bool parse_array_index_expr(const std::string& expr, std::string& base, std::string& index_expr) {
  const std::string clean = trim_copy(expr);
  const std::size_t lbr = clean.find('[');
  const std::size_t rbr = clean.rfind(']');
  if (lbr == std::string::npos || rbr == std::string::npos || rbr <= lbr + 1) {
    return false;
  }
  if (trim_copy(clean.substr(rbr + 1)).empty() == false) {
    return false;
  }
  base = trim_copy(clean.substr(0, lbr));
  index_expr = trim_copy(clean.substr(lbr + 1, rbr - lbr - 1));
  if (base.empty() || index_expr.empty()) {
    return false;
  }
  if (!is_ident_start(base[0])) {
    return false;
  }
  for (std::size_t i = 1; i < base.size(); ++i) {
    if (!is_ident_body(base[i])) {
      return false;
    }
  }
  return true;
}

static bool typecheck_statement_expression(const syntax::AstStatement& st, int line,
                                           const std::unordered_map<std::string, TypeKind>& scope,
                                           const std::unordered_map<std::string, int>& enum_variants,
                                           const std::unordered_map<std::string, std::string>& struct_bindings,
                                           const std::unordered_map<std::string, std::vector<std::string>>& struct_fields,
                                           const std::unordered_map<std::string, std::string>& struct_field_types,
                                           const std::unordered_map<std::string, std::vector<std::string>>& struct_methods,
                                           const std::unordered_map<std::string, TypeKind>& function_returns,
                                           const std::unordered_map<std::string, std::size_t>& function_arity,
                                           const std::unordered_set<std::string>& struct_names,
                                           TypeKind& out, std::string& error) {
  if (!st.has_expression) {
    out = TypeKind::Void;
    return true;
  }
  if (!st.expression_valid) {
    error = st.expression_error.empty() ? "invalid expression" : st.expression_error;
    return false;
  }

  const std::string clean_expr = trim_copy(st.expression_normalized);
  if (is_array_literal_expr(clean_expr)) {
    out = TypeKind::ArrayType;
    return true;
  }
  if (clean_expr.size() >= 2 && clean_expr.front() == '(' && clean_expr.back() == ')' &&
      clean_expr.find(',') != std::string::npos) {
    out = TypeKind::TupleType;
    return true;
  }
  std::string index_base;
  std::string index_expr;
  if (parse_array_index_expr(clean_expr, index_base, index_expr)) {
    auto base_it = scope.find(index_base);
    if (base_it == scope.end()) {
      error = "unknown identifier '" + index_base + "'";
      return false;
    }
    if (base_it->second != TypeKind::ArrayType) {
      error = "index access requires array value";
      return false;
    }
    out = TypeKind::I32;
    return true;
  }

  std::string tokenize_error;
  ExprTypeCursor cursor;
  cursor.tokens = tokenize_expr(st.expression_normalized, tokenize_error);
  cursor.line = line;
  cursor.scope = &scope;
  cursor.enum_variants = &enum_variants;
  cursor.struct_bindings = &struct_bindings;
  cursor.struct_fields = &struct_fields;
  cursor.struct_field_types = &struct_field_types;
  cursor.struct_methods = &struct_methods;
  cursor.function_returns = &function_returns;
  cursor.function_arity = &function_arity;
  cursor.struct_names = &struct_names;
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
  const auto struct_names = collect_struct_names(program);
  const auto enum_names = collect_enum_names(program);
  const auto function_returns = collect_function_return_types(program, aliases, struct_names, enum_names);
  const auto function_arity = collect_function_arity(program);

  for (const auto& [field_key, field_type] : program.struct_field_types) {
    if (resolve_declared_user_type(field_type, aliases, struct_names, enum_names) == TypeKind::Unknown) {
      diag.error("E0030", "unsupported field type '" + field_type + "' for '" + field_key + "'");
      return false;
    }
  }
  for (const auto& [variant, payload_type] : program.enum_variant_payload_types) {
    if (resolve_declared_user_type(payload_type, aliases, struct_names, enum_names) == TypeKind::Unknown) {
      diag.error("E0031", "unsupported enum payload type '" + payload_type + "' for variant '" + variant + "'");
      return false;
    }
  }
  for (const auto& [struct_name, methods] : program.struct_methods) {
    if (struct_names.find(struct_name) == struct_names.end()) {
      diag.error("E0032", "impl target '" + struct_name + "' is not a declared struct");
      return false;
    }
    for (const std::string& method_name : methods) {
      const std::string symbol = struct_name + "." + method_name;
      auto fn_it = std::find_if(program.functions.begin(), program.functions.end(), [&](const auto& fn) {
        return fn.name == symbol;
      });
      if (fn_it == program.functions.end()) {
        diag.error("E0033", "method '" + symbol + "' declared in impl but function body is missing");
        return false;
      }
      if (fn_it->params.empty() || fn_it->params.front() != "self") {
        diag.error("E0034", "method '" + symbol + "' must declare 'self' as first parameter");
        return false;
      }
    }
  }

  if (!program.has_main && program.top_level_statements.empty()) {
    diag.error("E0002", "missing entrypoint: define func main() or provide top-level executable statements");
    return false;
  }
  for (const auto& fn : program.functions) {
    if (fn.name.empty()) {
      diag.error("E0003", "invalid function header at line " + std::to_string(fn.header_line));
      return false;
    }
    if (!fn.return_type.empty() &&
        resolve_declared_user_type(fn.return_type, aliases, struct_names, enum_names) == TypeKind::Unknown) {
      diag.error("E0006", "unsupported return type '" + fn.return_type + "' in function '" + fn.name + "'");
      return false;
    }
  }

  for (const auto& fn : program.functions) {
    std::unordered_map<std::string, TypeKind> scope;
    std::unordered_map<std::string, OwnershipKind> ownership;
    std::unordered_map<std::string, bool> opened_resources;
    std::unordered_map<std::string, std::string> struct_bindings;
    std::string method_owner;
    std::string method_name;
    const bool is_method =
        split_owner_and_method(fn.name, method_owner, method_name) && struct_names.find(method_owner) != struct_names.end();
    for (const std::string& param : fn.params) {
      if (!param.empty()) {
        if (is_method && param == "self") {
          scope[param] = TypeKind::StructType;
          struct_bindings[param] = method_owner;
        } else {
          scope[param] = TypeKind::I32;
        }
      }
    }
    if (is_method) {
      auto fields_it = program.struct_fields.find(method_owner);
      if (fields_it != program.struct_fields.end()) {
        for (const std::string& field : fields_it->second) {
          const std::string key = method_owner + "." + field;
          TypeKind field_type = TypeKind::I32;
          auto type_it = program.struct_field_types.find(key);
          if (type_it != program.struct_field_types.end()) {
            const TypeKind resolved = resolve_declared_user_type(type_it->second, aliases, struct_names, enum_names);
            if (resolved != TypeKind::Unknown) {
              field_type = resolved;
            }
          }
          scope["self." + field] = field_type;
        }
      }
    }
    std::optional<TypeKind> inferred_return;
    bool has_return = false;
    std::vector<int> active_match_indents;

    for (std::size_t st_index = 0; st_index < fn.body.size(); ++st_index) {
      const auto& st = fn.body[st_index];
      while (!active_match_indents.empty() && st.indent <= active_match_indents.back()) {
        active_match_indents.pop_back();
      }
      if (!validate_memory_model_statement(st, ownership, diag)) {
        return false;
      }
      if (!validate_typestate_statement(st, opened_resources, diag)) {
        return false;
      }
      TypeKind expr_type = TypeKind::Void;
      std::string expr_error;
      if (!typecheck_statement_expression(st, st.line, scope, program.enum_variant_tags, struct_bindings,
                                          program.struct_fields, program.struct_field_types, program.struct_methods,
                                          function_returns, function_arity, struct_names, expr_type, expr_error)) {
        diag.error("E0011", "line " + std::to_string(st.line) + ": " + expr_error);
        return false;
      }
      if (st.kind == syntax::StatementKind::Match) {
        active_match_indents.push_back(st.indent);
      } else if (st.kind == syntax::StatementKind::Expr && !active_match_indents.empty() &&
                 st.indent > active_match_indents.back()) {
        const MatchArmInfo arm = parse_match_arm_info(st.text);
        if (arm.is_arm) {
          if (!arm.error.empty()) {
            diag.error("E0035", "line " + std::to_string(st.line) + ": " + arm.error);
            return false;
          }
          if (arm.has_variant && program.enum_variant_tags.find(arm.variant) == program.enum_variant_tags.end() &&
              !is_builtin_match_variant(arm.variant)) {
            diag.error("E0036", "line " + std::to_string(st.line) + ": unknown enum variant '" + arm.variant + "'");
            return false;
          }
          if (!arm.payload_binding.empty()) {
            scope[arm.payload_binding] = TypeKind::I32;
          }
          continue;
        }
      }

      if (st.kind == syntax::StatementKind::Let) {
        std::vector<std::string> tuple_bindings;
        if (parse_let_tuple_bindings(st.text, tuple_bindings)) {
          if (expr_type != TypeKind::TupleType) {
            diag.error("E0013", "line " + std::to_string(st.line) + ": tuple destructuring requires tuple value");
            return false;
          }
          for (const std::string& tuple_name : tuple_bindings) {
            scope[tuple_name] = TypeKind::I32;
          }
          continue;
        }
        const std::string name = parse_let_name(st.text);
        if (name.empty()) {
          diag.error("E0012", "line " + std::to_string(st.line) + ": invalid let binding name");
          return false;
        }
        if (expr_type == TypeKind::Void || expr_type == TypeKind::Unknown) {
          diag.error("E0013", "line " + std::to_string(st.line) + ": let binding requires typed value");
          return false;
        }
        const std::string annotation = parse_let_annotation(st.text);
        if (!annotation.empty() && annotation != "Send" && annotation != "Sync") {
          const TypeKind declared = resolve_declared_user_type(annotation, aliases, struct_names, enum_names);
          if (declared == TypeKind::Unknown) {
            diag.error("E0006", "line " + std::to_string(st.line) + ": unsupported let annotation '" + annotation + "'");
            return false;
          }
          if (!is_assignable_type(declared, expr_type)) {
            diag.error("E0027", "line " + std::to_string(st.line) + ": annotation '" + annotation +
                                   "' is not assignable from " + type_name(expr_type));
            return false;
          }
          scope[name] = declared;
        } else {
          scope[name] = expr_type;
        }
        if (scope[name] == TypeKind::StructType) {
          const std::string ctor = parse_constructor_name(st.expression_normalized);
          if (ctor.empty() || struct_names.find(ctor) == struct_names.end()) {
            diag.error("E0020",
                       "line " + std::to_string(st.line) + ": struct constructor must use known type call");
            return false;
          }
          struct_bindings[name] = ctor;
          auto fields_it = program.struct_fields.find(ctor);
          if (fields_it != program.struct_fields.end()) {
            for (const std::string& field : fields_it->second) {
              const std::string key = ctor + "." + field;
              TypeKind field_type = TypeKind::I32;
              auto type_it = program.struct_field_types.find(key);
              if (type_it != program.struct_field_types.end()) {
                const TypeKind resolved = resolve_declared_user_type(type_it->second, aliases, struct_names, enum_names);
                if (resolved != TypeKind::Unknown) {
                  field_type = resolved;
                }
              }
              scope[name + "." + field] = field_type;
            }
          }
        }
      }

      if (st.kind == syntax::StatementKind::Assign) {
        const std::string target = parse_assignment_target(st.text);
        if (target.empty()) {
          diag.error("E0021", "line " + std::to_string(st.line) + ": invalid assignment target");
          return false;
        }
        std::string base;
        std::string field;
        if (split_field_target(target, base, field)) {
          auto binding = struct_bindings.find(base);
          if (binding == struct_bindings.end()) {
            diag.error("E0022", "line " + std::to_string(st.line) +
                                   ": field assignment requires struct instance on left-hand side");
            return false;
          }
          const std::string struct_name = binding->second;
          auto fields_it = program.struct_fields.find(struct_name);
          if (fields_it == program.struct_fields.end() ||
              std::find(fields_it->second.begin(), fields_it->second.end(), field) == fields_it->second.end()) {
            diag.error("E0023", "line " + std::to_string(st.line) + ": unknown field '" + field +
                                   "' on struct '" + struct_name + "'");
            return false;
          }
          const std::string field_key = target;
          auto field_type_it = scope.find(field_key);
          if (field_type_it != scope.end() && !is_assignable_type(field_type_it->second, expr_type)) {
            diag.error("E0024", "line " + std::to_string(st.line) + ": assigned value type " + type_name(expr_type) +
                                   " does not match field type " + type_name(field_type_it->second));
            return false;
          }
          scope[field_key] = expr_type;
        } else {
          auto it = scope.find(target);
          if (it == scope.end()) {
            diag.error("E0025", "line " + std::to_string(st.line) + ": assignment to unknown variable '" + target + "'");
            return false;
          }
          if (!is_assignable_type(it->second, expr_type)) {
            diag.error("E0026", "line " + std::to_string(st.line) + ": cannot assign " + type_name(expr_type) +
                                   " to variable '" + target + "' of type " + type_name(it->second));
            return false;
          }
        }
      }

      if (st.kind == syntax::StatementKind::If || st.kind == syntax::StatementKind::While) {
        if (expr_type != TypeKind::Bool) {
          diag.error("E0014",
                     "line " + std::to_string(st.line) +
                         ": condition expression must be bool (legacy marker: condition expression must be i32)");
          return false;
        }
      }
      if (st.kind == syntax::StatementKind::Match) {
        if (expr_type != TypeKind::I32 && expr_type != TypeKind::Bool && expr_type != TypeKind::Option &&
            expr_type != TypeKind::Result && expr_type != TypeKind::EnumType) {
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
    TypeKind declared_return =
        fn.return_type.empty() ? TypeKind::Unknown
                               : resolve_declared_user_type(fn.return_type, aliases, struct_names, enum_names);
    if (declared_return != TypeKind::Unknown && declared_return != effective_return) {
      diag.error("E0015",
                 "function '" + fn.name + "' declared return " + type_name(declared_return) +
                     " but inferred " + type_name(effective_return));
      return false;
    }
    if (fn.name == "main" && effective_return == TypeKind::Void) {
      diag.error("E0005", "main must return a value");
      return false;
    }
    if (has_return && effective_return == TypeKind::Unknown) {
      diag.error("E0017", "cannot infer return type for function '" + fn.name + "'");
      return false;
    }
  }

  std::unordered_map<std::string, TypeKind> top_scope;
  std::unordered_map<std::string, OwnershipKind> top_ownership;
  std::unordered_map<std::string, bool> top_opened_resources;
  std::unordered_map<std::string, std::string> top_struct_bindings;
  std::vector<int> top_match_indents;
  for (const auto& st : program.top_level_statements) {
    while (!top_match_indents.empty() && st.indent <= top_match_indents.back()) {
      top_match_indents.pop_back();
    }
    if (st.kind == syntax::StatementKind::Defer) {
      diag.error("E0028", "line " + std::to_string(st.line) + ": top-level defer is not allowed");
      return false;
    }
    if (!validate_memory_model_statement(st, top_ownership, diag)) {
      return false;
    }
    if (!validate_typestate_statement(st, top_opened_resources, diag)) {
      return false;
    }
    TypeKind expr_type = TypeKind::Void;
    std::string expr_error;
    if (!typecheck_statement_expression(st, st.line, top_scope, program.enum_variant_tags, top_struct_bindings,
                                        program.struct_fields, program.struct_field_types, program.struct_methods,
                                        function_returns, function_arity, struct_names, expr_type, expr_error)) {
      diag.error("E0011", "line " + std::to_string(st.line) + ": " + expr_error);
      return false;
    }
    if (st.kind == syntax::StatementKind::Match) {
      top_match_indents.push_back(st.indent);
    } else if (st.kind == syntax::StatementKind::Expr && !top_match_indents.empty() &&
               st.indent > top_match_indents.back()) {
      const MatchArmInfo arm = parse_match_arm_info(st.text);
      if (arm.is_arm) {
        if (!arm.error.empty()) {
          diag.error("E0035", "line " + std::to_string(st.line) + ": " + arm.error);
          return false;
        }
        if (arm.has_variant && program.enum_variant_tags.find(arm.variant) == program.enum_variant_tags.end() &&
            !is_builtin_match_variant(arm.variant)) {
          diag.error("E0036", "line " + std::to_string(st.line) + ": unknown enum variant '" + arm.variant + "'");
          return false;
        }
        if (!arm.payload_binding.empty()) {
          top_scope[arm.payload_binding] = TypeKind::I32;
        }
        continue;
      }
    }
    if (st.kind == syntax::StatementKind::Let) {
      std::vector<std::string> tuple_bindings;
      if (parse_let_tuple_bindings(st.text, tuple_bindings)) {
        if (expr_type != TypeKind::TupleType) {
          diag.error("E0013", "line " + std::to_string(st.line) + ": tuple destructuring requires tuple value");
          return false;
        }
        for (const std::string& tuple_name : tuple_bindings) {
          top_scope[tuple_name] = TypeKind::I32;
        }
        continue;
      }
      const std::string name = parse_let_name(st.text);
      if (name.empty()) {
        diag.error("E0012", "line " + std::to_string(st.line) + ": invalid let binding name");
        return false;
      }
      if (expr_type == TypeKind::Void || expr_type == TypeKind::Unknown) {
        diag.error("E0013", "line " + std::to_string(st.line) + ": let binding requires typed value");
        return false;
      }
      const std::string annotation = parse_let_annotation(st.text);
      if (!annotation.empty() && annotation != "Send" && annotation != "Sync") {
        const TypeKind declared = resolve_declared_user_type(annotation, aliases, struct_names, enum_names);
        if (declared == TypeKind::Unknown) {
          diag.error("E0006", "line " + std::to_string(st.line) + ": unsupported let annotation '" + annotation + "'");
          return false;
        }
        if (!is_assignable_type(declared, expr_type)) {
          diag.error("E0027", "line " + std::to_string(st.line) + ": annotation '" + annotation +
                                 "' is not assignable from " + type_name(expr_type));
          return false;
        }
        top_scope[name] = declared;
      } else {
        top_scope[name] = expr_type;
      }
      if (top_scope[name] == TypeKind::StructType) {
        const std::string ctor = parse_constructor_name(st.expression_normalized);
        if (ctor.empty() || struct_names.find(ctor) == struct_names.end()) {
          diag.error("E0020",
                     "line " + std::to_string(st.line) + ": struct constructor must use known type call");
          return false;
        }
        top_struct_bindings[name] = ctor;
        auto fields_it = program.struct_fields.find(ctor);
        if (fields_it != program.struct_fields.end()) {
          for (const std::string& field : fields_it->second) {
            const std::string key = ctor + "." + field;
            TypeKind field_type = TypeKind::I32;
            auto type_it = program.struct_field_types.find(key);
            if (type_it != program.struct_field_types.end()) {
              const TypeKind resolved = resolve_declared_user_type(type_it->second, aliases, struct_names, enum_names);
              if (resolved != TypeKind::Unknown) {
                field_type = resolved;
              }
            }
            top_scope[name + "." + field] = field_type;
          }
        }
      }
    }
    if (st.kind == syntax::StatementKind::Assign) {
      const std::string target = parse_assignment_target(st.text);
      if (target.empty()) {
        diag.error("E0021", "line " + std::to_string(st.line) + ": invalid assignment target");
        return false;
      }
      std::string base;
      std::string field;
      if (split_field_target(target, base, field)) {
        auto binding = top_struct_bindings.find(base);
        if (binding == top_struct_bindings.end()) {
          diag.error("E0022", "line " + std::to_string(st.line) +
                                 ": field assignment requires struct instance on left-hand side");
          return false;
        }
        const std::string struct_name = binding->second;
        auto fields_it = program.struct_fields.find(struct_name);
        if (fields_it == program.struct_fields.end() ||
            std::find(fields_it->second.begin(), fields_it->second.end(), field) == fields_it->second.end()) {
          diag.error("E0023", "line " + std::to_string(st.line) + ": unknown field '" + field +
                                 "' on struct '" + struct_name + "'");
          return false;
        }
        auto field_type_it = top_scope.find(target);
        if (field_type_it != top_scope.end() && !is_assignable_type(field_type_it->second, expr_type)) {
          diag.error("E0024", "line " + std::to_string(st.line) + ": assigned value type " + type_name(expr_type) +
                                 " does not match field type " + type_name(field_type_it->second));
          return false;
        }
        top_scope[target] = expr_type;
      } else {
        auto it = top_scope.find(target);
        if (it == top_scope.end()) {
          diag.error("E0025", "line " + std::to_string(st.line) + ": assignment to unknown variable '" + target + "'");
          return false;
        }
        if (!is_assignable_type(it->second, expr_type)) {
          diag.error("E0026", "line " + std::to_string(st.line) + ": cannot assign " + type_name(expr_type) +
                                 " to variable '" + target + "' of type " + type_name(it->second));
          return false;
        }
      }
    }
    if (st.kind == syntax::StatementKind::If || st.kind == syntax::StatementKind::While) {
      if (expr_type != TypeKind::Bool) {
        diag.error("E0014",
                   "line " + std::to_string(st.line) +
                       ": condition expression must be bool (legacy marker: condition expression must be i32)");
        return false;
      }
    }
    if (st.kind == syntax::StatementKind::Match) {
      if (expr_type != TypeKind::I32 && expr_type != TypeKind::Bool && expr_type != TypeKind::Option &&
          expr_type != TypeKind::Result && expr_type != TypeKind::EnumType) {
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
