#include "thagc/frontend/typechecker.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "thagc/hir/typecheck.hpp"
#include "thagc/frontend/types.hpp"
#include "thagc/frontend/source_map.hpp"

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

static const std::unordered_set<std::string>& supported_intent_goals() {
  static const std::unordered_set<std::string> kGoals = {
      "off",          "auto_plan",             "reduce_sum",                "map_filter_reduce",
      "deduplicate_sorted",                    "binary_search",             "binary_search_sorted",
      "lower_bound_sorted",                    "upper_bound_sorted",        "count_less_sorted",
      "count_less_equal_sorted",               "count_greater_sorted",      "count_greater_equal_sorted",
      "count_equal_sorted",                    "count_not_equal_sorted",    "count_range_sorted",
      "count_outside_range_sorted",            "two_sum_sorted_exists",     "string_contains",
      "dot_product",                           "polynomial_eval",           "fibonacci_dp",
      "tribonacci_dp",                         "factorial_iterative",       "power_fast",
      "gcd_euclid",                            "is_prime_fast",             "count_divisors_sqrt",
      "interval_cover_greedy",                 "bit_peel_iterative",        "sum_squares_formula",
      "sum_cubes_formula",                     "sum_even_squares_formula",  "sum_odd_squares_formula",
      "sum_even_cubes_formula",                "sum_odd_cubes_formula",     "sum_even_formula",
      "sum_odd_formula",                       "sort_ascending",            "search_element",
      "sqrt_bounded_loop",
  };
  return kGoals;
}

static bool valid_intent_strategy(const std::string& strategy) {
  if (strategy.empty()) {
    return true;
  }
  bool saw_dot = false;
  bool has_segment_char = false;
  bool prev_dot = false;
  for (char ch : strategy) {
    const bool ident = std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
    if (ident) {
      has_segment_char = true;
      prev_dot = false;
      continue;
    }
    if (ch == '.') {
      if (prev_dot || !has_segment_char) {
        return false;
      }
      saw_dot = true;
      has_segment_char = false;
      prev_dot = true;
      continue;
    }
    return false;
  }
  return saw_dot && has_segment_char && !prev_dot;
}

static bool parse_generic_parts(const std::string& type_name, std::string& base, std::vector<std::string>& args) {
  args.clear();
  const std::string clean = trim_copy(type_name);
  const std::size_t lt = clean.find('<');
  if (lt == std::string::npos || lt == 0 || lt + 2 >= clean.size()) {
    return false;
  }
  base = trim_copy(clean.substr(0, lt));
  if (base.empty()) {
    return false;
  }
  std::size_t gt = std::string::npos;
  int depth = 0;
  for (std::size_t i = lt; i < clean.size(); ++i) {
    if (clean[i] == '<') {
      ++depth;
      continue;
    }
    if (clean[i] == '>') {
      --depth;
      if (depth == 0) {
        gt = i;
        break;
      }
      if (depth < 0) {
        return false;
      }
    }
  }
  if (gt == std::string::npos || depth != 0) {
    return false;
  }
  if (trim_copy(clean.substr(gt + 1)).size() != 0) {
    return false;
  }
  if (gt <= lt + 1) {
    return false;
  }
  const std::string inner = clean.substr(lt + 1, gt - lt - 1);
  std::size_t i = 0;
  depth = 0;
  std::size_t start = 0;
  auto push_arg = [&](std::size_t end) {
    const std::string arg = trim_copy(inner.substr(start, end - start));
    if (arg.empty()) {
      return false;
    }
    args.push_back(arg);
    return true;
  };
  while (i < inner.size()) {
    const char ch = inner[i];
    if (ch == '<') {
      ++depth;
      ++i;
      continue;
    }
    if (ch == '>') {
      if (depth == 0) {
        return false;
      }
      --depth;
      ++i;
      continue;
    }
    if (ch == ',' && depth == 0) {
      if (!push_arg(i)) {
        return false;
      }
      start = i + 1;
    }
    ++i;
  }
  if (depth != 0) {
    return false;
  }
  if (!push_arg(inner.size())) {
    return false;
  }
  return !args.empty();
}

static bool is_builtin_generic_base(const std::string& base) {
  return base == "Option" || base == "Result" || base == "List" || base == "Rc" || base == "Arc";
}

static std::size_t builtin_generic_arity(const std::string& base) {
  if (base == "Result") {
    return 2;
  }
  if (base == "Option" || base == "List" || base == "Rc" || base == "Arc") {
    return 1;
  }
  return 0;
}

static std::string strip_state_annotation(const std::string& type_name);
static bool is_tuple_type_syntax(const std::string& type_name);
static bool is_array_type_syntax(const std::string& type_name);

static bool is_supported_type_internal(const std::string& type_name, int depth_limit) {
  if (depth_limit <= 0) {
    return false;
  }
  const std::string clean = strip_state_annotation(type_name);
  if (clean == "i32" || clean == "i64" || clean == "f32" || clean == "f64" || clean == "bool" || clean == "string" ||
      clean == "String" || clean == "ptr" || clean == "void" || clean == "Fn") {
    return true;
  }
  if (clean == "Option" || clean == "Result" || clean == "List" || clean == "Rc" || clean == "Arc") {
    return true;
  }
  if (is_tuple_type_syntax(clean) || is_array_type_syntax(clean)) {
    return true;
  }
  std::string base;
  std::vector<std::string> args;
  if (!parse_generic_parts(clean, base, args)) {
    return false;
  }
  if (!is_builtin_generic_base(base)) {
    return false;
  }
  const std::size_t expected = builtin_generic_arity(base);
  if (expected == 0 || args.size() != expected) {
    return false;
  }
  for (const std::string& arg : args) {
    if (!is_supported_type_internal(arg, depth_limit - 1)) {
      return false;
    }
  }
  return true;
}

static bool split_state_annotation(const std::string& type_name, std::string& set_name, std::string& variant_name) {
  const std::string clean = trim_copy(type_name);
  const std::size_t lbr = clean.find('[');
  const std::size_t rbr = clean.rfind(']');
  if (lbr == std::string::npos || rbr == std::string::npos || rbr <= lbr + 1 || rbr + 1 != clean.size()) {
    return false;
  }
  set_name = trim_copy(clean.substr(0, lbr));
  variant_name = trim_copy(clean.substr(lbr + 1, rbr - lbr - 1));
  return !set_name.empty() && !variant_name.empty();
}

static std::string strip_state_annotation(const std::string& type_name) {
  std::string set_name;
  std::string variant_name;
  if (split_state_annotation(type_name, set_name, variant_name)) {
    return set_name;
  }
  return trim_copy(type_name);
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
  return is_supported_type_internal(type_name, 32);
}

static std::string type_name(TypeKind kind) {
  if (kind == TypeKind::I32) return "i32";
  if (kind == TypeKind::I64) return "i64";
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
  const std::string clean = strip_state_annotation(type_name);
  if (clean == "i32") return TypeKind::I32;
  if (clean == "i64") return TypeKind::I64;
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
    const std::size_t expected = builtin_generic_arity(base);
    if (expected == 0 || args.size() != expected) {
      return TypeKind::Unknown;
    }
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
  const std::string clean = strip_state_annotation(type_name);
  const TypeKind direct = parse_type_name(clean);
  if (direct != TypeKind::Unknown) {
    return direct;
  }
  auto it = aliases.find(clean);
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
    const std::string clean_return = strip_state_annotation(fn.return_type);
    const TypeKind direct = resolve_declared_type(clean_return, aliases);
    if (direct != TypeKind::Unknown) {
      out[fn.name] = direct;
      continue;
    }
    if (struct_names.find(clean_return) != struct_names.end()) {
      out[fn.name] = TypeKind::StructType;
      continue;
    }
    if (enum_names.find(clean_return) != enum_names.end()) {
      out[fn.name] = TypeKind::EnumType;
      continue;
    }
    out[fn.name] = TypeKind::Unknown;
  }
  for (const auto& ext : program.extern_functions) {
    const std::string clean_return = strip_state_annotation(ext.return_type);
    const TypeKind direct = resolve_declared_type(clean_return, aliases);
    if (direct != TypeKind::Unknown) {
      out[ext.name] = direct;
    } else if (struct_names.find(clean_return) != struct_names.end()) {
      out[ext.name] = TypeKind::StructType;
    } else if (enum_names.find(clean_return) != enum_names.end()) {
      out[ext.name] = TypeKind::EnumType;
    } else {
      out[ext.name] = TypeKind::Unknown;
    }
  }
  for (const auto& flow : program.flow_defs) {
    if (!flow.name.empty()) {
      out[flow.name] = TypeKind::I32;
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
  for (const auto& flow : program.flow_defs) {
    if (!flow.name.empty()) {
      out[flow.name] = 0;
    }
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
      if (tok.text == "spawn") {
        if (args.size() != 1) {
          cursor.error = "spawn() expects exactly 1 argument";
          return TypeKind::Unknown;
        }
        return TypeKind::I32;
      }
      if (tok.text == "Rc" || tok.text == "Arc") {
        if (args.size() != 1) {
          cursor.error = tok.text + "() expects exactly 1 argument";
          return TypeKind::Unknown;
        }
        return tok.text == "Arc" ? TypeKind::Arc : TypeKind::Rc;
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
  return kind == TypeKind::I32 || kind == TypeKind::I64 || kind == TypeKind::F32 || kind == TypeKind::F64;
}

static TypeKind combine_numeric(TypeKind lhs, TypeKind rhs) {
  if (lhs == TypeKind::F64 || rhs == TypeKind::F64) {
    return TypeKind::F64;
  }
  if (lhs == TypeKind::F32 || rhs == TypeKind::F32) {
    return TypeKind::F32;
  }
  if (lhs == TypeKind::I64 || rhs == TypeKind::I64) {
    return TypeKind::I64;
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
      if (lhs != rhs && !(is_numeric_type(lhs) && is_numeric_type(rhs))) {
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

static bool is_spawn_statement(const std::string& line) {
  return line.find("spawn(") != std::string::npos || starts_with(line, "spawn ");
}

static std::string extract_call_arg_ident(const std::string& line, const std::string& callee) {
  const std::string needle = callee + "(";
  std::size_t call = line.find(needle);
  while (call != std::string::npos) {
    if (call > 0) {
      const char prev = line[call - 1];
      // Typestate checks only apply to bare calls (read(x), write(x), ...),
      // not qualified/member/runtime calls (fs.read(x), thag_fs_read(x), obj.read(x)).
      if (!is_word_boundary(prev) || prev == '.') {
        call = line.find(needle, call + 1);
        continue;
      }
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
    const std::size_t ident_end = i;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
      ++i;
    }
    if (i >= line.size() || line[i] != ')') {
      return "";
    }
    return line.substr(start, ident_end - start);
  }
  return "";
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

struct StateRef {
  bool tagged = false;
  std::string set;
  std::string variant;
};

struct FunctionStateContract {
  std::vector<StateRef> param_states;
  StateRef return_state;
};

struct CallExprInfo {
  bool is_call = false;
  std::string callee;
  std::vector<std::string> args;
};

static bool state_variant_exists(const syntax::AstProgram& program, const std::string& set_name,
                                 const std::string& variant_name) {
  auto it = program.state_sets.find(set_name);
  if (it == program.state_sets.end()) {
    return false;
  }
  return std::find(it->second.begin(), it->second.end(), variant_name) != it->second.end();
}

static bool parse_state_ref(const std::string& annotation, const syntax::AstProgram& program, StateRef& out,
                            std::string& error_code, std::string& error_message) {
  out = StateRef{};
  error_code.clear();
  error_message.clear();
  std::string set_name;
  std::string variant_name;
  if (!split_state_annotation(annotation, set_name, variant_name)) {
    return true;
  }
  if (program.state_sets.find(set_name) == program.state_sets.end()) {
    error_code = "E_STATE_UNKNOWN_SET";
    error_message = "unknown state set '" + set_name + "'";
    return false;
  }
  if (!state_variant_exists(program, set_name, variant_name)) {
    error_code = "E_STATE_UNKNOWN_VARIANT";
    error_message = "unknown state variant '" + variant_name + "' in set '" + set_name + "'";
    return false;
  }
  out.tagged = true;
  out.set = set_name;
  out.variant = variant_name;
  return true;
}

static bool is_same_state_ref(const StateRef& lhs, const StateRef& rhs) {
  if (!lhs.tagged || !rhs.tagged) {
    return false;
  }
  return lhs.set == rhs.set && lhs.variant == rhs.variant;
}

static std::string state_ref_display(const StateRef& ref) {
  if (!ref.tagged) {
    return "<unknown>";
  }
  return ref.set + "[" + ref.variant + "]";
}

static bool parse_call_expr_info(const std::string& expr, CallExprInfo& out) {
  out = CallExprInfo{};
  std::string clean = trim_copy(expr);
  if (starts_with(clean, "await ")) {
    clean = trim_copy(clean.substr(6));
  }
  const std::size_t lparen = clean.find('(');
  const std::size_t rparen = clean.rfind(')');
  if (lparen == std::string::npos || rparen == std::string::npos || rparen <= lparen || rparen + 1 != clean.size()) {
    return false;
  }
  const std::string callee = trim_copy(clean.substr(0, lparen));
  if (callee.empty() || !is_ident_start(callee[0])) {
    return false;
  }
  for (std::size_t i = 1; i < callee.size(); ++i) {
    if (!is_ident_body(callee[i])) {
      return false;
    }
  }
  const std::string inner = clean.substr(lparen + 1, rparen - lparen - 1);
  std::vector<std::string> args;
  std::size_t start = 0;
  int depth = 0;
  bool in_string = false;
  bool escape = false;
  for (std::size_t i = 0; i <= inner.size(); ++i) {
    const char ch = i < inner.size() ? inner[i] : ',';
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
      const std::string arg = trim_copy(inner.substr(start, i - start));
      if (!arg.empty()) {
        args.push_back(arg);
      }
      start = i + 1;
    }
  }
  out.is_call = true;
  out.callee = callee;
  out.args = std::move(args);
  return true;
}

static std::unordered_map<std::string, FunctionStateContract> collect_function_state_contracts(
    const syntax::AstProgram& program, support::DiagnosticSink& diag) {
  std::unordered_map<std::string, FunctionStateContract> contracts;
  for (const auto& fn : program.functions) {
    FunctionStateContract contract;
    contract.param_states.resize(fn.params.size());
    for (std::size_t i = 0; i < fn.param_types.size() && i < contract.param_states.size(); ++i) {
      std::string error_code;
      std::string error_message;
      if (!parse_state_ref(fn.param_types[i], program, contract.param_states[i], error_code, error_message)) {
        diag.error(error_code, "line " + std::to_string(fn.header_line) + ": " + error_message);
      }
    }
    if (!fn.return_type.empty()) {
      std::string error_code;
      std::string error_message;
      if (!parse_state_ref(fn.return_type, program, contract.return_state, error_code, error_message)) {
        diag.error(error_code, "line " + std::to_string(fn.header_line) + ": " + error_message);
      }
    }
    contracts[fn.name] = contract;
  }
  for (const auto& ext : program.extern_functions) {
    FunctionStateContract contract;
    contract.param_states.resize(ext.param_types.size());
    for (std::size_t i = 0; i < ext.param_types.size(); ++i) {
      std::string error_code;
      std::string error_message;
      if (!parse_state_ref(ext.param_types[i], program, contract.param_states[i], error_code, error_message)) {
        diag.error(error_code, "line " + std::to_string(ext.line) + ": " + error_message);
      }
    }
    std::string error_code;
    std::string error_message;
    if (!ext.return_type.empty() &&
        !parse_state_ref(ext.return_type, program, contract.return_state, error_code, error_message)) {
      diag.error(error_code, "line " + std::to_string(ext.line) + ": " + error_message);
    }
    contracts[ext.name] = contract;
  }
  for (const auto& flow : program.flow_defs) {
    if (!flow.name.empty()) {
      contracts[flow.name] = FunctionStateContract{};
    }
  }
  return contracts;
}

struct MemoryModelState {
  std::unordered_map<std::string, std::string> value_types;
  std::unordered_map<std::string, std::vector<std::string>> closure_captures;
};

static std::string normalize_type_expr(const std::string& type_expr) {
  return trim_copy(type_expr);
}

static std::vector<std::string> extract_identifiers(const std::string& text) {
  std::vector<std::string> out;
  std::size_t i = 0;
  while (i < text.size()) {
    if (!is_ident_start(text[i])) {
      ++i;
      continue;
    }
    const std::size_t start = i;
    ++i;
    while (i < text.size() && is_ident_body(text[i])) {
      ++i;
    }
    out.push_back(text.substr(start, i - start));
  }
  return out;
}

static bool find_non_send_path_in_type(const std::string& type_expr,
                                       const std::unordered_map<std::string, std::vector<std::string>>& struct_fields,
                                       const std::unordered_map<std::string, std::string>& struct_field_types,
                                       std::unordered_set<std::string>& visiting_structs, std::string& out_path,
                                       std::string& out_offending) {
  const std::string clean = normalize_type_expr(type_expr);
  if (clean.empty()) {
    return false;
  }
  if (clean == "Rc" || starts_with(clean, "Rc<")) {
    out_path.clear();
    out_offending = clean == "Rc" ? "Rc<T>" : clean;
    return true;
  }
  if (clean == "Arc") {
    return false;
  }

  std::string generic_base;
  std::vector<std::string> generic_args;
  if (parse_generic_parts(clean, generic_base, generic_args)) {
    if (generic_base == "Rc") {
      out_path.clear();
      out_offending = clean;
      return true;
    }
    if (generic_base == "Arc") {
      if (generic_args.empty()) {
        return false;
      }
      return find_non_send_path_in_type(generic_args.front(), struct_fields, struct_field_types, visiting_structs, out_path,
                                        out_offending);
    }
    for (std::size_t i = 0; i < generic_args.size(); ++i) {
      std::string nested_path;
      std::string nested_type;
      if (find_non_send_path_in_type(generic_args[i], struct_fields, struct_field_types, visiting_structs, nested_path,
                                     nested_type)) {
        out_path = nested_path.empty() ? ("arg" + std::to_string(i)) : ("arg" + std::to_string(i) + "." + nested_path);
        out_offending = nested_type;
        return true;
      }
    }
    return false;
  }

  auto fields_it = struct_fields.find(clean);
  if (fields_it == struct_fields.end()) {
    return false;
  }
  if (visiting_structs.find(clean) != visiting_structs.end()) {
    return false;
  }
  visiting_structs.insert(clean);
  for (const std::string& field : fields_it->second) {
    const std::string key = clean + "." + field;
    auto field_type_it = struct_field_types.find(key);
    const std::string field_type = field_type_it == struct_field_types.end() ? "i32" : field_type_it->second;
    std::string nested_path;
    std::string nested_type;
    if (find_non_send_path_in_type(field_type, struct_fields, struct_field_types, visiting_structs, nested_path,
                                   nested_type)) {
      out_path = nested_path.empty() ? field : (field + "." + nested_path);
      out_offending = nested_type;
      visiting_structs.erase(clean);
      return true;
    }
  }
  visiting_structs.erase(clean);
  return false;
}

static bool emit_send_sync_violation(int line, const std::string& path, const std::string& offending_type,
                                     support::DiagnosticSink& diag) {
  diag.error("E_SEND_SYNC_004",
             "line " + std::to_string(line) + ": field `" + path + "` of type `" + offending_type +
                 "` is not Send — replace Rc<T> with Arc<T>");
  return false;
}

static std::string infer_memory_model_type(
    const syntax::AstStatement& st, const MemoryModelState& state,
    const std::unordered_map<std::string, std::vector<std::string>>& struct_fields) {
  const std::string annotation = parse_let_annotation(st.text);
  if (!annotation.empty() && annotation != "Send" && annotation != "Sync") {
    return annotation;
  }
  if (!st.has_expression || !st.expression_valid) {
    return "";
  }
  const std::string expr = normalize_type_expr(st.expression_normalized);
  if (starts_with(expr, "Rc(")) {
    return "Rc<T>";
  }
  if (starts_with(expr, "Arc(")) {
    return "Arc<T>";
  }
  const std::string copied = parse_simple_identifier_expr(expr);
  if (!copied.empty()) {
    auto it = state.value_types.find(copied);
    if (it != state.value_types.end()) {
      return it->second;
    }
  }
  const std::string ctor = parse_constructor_name(expr);
  if (!ctor.empty() && struct_fields.find(ctor) != struct_fields.end()) {
    return ctor;
  }
  std::vector<std::string> closure_params;
  std::string closure_body;
  if (parse_closure_literal(expr, closure_params, closure_body)) {
    return "closure";
  }
  return "";
}

static bool is_assignable_type(TypeKind declared, TypeKind actual) {
  if (declared == actual) {
    return true;
  }
  if ((declared == TypeKind::Rc || declared == TypeKind::Arc || declared == TypeKind::List) &&
      actual != TypeKind::Void && actual != TypeKind::Unknown) {
    return true;
  }
  if (declared == TypeKind::I64 && actual == TypeKind::I32) {
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
    const syntax::AstStatement& st, MemoryModelState& state,
    const std::unordered_map<std::string, std::vector<std::string>>& struct_fields,
    const std::unordered_map<std::string, std::string>& struct_field_types, support::DiagnosticSink& diag) {
  if (st.kind == syntax::StatementKind::Let) {
    const std::string name = parse_let_name(st.text);
    if (!name.empty()) {
      const std::string inferred_type = infer_memory_model_type(st, state, struct_fields);
      if (!inferred_type.empty()) {
        state.value_types[name] = inferred_type;
      }
      if (st.has_expression && st.expression_valid) {
        std::vector<std::string> closure_params;
        std::string closure_body;
        if (parse_closure_literal(st.expression_normalized, closure_params, closure_body)) {
          std::unordered_set<std::string> param_set(closure_params.begin(), closure_params.end());
          std::unordered_set<std::string> captures;
          for (const std::string& ident : extract_identifiers(closure_body)) {
            if (param_set.find(ident) != param_set.end()) {
              continue;
            }
            if (ident == "return" || ident == "if" || ident == "else" || ident == "true" || ident == "false" ||
                ident == "match" || ident == "let" || ident == "while" || ident == "for") {
              continue;
            }
            if (state.value_types.find(ident) != state.value_types.end()) {
              captures.insert(ident);
            }
          }
          state.closure_captures[name] = std::vector<std::string>(captures.begin(), captures.end());
        }
      }
    }
  }

  auto check_symbol_send = [&](const std::string& symbol, const std::string& prefix) {
    auto type_it = state.value_types.find(symbol);
    if (type_it != state.value_types.end()) {
      std::unordered_set<std::string> visiting_structs;
      std::string bad_path;
      std::string bad_type;
      if (find_non_send_path_in_type(type_it->second, struct_fields, struct_field_types, visiting_structs, bad_path,
                                     bad_type)) {
        const std::string full_path =
            prefix.empty() ? (bad_path.empty() ? symbol : symbol + "." + bad_path)
                           : (bad_path.empty() ? prefix : prefix + "." + bad_path);
        return emit_send_sync_violation(st.line, full_path, bad_type, diag);
      }
    }
    auto closure_it = state.closure_captures.find(symbol);
    if (closure_it != state.closure_captures.end()) {
      for (const std::string& captured : closure_it->second) {
        auto capture_type = state.value_types.find(captured);
        if (capture_type == state.value_types.end()) {
          continue;
        }
        std::unordered_set<std::string> visiting_structs;
        std::string bad_path;
        std::string bad_type;
        if (find_non_send_path_in_type(capture_type->second, struct_fields, struct_field_types, visiting_structs,
                                       bad_path, bad_type)) {
          std::string capture_path = prefix.empty() ? symbol : prefix;
          capture_path += ".capture." + captured;
          if (!bad_path.empty()) {
            capture_path += "." + bad_path;
          }
          return emit_send_sync_violation(st.line, capture_path, bad_type, diag);
        }
      }
    }
    return true;
  };

  if (is_spawn_statement(st.text)) {
    const std::string direct = extract_call_arg_ident(st.text, "spawn");
    if (!direct.empty()) {
      if (!check_symbol_send(direct, direct)) {
        return false;
      }
      return true;
    }
    for (const auto& pair : state.value_types) {
      if (contains_word(st.text, pair.first) && !check_symbol_send(pair.first, pair.first)) {
        return false;
      }
    }
  }
  return true;
}

static bool validate_stateful_call_usage(const syntax::AstStatement& st,
                                         const std::unordered_map<std::string, FunctionStateContract>& contracts,
                                         const std::unordered_map<std::string, StateRef>& value_states,
                                         const std::unordered_set<std::string>& ambiguous_values,
                                         support::DiagnosticSink& diag) {
  if (!st.has_expression || !st.expression_valid) {
    return true;
  }
  CallExprInfo call;
  if (!parse_call_expr_info(st.expression_normalized, call)) {
    return true;
  }
  auto contract_it = contracts.find(call.callee);
  if (contract_it == contracts.end()) {
    return true;
  }
  const FunctionStateContract& contract = contract_it->second;
  const std::size_t limit = std::min(call.args.size(), contract.param_states.size());
  for (std::size_t i = 0; i < limit; ++i) {
    if (!contract.param_states[i].tagged) {
      continue;
    }
    const std::string arg_ident = parse_simple_identifier_expr(trim_copy(call.args[i]));
    if (arg_ident.empty()) {
      diag.error("E_STATE_MISMATCH_ARG",
                 "line " + std::to_string(st.line) + ": argument " + std::to_string(i + 1) + " to '" + call.callee +
                     "' must be in state " + state_ref_display(contract.param_states[i]));
      return false;
    }
    if (ambiguous_values.find(arg_ident) != ambiguous_values.end()) {
      diag.error("E_STATE_AMBIGUOUS",
                 "line " + std::to_string(st.line) + ": state of '" + arg_ident +
                     "' is ambiguous at call to '" + call.callee + "'");
      return false;
    }
    auto state_it = value_states.find(arg_ident);
    if (state_it == value_states.end() || !state_it->second.tagged) {
      diag.warn("W_STATE_AMBIGUOUS",
                "line " + std::to_string(st.line) + ": cannot determine current state of '" + arg_ident + "'");
      diag.error("E_STATE_AMBIGUOUS",
                 "line " + std::to_string(st.line) + ": state of '" + arg_ident +
                     "' is unknown for call to '" + call.callee + "'");
      return false;
    }
    if (!is_same_state_ref(state_it->second, contract.param_states[i])) {
      diag.error("E_STATE_MISMATCH_ARG",
                 "line " + std::to_string(st.line) + ": call '" + call.callee + "' expects argument '" + arg_ident +
                     "' in state " + state_ref_display(contract.param_states[i]) + ", got " +
                     state_ref_display(state_it->second));
      return false;
    }
  }
  return true;
}

static bool infer_statement_assigned_state(const syntax::AstStatement& st,
                                           const std::unordered_map<std::string, FunctionStateContract>& contracts,
                                           const std::unordered_map<std::string, StateRef>& value_states,
                                           std::string& target_name, StateRef& out_state) {
  target_name.clear();
  out_state = StateRef{};

  if (st.kind == syntax::StatementKind::Let) {
    target_name = parse_let_name(st.text);
    const std::string annotation = parse_let_annotation(st.text);
    if (!annotation.empty()) {
      std::string set_name;
      std::string variant_name;
      if (split_state_annotation(annotation, set_name, variant_name)) {
        out_state.tagged = true;
        out_state.set = set_name;
        out_state.variant = variant_name;
        return !target_name.empty();
      }
    }
  } else if (st.kind == syntax::StatementKind::Assign) {
    target_name = parse_assignment_target(st.text);
    if (target_name.find('.') != std::string::npos) {
      target_name.clear();
      return false;
    }
  } else {
    return false;
  }

  if (target_name.empty() || !st.has_expression || !st.expression_valid) {
    return false;
  }

  const std::string rhs_ident = parse_simple_identifier_expr(trim_copy(st.expression_normalized));
  if (!rhs_ident.empty()) {
    auto it = value_states.find(rhs_ident);
    if (it != value_states.end() && it->second.tagged) {
      out_state = it->second;
      return true;
    }
  }

  CallExprInfo call;
  if (parse_call_expr_info(st.expression_normalized, call)) {
    auto contract_it = contracts.find(call.callee);
    if (contract_it != contracts.end() && contract_it->second.return_state.tagged) {
      out_state = contract_it->second.return_state;
      return true;
    }
  }
  return false;
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

  std::string current_intent_goal;
  for (const std::string& line : program.top_level_lines) {
    const std::string clean = trim_copy(line);
    if (starts_with(clean, "intent ")) {
      current_intent_goal.clear();
      continue;
    }
    if (starts_with(clean, "goal:")) {
      const std::string goal = trim_copy(clean.substr(5));
      if (supported_intent_goals().find(goal) == supported_intent_goals().end()) {
        diag.error("E_TYPE_INTENT_GOAL", "unsupported intent goal: '" + goal + "'");
        return false;
      }
      current_intent_goal = goal;
      continue;
    }
    if (starts_with(clean, "strategy:")) {
      const std::string strategy = trim_copy(clean.substr(9));
      if (!valid_intent_strategy(strategy)) {
        diag.error("E_TYPE_INTENT_STRATEGY", "invalid intent strategy: '" + strategy + "'");
        return false;
      }
      if (current_intent_goal == "off") {
        diag.error("E_TYPE_INTENT_STRATEGY", "strategy cannot be set when goal is 'off'");
        return false;
      }
      continue;
    }
  }

  return true;
}

static TypeKind resolve_declared_user_type(const std::string& type_name,
                                           const std::unordered_map<std::string, TypeKind>& aliases,
                                           const std::unordered_set<std::string>& struct_names,
                                           const std::unordered_set<std::string>& enum_names);

static bool validate_extern_declarations(const syntax::AstProgram& program, support::DiagnosticSink& diag) {
  const auto aliases = collect_type_aliases(program);
  const auto struct_names = collect_struct_names(program);
  const auto enum_names = collect_enum_names(program);
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
    if (resolve_declared_user_type(ret, aliases, struct_names, enum_names) == TypeKind::Unknown) {
      diag.error("E0006", "unsupported extern return type '" + ret + "'");
      return false;
    }
  }
  return true;
}

static bool flow_action_looks_side_effectful(const std::string& action) {
  const std::string clean = trim_copy(action);
  if (clean.empty()) {
    return false;
  }
  return clean.find('(') != std::string::npos || clean.find('.') != std::string::npos;
}

static bool validate_flow_constructs(const syntax::AstProgram& program, support::DiagnosticSink& diag) {
  std::unordered_set<std::string> flow_names;
  std::unordered_set<std::string> function_names;
  for (const auto& fn : program.functions) {
    if (!fn.name.empty()) {
      function_names.insert(fn.name);
    }
  }
  for (const auto& flow : program.flow_defs) {
    if (flow.name.empty()) {
      diag.error("E_FLOW_306", "flow declaration requires a valid identifier name", program.source_path, flow.line, 1);
      return false;
    }
    if (!flow_names.insert(flow.name).second) {
      diag.error("E_FLOW_307", "duplicate flow declaration '" + flow.name + "'", program.source_path, flow.line, 1);
      return false;
    }
    if (function_names.find(flow.name) != function_names.end()) {
      diag.error("E_FLOW_308", "flow name '" + flow.name + "' conflicts with existing function name",
                 program.source_path, flow.line, 1);
      return false;
    }
    if (flow.steps.empty()) {
      diag.error("E_FLOW_300", "flow '" + flow.name + "' must declare at least one step", program.source_path, flow.line, 1);
      return false;
    }

    std::unordered_set<std::string> step_names;
    for (const auto& step : flow.steps) {
      if (!step.name.empty()) {
        auto inserted = step_names.insert(step.name);
        if (!inserted.second) {
          diag.error("E_FLOW_301", "duplicate flow step name '" + step.name + "'", program.source_path, step.line, 1);
          return false;
        }
      }
      if (step.has_timeout && step.timeout_ms <= 0) {
        diag.error("E_FLOW_302", "flow step timeout must be greater than 0ms", program.source_path, step.line, 1);
        return false;
      }
      if (step.has_retry && step.retry_count > 0 && !step.idempotent) {
        diag.error("E_FLOW_303", "flow step with retry requires idempotent contract", program.source_path, step.line, 1);
        return false;
      }
      if (step.irreversible && !step.undo_action.empty()) {
        diag.error("E_FLOW_304", "flow step cannot declare both undo and irreversible", program.source_path, step.line, 1);
        return false;
      }
      if (flow_action_looks_side_effectful(step.action) && step.undo_action.empty() && !step.irreversible) {
        diag.error("E_FLOW_305", "side-effect flow step requires undo or irreversible", program.source_path, step.line, 1);
        return false;
      }
    }
  }
  return true;
}

static bool validate_state_sets(const syntax::AstProgram& program, support::DiagnosticSink& diag) {
  for (const auto& entry : program.state_sets) {
    const std::string& set_name = entry.first;
    const std::vector<std::string>& variants = entry.second;
    if (variants.size() < 2) {
      diag.error("E_STATE_UNKNOWN_VARIANT", "state set '" + set_name + "' must contain at least two variants");
      return false;
    }
    std::unordered_set<std::string> seen;
    for (const std::string& variant : variants) {
      if (!seen.insert(variant).second) {
        diag.error("E_STATE_UNKNOWN_VARIANT",
                   "state set '" + set_name + "' contains duplicate variant '" + variant + "'");
        return false;
      }
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

static bool has_any_angle_bracket(const std::string& type_name) {
  return type_name.find('<') != std::string::npos || type_name.find('>') != std::string::npos;
}

static TypeKind resolve_declared_user_type(const std::string& type_name,
                                           const std::unordered_map<std::string, TypeKind>& aliases,
                                           const std::unordered_set<std::string>& struct_names,
                                           const std::unordered_set<std::string>& enum_names) {
  const std::string clean = strip_state_annotation(type_name);
  std::string generic_base;
  std::vector<std::string> generic_args;
  if (parse_generic_parts(clean, generic_base, generic_args)) {
    if (!is_builtin_generic_base(generic_base)) {
      return TypeKind::Unknown;
    }
    const std::size_t expected = builtin_generic_arity(generic_base);
    if (expected == 0 || generic_args.size() != expected) {
      return TypeKind::Unknown;
    }
    for (const std::string& arg : generic_args) {
      if (resolve_declared_user_type(arg, aliases, struct_names, enum_names) == TypeKind::Unknown) {
        return TypeKind::Unknown;
      }
    }
    return parse_type_name(generic_base);
  }
  if (has_any_angle_bracket(clean)) {
    return TypeKind::Unknown;
  }
  const TypeKind direct = resolve_declared_type(clean, aliases);
  if (direct != TypeKind::Unknown) {
    return direct;
  }
  if (struct_names.find(clean) != struct_names.end()) {
    return TypeKind::StructType;
  }
  if (enum_names.find(clean) != enum_names.end()) {
    return TypeKind::EnumType;
  }
  return TypeKind::Unknown;
}

static bool validate_declared_type_expr(const std::string& type_name,
                                        const std::unordered_map<std::string, TypeKind>& aliases,
                                        const std::unordered_set<std::string>& struct_names,
                                        const std::unordered_set<std::string>& enum_names,
                                        std::string& error,
                                        int depth_limit = 32) {
  error.clear();
  if (depth_limit <= 0) {
    error = "generic type nesting is too deep";
    return false;
  }
  const std::string clean = strip_state_annotation(type_name);
  std::string generic_base;
  std::vector<std::string> generic_args;
  if (parse_generic_parts(clean, generic_base, generic_args)) {
    if (!is_builtin_generic_base(generic_base)) {
      error = "unsupported generic base type '" + generic_base + "'";
      return false;
    }
    const std::size_t expected = builtin_generic_arity(generic_base);
    if (expected == 0 || generic_args.size() != expected) {
      error = "generic type '" + generic_base + "' expects " + std::to_string(expected) + " argument(s) but got " +
              std::to_string(generic_args.size());
      return false;
    }
    for (const std::string& arg : generic_args) {
      std::string nested_error;
      if (!validate_declared_type_expr(arg, aliases, struct_names, enum_names, nested_error, depth_limit - 1)) {
        error = nested_error;
        return false;
      }
    }
    return true;
  }
  if (has_any_angle_bracket(clean)) {
    error = "malformed generic type syntax '" + clean + "'";
    return false;
  }
  if (resolve_declared_user_type(clean, aliases, struct_names, enum_names) != TypeKind::Unknown) {
    return true;
  }
  error = "unsupported type '" + clean + "'";
  return false;
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

static bool is_parenthesized_tuple_expr(const std::string& expr) {
  const std::string clean = trim_copy(expr);
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
  if (is_parenthesized_tuple_expr(clean_expr)) {
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

  if (st.expression_ast) {
    hir::TypeEnv hir_env;
    hir_env.scope = &scope;
    hir_env.enum_variants = &enum_variants;
    hir_env.struct_bindings = &struct_bindings;
    hir_env.struct_fields = &struct_fields;
    hir_env.struct_field_types = &struct_field_types;
    hir_env.struct_methods = &struct_methods;
    hir_env.function_returns = &function_returns;
    hir_env.function_arity = &function_arity;
    hir_env.struct_names = &struct_names;
    std::string hir_error;
    const TypeKind hir_type = hir::infer_expression(hir::lower_ast_expr(st.expression_ast), hir_env, hir_error);
    if (hir_type != TypeKind::Unknown) {
      out = hir_type;
      return true;
    }
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

static bool comptime_expression_contains_call(const std::string& expr) {
  for (std::size_t i = 0; i < expr.size();) {
    const char ch = expr[i];
    if (!is_ident_start(ch)) {
      ++i;
      continue;
    }
    ++i;
    while (i < expr.size() && is_ident_body(expr[i])) {
      ++i;
    }
    std::size_t j = i;
    while (j < expr.size() && std::isspace(static_cast<unsigned char>(expr[j]))) {
      ++j;
    }
    if (j < expr.size() && expr[j] == '(') {
      return true;
    }
  }
  return false;
}

static bool build_comptime_scope(const syntax::AstProgram& program,
                                 const std::unordered_map<std::string, TypeKind>& function_returns,
                                 const std::unordered_map<std::string, std::size_t>& function_arity,
                                 const std::unordered_set<std::string>& struct_names,
                                 std::unordered_map<std::string, TypeKind>& out_scope,
                                 support::DiagnosticSink& diag) {
  out_scope.clear();
  std::unordered_map<std::string, std::string> struct_bindings;
  for (const auto& binding : program.comptime_bindings) {
    if (binding.name.empty() || !is_ident_start(binding.name[0])) {
      diag.error("E_COMPTIME_400",
                 "line " + std::to_string(binding.line) + ": invalid comptime binding name '" + binding.name + "'");
      return false;
    }
    for (std::size_t i = 1; i < binding.name.size(); ++i) {
      if (!is_ident_body(binding.name[i])) {
        diag.error("E_COMPTIME_400",
                   "line " + std::to_string(binding.line) + ": invalid comptime binding name '" + binding.name + "'");
        return false;
      }
    }
    if (comptime_expression_contains_call(binding.expression)) {
      diag.error("E_COMPTIME_401",
                 "line " + std::to_string(binding.line) +
                     ": comptime binding cannot call runtime functions in v1.8 basic mode");
      return false;
    }
    syntax::AstStatement st;
    st.kind = syntax::StatementKind::Expr;
    st.line = binding.line;
    st.has_expression = true;
    st.expression_valid = true;
    st.expression_normalized = binding.expression;
    TypeKind expr_type = TypeKind::Unknown;
    std::string expr_error;
    if (!typecheck_statement_expression(st, binding.line, out_scope, program.enum_variant_tags, struct_bindings,
                                        program.struct_fields, program.struct_field_types, program.struct_methods,
                                        function_returns, function_arity, struct_names, expr_type, expr_error)) {
      diag.error("E_COMPTIME_402", "line " + std::to_string(binding.line) + ": " + expr_error);
      return false;
    }
    if (expr_type == TypeKind::Void || expr_type == TypeKind::Unknown) {
      diag.error("E_COMPTIME_403",
                 "line " + std::to_string(binding.line) + ": comptime binding requires concrete value type");
      return false;
    }
    out_scope[binding.name] = expr_type;
  }
  return true;
}

bool TypeChecker::check(const syntax::AstProgram& program, support::DiagnosticSink& diag) const {
  if (program.source.empty()) {
    diag.error("E0001", "source is empty");
    return false;
  }
  if (!program.parse_errors.empty()) {
    syntax::SourceMap source_map;
    const std::uint32_t file_id = source_map.add_file(program.source_path, program.source);
    if (!program.parse_error_details.empty()) {
      for (const auto& detail : program.parse_error_details) {
        int line = detail.line > 0 ? detail.line : 1;
        int column = 1;
        int end_column = 1;
        if (detail.span.has_value()) {
          syntax::Span span = *detail.span;
          span.file_id = file_id;
          const auto begin = source_map.lookup_line_col(span);
          line = static_cast<int>(begin.first);
          column = static_cast<int>(begin.second);
          syntax::Span end = span;
          if (end.hi > end.lo) {
            end.lo = end.hi - 1;
            end.hi = end.lo + 1;
          }
          const auto end_pos = source_map.lookup_line_col(end);
          end_column = static_cast<int>(end_pos.second + (span.hi > span.lo ? 1 : 0));
          if (end_column < column) {
            end_column = column;
          }
        }
        diag.error(classify_parse_error(detail.message), "syntax error: line " + std::to_string(line) + ": " + detail.message,
                   program.source_path, line, column, line, end_column);
      }
    } else {
      for (const std::string& message : program.parse_errors) {
        diag.error(classify_parse_error(message), "syntax error: " + message, program.source_path);
      }
    }
    return false;
  }
  if (!validate_feature_edges(program, diag)) {
    return false;
  }
  if (!validate_extern_declarations(program, diag)) {
    return false;
  }
  if (!validate_flow_constructs(program, diag)) {
    return false;
  }
  if (!validate_state_sets(program, diag)) {
    return false;
  }
  const auto aliases = collect_type_aliases(program);
  const auto struct_names = collect_struct_names(program);
  const auto enum_names = collect_enum_names(program);
  const auto function_returns = collect_function_return_types(program, aliases, struct_names, enum_names);
  const auto function_arity = collect_function_arity(program);
  const auto function_state_contracts = collect_function_state_contracts(program, diag);
  if (diag.has_errors()) {
    return false;
  }
  std::unordered_map<std::string, TypeKind> comptime_scope;
  if (!build_comptime_scope(program, function_returns, function_arity, struct_names, comptime_scope, diag)) {
    return false;
  }

  for (const auto& [field_key, field_type] : program.struct_field_types) {
    std::string type_error;
    if (!validate_declared_type_expr(field_type, aliases, struct_names, enum_names, type_error)) {
      diag.error("E0030",
                 "unsupported field type '" + field_type + "' for '" + field_key + "': " + type_error);
      return false;
    }
  }
  for (const auto& [variant, payload_type] : program.enum_variant_payload_types) {
    std::string type_error;
    if (!validate_declared_type_expr(payload_type, aliases, struct_names, enum_names, type_error)) {
      diag.error("E0031",
                 "unsupported enum payload type '" + payload_type + "' for variant '" + variant + "': " + type_error);
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
  if (program.has_main && !program.top_level_statements.empty()) {
    const auto& st = program.top_level_statements.front();
    diag.error("E0029",
               "line " + std::to_string(st.line) +
                   ": top-level executable statements are not allowed when func main() is defined");
    return false;
  }
  for (const auto& fn : program.functions) {
    if (fn.name.empty()) {
      diag.error("E0003", "invalid function header at line " + std::to_string(fn.header_line));
      return false;
    }
    if (!fn.return_type.empty()) {
      std::string return_error;
      if (!validate_declared_type_expr(fn.return_type, aliases, struct_names, enum_names, return_error)) {
        diag.error("E0006",
                   "unsupported return type '" + fn.return_type + "' in function '" + fn.name + "': " + return_error);
        return false;
      }
    }
    for (const std::string& param_type : fn.param_types) {
      if (param_type.empty()) {
        continue;
      }
      std::string param_error;
      if (!validate_declared_type_expr(param_type, aliases, struct_names, enum_names, param_error)) {
        diag.error("E0006",
                   "unsupported parameter type '" + param_type + "' in function '" + fn.name + "': " + param_error);
        return false;
      }
    }
  }

  for (const auto& fn : program.functions) {
    std::unordered_map<std::string, TypeKind> scope = comptime_scope;
    MemoryModelState memory_state;
    std::unordered_map<std::string, bool> opened_resources;
    std::unordered_map<std::string, StateRef> value_states;
    std::unordered_set<std::string> ambiguous_state_values;
    std::unordered_map<std::string, std::string> struct_bindings;
    std::string method_owner;
    std::string method_name;
    const bool is_method =
        split_owner_and_method(fn.name, method_owner, method_name) && struct_names.find(method_owner) != struct_names.end();
    for (std::size_t param_index = 0; param_index < fn.params.size(); ++param_index) {
      const std::string& param = fn.params[param_index];
      if (!param.empty()) {
        if (is_method && param == "self") {
          scope[param] = TypeKind::StructType;
          struct_bindings[param] = method_owner;
        } else {
          TypeKind param_type = TypeKind::I32;
          if (param_index < fn.param_types.size()) {
            const std::string& declared_param = fn.param_types[param_index];
            if (!declared_param.empty()) {
              const TypeKind resolved = resolve_declared_user_type(declared_param, aliases, struct_names, enum_names);
              if (resolved != TypeKind::Unknown) {
                param_type = resolved;
              }
            }
          }
          scope[param] = param_type;
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
    auto fn_state_it = function_state_contracts.find(fn.name);
    const FunctionStateContract* fn_state_contract =
        fn_state_it == function_state_contracts.end() ? nullptr : &fn_state_it->second;
    if (fn_state_contract != nullptr) {
      const std::size_t param_limit = std::min(fn.params.size(), fn_state_contract->param_states.size());
      for (std::size_t i = 0; i < param_limit; ++i) {
        if (!fn_state_contract->param_states[i].tagged) {
          continue;
        }
        value_states[fn.params[i]] = fn_state_contract->param_states[i];
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
      if (!validate_memory_model_statement(st, memory_state, program.struct_fields, program.struct_field_types, diag)) {
        return false;
      }
      if (!validate_typestate_statement(st, opened_resources, diag)) {
        return false;
      }
      if (!validate_stateful_call_usage(st, function_state_contracts, value_states, ambiguous_state_values, diag)) {
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
          StateRef declared_state_ref;
          std::string state_error_code;
          std::string state_error_message;
          if (!parse_state_ref(annotation, program, declared_state_ref, state_error_code, state_error_message)) {
            diag.error(state_error_code, "line " + std::to_string(st.line) + ": " + state_error_message);
            return false;
          }
          std::string annotation_error;
          if (!validate_declared_type_expr(annotation, aliases, struct_names, enum_names, annotation_error)) {
            diag.error("E0006",
                       "line " + std::to_string(st.line) + ": unsupported let annotation '" + annotation + "': " +
                           annotation_error);
            return false;
          }
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
          if (declared_state_ref.tagged) {
            value_states[name] = declared_state_ref;
          }
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

      std::string state_target;
      StateRef assigned_state;
      if (infer_statement_assigned_state(st, function_state_contracts, value_states, state_target, assigned_state) &&
          !state_target.empty() && assigned_state.tagged) {
        value_states[state_target] = assigned_state;
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
        if (expr_type != TypeKind::I32 && expr_type != TypeKind::I64 && expr_type != TypeKind::Bool && expr_type != TypeKind::Option &&
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
        if (fn_state_contract != nullptr && fn_state_contract->return_state.tagged) {
          StateRef actual_state;
          bool resolved = false;
          if (st.has_expression && st.expression_valid) {
            const std::string ret_ident = parse_simple_identifier_expr(trim_copy(st.expression_normalized));
            if (!ret_ident.empty()) {
              auto state_it = value_states.find(ret_ident);
              if (state_it != value_states.end() && state_it->second.tagged) {
                actual_state = state_it->second;
                resolved = true;
              }
              if (ambiguous_state_values.find(ret_ident) != ambiguous_state_values.end()) {
                diag.error("E_STATE_AMBIGUOUS",
                           "line " + std::to_string(st.line) + ": return state of '" + ret_ident + "' is ambiguous");
                return false;
              }
            }
            if (!resolved) {
              CallExprInfo ret_call;
              if (parse_call_expr_info(st.expression_normalized, ret_call)) {
                auto ret_contract_it = function_state_contracts.find(ret_call.callee);
                if (ret_contract_it != function_state_contracts.end() && ret_contract_it->second.return_state.tagged) {
                  actual_state = ret_contract_it->second.return_state;
                  resolved = true;
                }
              }
            }
          }
          if (!resolved || !is_same_state_ref(actual_state, fn_state_contract->return_state)) {
            diag.error("E_STATE_MISMATCH_RETURN",
                       "line " + std::to_string(st.line) + ": function '" + fn.name + "' must return " +
                           state_ref_display(fn_state_contract->return_state) +
                           (resolved ? ", got " + state_ref_display(actual_state) : ", got unknown state"));
            return false;
          }
        }
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

  std::unordered_map<std::string, TypeKind> top_scope = comptime_scope;
  MemoryModelState top_memory_state;
  std::unordered_map<std::string, bool> top_opened_resources;
  std::unordered_map<std::string, StateRef> top_value_states;
  std::unordered_set<std::string> top_ambiguous_state_values;
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
    if (!validate_memory_model_statement(st, top_memory_state, program.struct_fields, program.struct_field_types, diag)) {
      return false;
    }
    if (!validate_typestate_statement(st, top_opened_resources, diag)) {
      return false;
    }
    if (!validate_stateful_call_usage(st, function_state_contracts, top_value_states, top_ambiguous_state_values, diag)) {
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
        StateRef declared_state_ref;
        std::string state_error_code;
        std::string state_error_message;
        if (!parse_state_ref(annotation, program, declared_state_ref, state_error_code, state_error_message)) {
          diag.error(state_error_code, "line " + std::to_string(st.line) + ": " + state_error_message);
          return false;
        }
        std::string annotation_error;
        if (!validate_declared_type_expr(annotation, aliases, struct_names, enum_names, annotation_error)) {
          diag.error("E0006",
                     "line " + std::to_string(st.line) + ": unsupported let annotation '" + annotation + "': " +
                         annotation_error);
          return false;
        }
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
        if (declared_state_ref.tagged) {
          top_value_states[name] = declared_state_ref;
        }
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
    std::string top_state_target;
    StateRef top_assigned_state;
    if (infer_statement_assigned_state(st, function_state_contracts, top_value_states, top_state_target, top_assigned_state) &&
        !top_state_target.empty() && top_assigned_state.tagged) {
      top_value_states[top_state_target] = top_assigned_state;
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
      if (expr_type != TypeKind::I32 && expr_type != TypeKind::I64 && expr_type != TypeKind::Bool && expr_type != TypeKind::Option &&
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
