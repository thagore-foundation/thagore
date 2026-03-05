#include "thagc/middleend/core_ir.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>

namespace thagc::lowering {

static bool parse_i32_literal(const std::string& text, int& value) {
  if (text.empty()) return false;
  std::size_t i = 0;
  bool neg = false;
  if (text[i] == '-') {
    neg = true;
    ++i;
  }
  if (i >= text.size()) return false;
  for (std::size_t k = i; k < text.size(); ++k) {
    if (!std::isdigit(static_cast<unsigned char>(text[k]))) {
      return false;
    }
  }
  value = std::stoi(text.substr(i));
  if (neg) value = -value;
  return true;
}

static bool is_ident_start(char ch) {
  return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}

static bool is_ident_body(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
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

static std::string parse_struct_name_from_header(const std::string& header) {
  constexpr std::string_view kStruct = "struct ";
  if (header.size() < kStruct.size() || header.compare(0, kStruct.size(), kStruct) != 0) {
    return "";
  }
  std::string out = header.substr(kStruct.size());
  if (!out.empty() && out.back() == ':') {
    out.pop_back();
  }
  out = trim_copy(out);
  if (out.empty() || !is_ident_start(out[0])) {
    return "";
  }
  for (std::size_t i = 1; i < out.size(); ++i) {
    if (!is_ident_body(out[i])) {
      return "";
    }
  }
  return out;
}

static std::string parse_enum_name_from_header(const std::string& header) {
  constexpr std::string_view kEnum = "enum ";
  if (header.size() < kEnum.size() || header.compare(0, kEnum.size(), kEnum) != 0) {
    return "";
  }
  std::string out = header.substr(kEnum.size());
  if (!out.empty() && out.back() == ':') {
    out.pop_back();
  }
  out = trim_copy(out);
  if (out.empty() || !is_ident_start(out[0])) {
    return "";
  }
  for (std::size_t i = 1; i < out.size(); ++i) {
    if (!is_ident_body(out[i])) {
      return "";
    }
  }
  return out;
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

static std::string parse_let_name(const std::string& line) {
  constexpr std::string_view kLet = "let ";
  if (line.size() < kLet.size() || line.compare(0, kLet.size(), kLet) != 0) {
    return "";
  }
  std::size_t i = kLet.size();
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

static std::string substitute_identifiers(
    const std::string& expr, const std::unordered_map<std::string, std::string>& known_values) {
  std::string out;
  bool in_string = false;
  bool escape = false;
  for (std::size_t i = 0; i < expr.size();) {
    const char ch = expr[i];
    if (in_string) {
      out.push_back(ch);
      if (escape) {
        escape = false;
      } else if (ch == '\\') {
        escape = true;
      } else if (ch == '"') {
        in_string = false;
      }
      ++i;
      continue;
    }
    if (ch == '"') {
      in_string = true;
      out.push_back(ch);
      ++i;
      continue;
    }
    if (!is_ident_start(ch)) {
      out.push_back(ch);
      ++i;
      continue;
    }
    const std::size_t start = i;
    while (i < expr.size() && is_ident_body(expr[i])) {
      ++i;
    }
    const bool prev_is_dot = start > 0 && expr[start - 1] == '.';
    const std::string ident = expr.substr(start, i - start);
    const auto it = known_values.find(ident);
    if (!prev_is_dot && it != known_values.end()) {
      out += "(" + it->second + ")";
    } else {
      out += ident;
    }
  }
  return out;
}

static bool should_track_known_value(const std::string& expr) {
  if (expr.empty()) {
    return false;
  }
  if (expr.find('(') != std::string::npos || expr.find(')') != std::string::npos ||
      expr.find('.') != std::string::npos || expr.find('|') != std::string::npos ||
      expr.find('{') != std::string::npos || expr.find('}') != std::string::npos ||
      expr.find('[') != std::string::npos || expr.find(']') != std::string::npos ||
      expr.find('?') != std::string::npos) {
    return false;
  }
  return true;
}

static bool is_print_like_expression(const std::string& expr) {
  const std::string normalized = trim_copy(expr);
  return (normalized.rfind("print(", 0) == 0 && !normalized.empty() && normalized.back() == ')') ||
         (normalized.rfind("print_raw(", 0) == 0 && !normalized.empty() && normalized.back() == ')');
}

static CoreStmtKind map_stmt_kind(syntax::StatementKind kind) {
  switch (kind) {
    case syntax::StatementKind::Let:
      return CoreStmtKind::Let;
    case syntax::StatementKind::Assign:
      return CoreStmtKind::Assign;
    case syntax::StatementKind::Defer:
      return CoreStmtKind::Defer;
    case syntax::StatementKind::Break:
      return CoreStmtKind::Break;
    case syntax::StatementKind::Continue:
      return CoreStmtKind::Continue;
    case syntax::StatementKind::Return:
      return CoreStmtKind::Return;
    case syntax::StatementKind::If:
      return CoreStmtKind::If;
    case syntax::StatementKind::Else:
      return CoreStmtKind::Else;
    case syntax::StatementKind::While:
      return CoreStmtKind::While;
    case syntax::StatementKind::For:
      return CoreStmtKind::For;
    case syntax::StatementKind::Match:
      return CoreStmtKind::Match;
    case syntax::StatementKind::Expr:
    default:
      return CoreStmtKind::Expr;
  }
}

static void append_core_statement(std::vector<CoreStmt>& out_statements, const syntax::AstStatement& st,
                                  const std::unordered_map<std::string, std::string>& known_values) {
  CoreStmt out;
  out.kind = map_stmt_kind(st.kind);
  out.indent = st.indent;
  out.text = st.text;
  out.target = st.target;
  out.has_await = st.has_await;
  out.has_expression = st.has_expression && st.expression_valid;
  out.expression_ast = st.expression_ast;
  out.span = st.span;
  if (out.has_expression) {
    out.expression = substitute_identifiers(st.expression_normalized, known_values);
  }
  out_statements.push_back(std::move(out));
}

static CoreStmt make_expr_stmt(int indent, const std::string& text, bool has_await = false) {
  CoreStmt out;
  out.kind = CoreStmtKind::Expr;
  out.indent = indent;
  out.text = text;
  out.has_await = has_await;
  out.has_expression = true;
  out.expression = text;
  return out;
}

static CoreStmt make_let_stmt(int indent, const std::string& name, const std::string& expression) {
  CoreStmt out;
  out.kind = CoreStmtKind::Let;
  out.indent = indent;
  out.text = "let " + name + " = " + expression;
  out.has_expression = true;
  out.expression = expression;
  return out;
}

static CoreStmt make_assign_stmt(int indent, const std::string& target, const std::string& expression) {
  CoreStmt out;
  out.kind = CoreStmtKind::Assign;
  out.indent = indent;
  out.text = target + " = " + expression;
  out.target = target;
  out.has_expression = true;
  out.expression = expression;
  return out;
}

static CoreStmt make_if_stmt(int indent, const std::string& condition) {
  CoreStmt out;
  out.kind = CoreStmtKind::If;
  out.indent = indent;
  out.text = "if (" + condition + "):";
  out.has_expression = true;
  out.expression = condition;
  return out;
}

static CoreStmt make_while_stmt(int indent, const std::string& condition) {
  CoreStmt out;
  out.kind = CoreStmtKind::While;
  out.indent = indent;
  out.text = "while (" + condition + "):";
  out.has_expression = true;
  out.expression = condition;
  return out;
}

static CoreStmt make_break_stmt(int indent) {
  CoreStmt out;
  out.kind = CoreStmtKind::Break;
  out.indent = indent;
  out.text = "break";
  return out;
}

static CoreStmt make_return_stmt(int indent, const std::string& expression) {
  CoreStmt out;
  out.kind = CoreStmtKind::Return;
  out.indent = indent;
  out.text = "return " + expression;
  out.has_expression = true;
  out.expression = expression;
  return out;
}

static bool has_core_extern(const std::vector<CoreExternFunction>& externs, const std::string& name) {
  for (const auto& ext : externs) {
    if (ext.name == name) {
      return true;
    }
  }
  return false;
}

static CoreFunction lower_flow_to_function(const syntax::AstFlow& flow) {
  CoreFunction out;
  out.name = flow.name;
  out.return_type = "i32";
  out.return_literal = 1;
  out.return_expression = "1";

  std::vector<std::string> rollback_actions;
  const int base_indent = 2;
  for (std::size_t i = 0; i < flow.steps.size(); ++i) {
    const auto& step = flow.steps[i];
    const std::string idx = std::to_string(i);
    const std::string ok_name = "__flow_step_" + idx + "_ok";
    const std::string attempt_name = "__flow_step_" + idx + "_attempt";
    const std::string deadline_name = "__flow_step_" + idx + "_deadline";

    out.statements.push_back(make_let_stmt(base_indent, ok_name, "0"));
    out.statements.push_back(make_let_stmt(base_indent, attempt_name, "0"));
    if (step.has_timeout) {
      out.statements.push_back(
          make_let_stmt(base_indent, deadline_name, "thag_now_ms() + " + std::to_string(step.timeout_ms)));
    }

    const int retries = step.has_retry ? step.retry_count : 0;
    out.statements.push_back(make_while_stmt(base_indent, attempt_name + " <= " + std::to_string(retries)));
    if (step.has_timeout) {
      out.statements.push_back(make_if_stmt(base_indent + 2, "thag_now_ms() > " + deadline_name));
      out.statements.push_back(make_break_stmt(base_indent + 4));
    }
    out.statements.push_back(make_assign_stmt(base_indent + 2, ok_name, step.action));
    out.statements.push_back(make_if_stmt(base_indent + 2, ok_name + " != 0"));
    out.statements.push_back(make_break_stmt(base_indent + 4));
    out.statements.push_back(make_assign_stmt(base_indent + 2, attempt_name, attempt_name + " + 1"));

    out.statements.push_back(make_if_stmt(base_indent, ok_name + " == 0"));
    if (!step.undo_action.empty() && !step.irreversible) {
      out.statements.push_back(make_expr_stmt(base_indent + 2, step.undo_action));
    }
    for (auto it = rollback_actions.rbegin(); it != rollback_actions.rend(); ++it) {
      out.statements.push_back(make_expr_stmt(base_indent + 2, *it));
    }
    out.statements.push_back(make_return_stmt(base_indent + 2, "0"));

    if (!step.undo_action.empty() && !step.irreversible) {
      rollback_actions.push_back(step.undo_action);
    }
  }

  out.statements.push_back(make_return_stmt(base_indent, "1"));
  return out;
}

CoreProgram lower_to_core(const syntax::AstProgram& program) {
  CoreProgram core;
  core.normalized_source = program.source;
  core.enum_variant_tags = program.enum_variant_tags;
  core.enum_variant_payload_types = program.enum_variant_payload_types;
  core.struct_fields = program.struct_fields;
  core.struct_field_types = program.struct_field_types;
  core.struct_methods = program.struct_methods;

  for (const std::string& header : program.structs) {
    const std::string struct_name = parse_struct_name_from_header(header);
    if (struct_name.empty()) {
      continue;
    }
    CoreStructType typed;
    typed.name = struct_name;
    auto fields_it = program.struct_fields.find(struct_name);
    if (fields_it != program.struct_fields.end()) {
      typed.fields = fields_it->second;
      typed.field_types.reserve(fields_it->second.size());
      for (const std::string& field : fields_it->second) {
        const std::string key = struct_name + "." + field;
        auto type_it = program.struct_field_types.find(key);
        typed.field_types.push_back(type_it == program.struct_field_types.end() ? "i32" : type_it->second);
      }
    }
    core.struct_types.push_back(std::move(typed));
  }

  std::vector<std::pair<std::string, int>> variants;
  variants.reserve(program.enum_variant_tags.size());
  for (const auto& entry : program.enum_variant_tags) {
    variants.push_back(entry);
  }
  std::unordered_map<std::string, std::string> variant_owner;
  for (const auto& [enum_name, enum_members] : program.enum_variants) {
    for (const std::string& variant_name : enum_members) {
      if (!variant_name.empty()) {
        variant_owner[variant_name] = enum_name;
      }
    }
  }
  std::sort(variants.begin(), variants.end(), [](const auto& lhs, const auto& rhs) { return lhs.second < rhs.second; });
  for (const auto& [name, tag] : variants) {
    CoreEnumVariant typed;
    auto owner_it = variant_owner.find(name);
    if (owner_it != variant_owner.end()) {
      typed.enum_name = owner_it->second;
    }
    typed.name = name;
    typed.tag = tag;
    auto payload_it = program.enum_variant_payload_types.find(name);
    if (payload_it != program.enum_variant_payload_types.end()) {
      typed.payload_type = payload_it->second;
    }
    core.enum_variants.push_back(std::move(typed));
  }

  for (const auto& ext : program.extern_functions) {
    CoreExternFunction out;
    out.name = ext.name;
    out.param_types = ext.param_types;
    out.return_type = ext.return_type;
    core.extern_functions.push_back(std::move(out));
  }

  std::unordered_map<std::string, std::string> comptime_known_values;
  for (const auto& binding : program.comptime_bindings) {
    if (binding.name.empty()) {
      continue;
    }
    comptime_known_values[binding.name] = substitute_identifiers(binding.expression, comptime_known_values);
  }

  bool flow_uses_timeout = false;
  for (const auto& flow : program.flow_defs) {
    if (flow.name.empty()) {
      continue;
    }
    core.functions.push_back(lower_flow_to_function(flow));
    for (const auto& step : flow.steps) {
      if (step.has_timeout) {
        flow_uses_timeout = true;
      }
    }
  }
  if (flow_uses_timeout && !has_core_extern(core.extern_functions, "thag_now_ms")) {
    CoreExternFunction ext;
    ext.name = "thag_now_ms";
    ext.return_type = "i64";
    core.extern_functions.push_back(std::move(ext));
  }
  core.has_main = program.has_main || !program.top_level_statements.empty();
  core.main_return_literal = program.main_return_literal;

  auto lower_function = [&](const syntax::AstFunction& fn) {
    CoreFunction out;
    out.name = fn.name;
    out.is_pub = fn.is_pub;
    out.is_async = fn.is_async;
    out.params = fn.params;
    out.param_types = fn.param_types;
    out.return_type = fn.return_type;
    std::string owner;
    std::string method;
    if (split_owner_and_method(fn.name, owner, method) && program.struct_fields.find(owner) != program.struct_fields.end()) {
      out.is_method = true;
      out.owner_type = owner;
      out.method_name = method;
      if (!out.params.empty() && out.params.front() == "self") {
        out.params.erase(out.params.begin());
        if (!out.param_types.empty()) {
          out.param_types.erase(out.param_types.begin());
        }
      }
    }
    for (const auto& st : fn.body) {
      append_core_statement(out.statements, st, comptime_known_values);
      if (st.kind == syntax::StatementKind::Return && st.has_expression && st.expression_valid) {
        out.return_expression = substitute_identifiers(st.expression_normalized, comptime_known_values);
        out.return_expression_ast = st.expression_ast;
        int parsed = 0;
        if (parse_i32_literal(out.return_expression, parsed)) {
          out.return_literal = parsed;
        }
      }
    }
    if (out.return_expression.empty()) {
      out.return_expression = std::to_string(out.return_literal);
    }
    return out;
  };

  for (const auto& fn : program.functions) {
    core.functions.push_back(lower_function(fn));
  }

  for (const auto& closure : program.closures) {
    CoreClosure out;
    out.captures = closure.captures;
    out.params = closure.params;
    out.body = closure.body;
    out.block_body = closure.block_body;
    out.line = closure.line;
    core.closures.push_back(std::move(out));
  }

  if (!program.has_main && !program.top_level_statements.empty()) {
    std::unordered_map<std::string, std::string> known_values;
    const syntax::AstStatement& last = program.top_level_statements.back();
    CoreFunction script_main;
    script_main.name = "main";
    script_main.return_type = "i32";
    for (const auto& st : program.top_level_statements) {
      append_core_statement(script_main.statements, st, comptime_known_values);
      if ((st.kind == syntax::StatementKind::Let || st.kind == syntax::StatementKind::Assign) && st.has_expression &&
          st.expression_valid) {
        const std::string name = st.kind == syntax::StatementKind::Let ? parse_let_name(st.text) : st.target;
        const std::string expression = substitute_identifiers(st.expression_normalized, known_values);
        if (!name.empty() && name.find('.') == std::string::npos && should_track_known_value(expression)) {
          known_values[name] = expression;
        }
      }
    }
    int top_ret = 0;
    if (last.kind == syntax::StatementKind::Expr && last.has_expression && last.expression_valid) {
      const std::string comptime_expr = substitute_identifiers(last.expression_normalized, comptime_known_values);
      const std::string expr = substitute_identifiers(comptime_expr, known_values);
      if (is_print_like_expression(last.text)) {
        script_main.return_literal = 0;
        script_main.return_expression = "0";
        script_main.return_expression_ast = nullptr;
        CoreStmt ret_zero;
        ret_zero.kind = CoreStmtKind::Return;
        ret_zero.has_expression = true;
        ret_zero.expression = "0";
        ret_zero.text = "return 0";
        script_main.statements.push_back(std::move(ret_zero));
      } else {
        script_main.return_expression = substitute_identifiers(expr, known_values);
        script_main.return_expression_ast = last.expression_ast;
        if (parse_i32_literal(expr, top_ret)) {
          script_main.return_literal = top_ret;
        }
        if (!script_main.statements.empty()) {
          CoreStmt& implicit_ret = script_main.statements.back();
          implicit_ret.kind = CoreStmtKind::Return;
          implicit_ret.has_expression = true;
          implicit_ret.expression = expr;
          implicit_ret.expression_ast = last.expression_ast;
          implicit_ret.text = "return " + expr;
        }
      }
    } else {
      script_main.return_literal = 0;
      script_main.return_expression = "0";
      script_main.return_expression_ast = nullptr;
      CoreStmt ret_zero;
      ret_zero.kind = CoreStmtKind::Return;
      ret_zero.has_expression = true;
      ret_zero.expression = "0";
      ret_zero.text = "return 0";
      script_main.statements.push_back(std::move(ret_zero));
    }
    core.main_statements = script_main.statements;
    core.main_return_expression = script_main.return_expression;
    core.main_return_expression_ast = script_main.return_expression_ast;
    core.main_return_literal = script_main.return_literal;
    core.functions.push_back(std::move(script_main));
  }

  for (const auto& fn : core.functions) {
    if (fn.name == "main") {
      core.main_statements = fn.statements;
      core.main_return_expression = fn.return_expression;
      core.main_return_expression_ast = fn.return_expression_ast;
      core.main_return_literal = fn.return_literal;
      break;
    }
  }

  if (core.main_return_expression.empty()) {
    core.main_return_expression = std::to_string(core.main_return_literal);
  }
  return core;
}

}  // namespace thagc::lowering
