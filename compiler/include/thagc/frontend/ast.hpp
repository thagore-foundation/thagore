#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace thagc::syntax {

enum class StatementKind {
  Return,
  Let,
  Assign,
  Defer,
  Break,
  Continue,
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
  std::string target;
  int line = 0;
  int indent = 0;
  bool has_await = false;
  bool has_expression = false;
  bool expression_valid = true;
  std::string expression_normalized;
  std::string expression_error;
};

struct AstClosure {
  std::vector<std::string> captures;
  std::vector<std::string> params;
  std::string body;
  int line = 0;
  bool block_body = false;
};

struct AstInterpolatedSegment {
  bool is_expression = false;
  std::string text;
};

struct AstInterpolatedString {
  std::string raw;
  std::vector<AstInterpolatedSegment> segments;
  int line = 0;
};

struct AstFunction {
  std::string name;
  std::vector<std::string> params;
  std::vector<std::string> param_types;
  std::string return_type;
  int header_line = 0;
  int header_indent = 0;
  bool is_pub = false;
  bool is_async = false;
  std::vector<AstStatement> body;
};

struct AstExternFunction {
  std::string name;
  std::vector<std::string> param_types;
  std::string return_type;
  int line = 0;
};

struct AstImport {
  bool is_from_import = false;
  std::vector<std::string> module_path;
  std::string alias;
  std::vector<std::string> symbols;
  int line = 0;
  int column = 1;
  std::string raw;
};

struct AstFlowStep {
  std::string name;
  std::string action;
  std::string undo_action;
  int line = 0;
  int retry_count = 0;
  bool has_retry = false;
  int timeout_ms = 0;
  bool has_timeout = false;
  bool idempotent = false;
  bool irreversible = false;
};

struct AstFlow {
  std::string header;
  std::string name;
  int line = 0;
  int indent = 0;
  std::vector<AstFlowStep> steps;
};

struct AstProgram {
  std::string source_path;
  std::vector<std::string> top_level_lines;
  std::vector<AstStatement> top_level_statements;
  std::vector<AstFunction> functions;
  std::vector<AstExternFunction> extern_functions;
  std::vector<AstImport> imports;
  std::vector<std::string> extern_decls;
  std::vector<std::string> structs;
  std::unordered_map<std::string, std::vector<std::string>> struct_fields;
  std::unordered_map<std::string, std::string> struct_field_types;
  std::unordered_map<std::string, std::vector<std::string>> struct_methods;
  std::vector<std::string> impls;
  std::vector<std::string> enums;
  std::unordered_map<std::string, std::vector<std::string>> enum_variants;
  std::vector<std::string> flows;
  std::vector<AstFlow> flow_defs;
  std::vector<std::string> intents;
  std::vector<std::string> type_aliases;
  std::vector<std::string> traits;
  std::vector<std::string> impl_for_headers;
  std::vector<std::string> public_decls;
  std::unordered_map<std::string, bool> function_visibility;
  std::unordered_map<std::string, bool> struct_visibility;
  std::unordered_map<std::string, bool> enum_visibility;
  std::vector<AstClosure> closures;
  std::vector<AstInterpolatedString> interpolated_strings;
  std::unordered_map<std::string, int> enum_variant_tags;
  std::unordered_map<std::string, std::string> enum_variant_payload_types;
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
