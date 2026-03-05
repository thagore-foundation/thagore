#pragma once

#include "thagc/frontend/parser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace thagc::syntax {

// ---------------------------------------------------------------------------
// Shared data types
// ---------------------------------------------------------------------------

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

struct ParsedFunctionParam {
  std::string name;
  std::string type;
};

// ---------------------------------------------------------------------------
// Inline utilities — used across all frontend translation units
// ---------------------------------------------------------------------------

inline std::string trim(const std::string& text) {
  std::size_t left = 0;
  while (left < text.size() && std::isspace(static_cast<unsigned char>(text[left])))
    ++left;
  std::size_t right = text.size();
  while (right > left && std::isspace(static_cast<unsigned char>(text[right - 1])))
    --right;
  return text.substr(left, right - left);
}

inline bool starts_with(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

inline bool ends_with(const std::string& text, const std::string& suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline int leading_indent(const std::string& line) {
  int indent = 0;
  for (char ch : line) {
    if (ch == ' ') { ++indent; continue; }
    if (ch == '\t') { indent += 2; continue; }
    break;
  }
  return indent;
}

inline std::string strip_comments(const std::string& line) {
  std::size_t cut = line.size();
  const std::size_t hash = line.find('#');
  if (hash != std::string::npos) cut = std::min(cut, hash);
  const std::size_t slash = line.find("//");
  if (slash != std::string::npos) cut = std::min(cut, slash);
  return line.substr(0, cut);
}

inline bool is_identifier(const std::string& text) {
  if (text.empty() || !(std::isalpha(static_cast<unsigned char>(text[0])) || text[0] == '_'))
    return false;
  for (std::size_t i = 1; i < text.size(); ++i) {
    if (!(std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_'))
      return false;
  }
  return true;
}

inline void add_parse_error(AstProgram& program, int line, const std::string& message) {
  program.parse_errors.push_back("line " + std::to_string(line) + ": " + message);
}

// ---------------------------------------------------------------------------
// Declarations: syntax.cpp
// ---------------------------------------------------------------------------

std::string function_name_from_header(const std::string& line);
std::vector<ParsedFunctionParam> function_param_specs_from_header(const std::string& line);
std::vector<std::string> function_params_from_header(const std::string& line);
std::vector<std::string> function_param_types_from_header(const std::string& line);
std::string method_name_from_line(const std::string& line);
std::string function_return_type_from_header(const std::string& line);
bool parse_impl_for_header(const std::string& line, std::string& trait_name, std::string& type_name);
bool parse_impl_type_header(const std::string& line, std::string& type_name);
std::string enum_variant_name_from_line(const std::string& line);
std::string enum_variant_payload_type_from_line(const std::string& line);
std::string enum_name_from_header(const std::string& line);
std::string struct_name_from_header(const std::string& line);
std::string struct_field_name_from_line(const std::string& line);
std::string struct_field_type_from_line(const std::string& line);
bool parse_state_header(const std::string& line, std::string& state_name,
                        std::vector<std::string>& variants, std::string& error);
bool parse_extern_function_declaration(const std::string& line, AstExternFunction& out);

// ---------------------------------------------------------------------------
// Declarations: macro.cpp
// ---------------------------------------------------------------------------

bool parse_macro_declaration(const std::string& line, AstMacro& out, std::string& error);
bool expand_macros(const std::string& expression,
                   const std::unordered_map<std::string, AstMacro>& macros,
                   std::string& out_expression, std::string& error);
bool is_interpolated_literal(const std::string& text);
bool parse_interpolated_string_literal(const std::string& text, AstInterpolatedString& out);
bool parse_closure_literal(const std::string& text, std::vector<std::string>& params,
                           std::string& body, bool& block_body);
std::vector<std::string> collect_closure_captures(const std::vector<std::string>& params,
                                                  const std::string& body);

// ---------------------------------------------------------------------------
// Declarations: expr.cpp
// ---------------------------------------------------------------------------

bool is_simple_assignable_target(const std::string& text);
bool is_assignment_line(const std::string& line);
bool is_labeled_loop_header(const std::string& line, std::string& loop_head);
bool valid_control_header(const std::string& keyword, const std::string& line);
bool valid_for_header(const std::string& line);
void collect_feature_counters(const std::string& line, AstProgram& program);
AstStatement build_statement_from_line(AstProgram& program, const SourceLine& body,
                                       const std::unordered_map<std::string, AstMacro>& macros);
int parse_return_literal(const std::string& line);

// ---------------------------------------------------------------------------
// Declarations: import.cpp
// ---------------------------------------------------------------------------

bool parse_import_decl(const std::string& line, AstImport& out, std::string& error);

// ---------------------------------------------------------------------------
// Declarations: flow.cpp
// ---------------------------------------------------------------------------

std::string flow_name_from_header(const std::string& header);
bool parse_flow_step_header(const std::string& line, AstFlowStep& out_step, std::string& error);
void parse_flow_step_directive(AstProgram& program, AstFlowStep& step, const SourceLine& line);
std::string substitute_known_identifiers(const std::string& expr,
                                         const std::unordered_map<std::string, std::string>& known);

}  // namespace thagc::syntax
