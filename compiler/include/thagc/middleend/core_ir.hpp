#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "thagc/frontend/ast.hpp"

namespace thagc::lowering {

enum class CoreStmtKind {
  Let,
  Assign,
  Defer,
  Break,
  Continue,
  Expr,
  Return,
  If,
  Else,
  While,
  For,
  Match,
};

struct CoreStmt {
  CoreStmtKind kind = CoreStmtKind::Expr;
  int indent = 0;
  std::string text;
  std::string target;
  bool has_await = false;
  bool has_expression = false;
  std::string expression;
  syntax::AstExprPtr expression_ast;
  std::optional<syntax::Span> span;
};

struct CoreFunction {
  std::string name;
  bool is_method = false;
  bool is_pub = false;
  bool is_async = false;
  std::string owner_type;
  std::string method_name;
  std::vector<std::string> params;
  std::vector<std::string> param_types;
  std::string return_type;
  int return_literal = 0;
  std::string return_expression;
  syntax::AstExprPtr return_expression_ast;
  std::vector<CoreStmt> statements;
};

struct CoreClosure {
  std::vector<std::string> captures;
  std::vector<std::string> params;
  std::string body;
  bool block_body = false;
  int line = 0;
};

struct CoreStructType {
  std::string name;
  std::vector<std::string> fields;
  std::vector<std::string> field_types;
};

struct CoreEnumVariant {
  std::string enum_name;
  std::string name;
  int tag = 0;
  std::string payload_type;
};

struct CoreExternFunction {
  std::string name;
  std::vector<std::string> param_types;
  std::string return_type;
};

struct CoreProgram {
  std::string normalized_source;
  bool has_main = false;
  int main_return_literal = 0;
  std::string main_return_expression;
  syntax::AstExprPtr main_return_expression_ast;
  std::vector<CoreStmt> main_statements;
  std::vector<CoreFunction> functions;
  std::vector<CoreClosure> closures;
  std::vector<CoreExternFunction> extern_functions;
  std::vector<CoreStructType> struct_types;
  std::vector<CoreEnumVariant> enum_variants;
  std::unordered_map<std::string, int> enum_variant_tags;
  std::unordered_map<std::string, std::string> enum_variant_payload_types;
  std::unordered_map<std::string, std::vector<std::string>> struct_fields;
  std::unordered_map<std::string, std::string> struct_field_types;
  std::unordered_map<std::string, std::vector<std::string>> struct_methods;
};

CoreProgram lower_to_core(const syntax::AstProgram& program);

}  // namespace thagc::lowering
