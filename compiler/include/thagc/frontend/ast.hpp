#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace thagc::syntax {

enum class StatementKind {
  Return,
  Let,
  If,
  Else,
  While,
  For,
  Match,
  Expr,
};

struct AstStatement {
  StatementKind kind = StatementKind::Expr;
  std::string text;
  int line = 0;
  int indent = 0;
  bool has_expression = false;
  bool expression_valid = true;
  std::string expression_normalized;
  std::string expression_error;
};

struct AstFunction {
  std::string name;
  std::string return_type;
  int header_line = 0;
  int header_indent = 0;
  std::vector<AstStatement> body;
};

struct AstProgram {
  std::vector<std::string> top_level_lines;
  std::vector<AstStatement> top_level_statements;
  std::vector<AstFunction> functions;
  std::vector<std::string> imports;
  std::vector<std::string> extern_decls;
  std::vector<std::string> structs;
  std::vector<std::string> impls;
  std::vector<std::string> enums;
  std::vector<std::string> type_aliases;
  std::vector<std::string> traits;
  std::vector<std::string> impl_for_headers;
  std::vector<std::string> public_decls;
  std::unordered_map<std::string, int> enum_variant_tags;
  std::unordered_map<std::string, std::vector<std::string>> trait_required_methods;
  std::unordered_map<std::string, std::vector<std::string>> impl_for_methods;
  std::vector<std::string> parse_errors;
  int match_count = 0;
  int range_loop_count = 0;
  int if_expr_count = 0;
  int closure_count = 0;
  int unsafe_count = 0;
  int enum_payload_count = 0;
  int defer_scope_count = 0;
  int comptime_count = 0;
  int extension_impl_count = 0;
  int visibility_count = 0;
  int tuple_destruct_count = 0;
  int array_literal_count = 0;
  int slice_expr_count = 0;
  int loop_label_count = 0;
  int raw_string_count = 0;
  int interpolated_string_count = 0;
  int result_sugar_count = 0;
  bool has_main = false;
  int main_return_literal = 0;
  std::string source;
};

}  // namespace thagc::syntax
