#include "internal.hpp"

#include <span>
#include <string_view>
#include <unordered_map>

namespace thagc::syntax {

namespace {

static const Token& eof_token() {
  static const Token kEof{
      TokenKind::EndOfFile,
      "",
      0,
      0,
      Span{},
  };
  return kEof;
}

struct ParserContext {
  std::span<const Token> tokens;
  std::size_t pos = 0;

  const Token& peek(int offset = 0) const {
    if (offset < 0) {
      return eof_token();
    }
    const std::size_t idx = pos + static_cast<std::size_t>(offset);
    if (idx >= tokens.size()) {
      return eof_token();
    }
    return tokens[idx];
  }

  Token advance() {
    const Token tok = peek();
    if (pos < tokens.size()) {
      ++pos;
    }
    return tok;
  }

  bool eat(TokenKind kind) {
    if (peek().kind != kind) {
      return false;
    }
    advance();
    return true;
  }
};

struct LineTokenInfo {
  TokenKind first = TokenKind::Unknown;
  TokenKind second = TokenKind::Unknown;
};

struct LineTokenBounds {
  std::size_t begin = 0;
  std::size_t end = 0;
  bool has_tokens = false;
};

static bool has_valid_span(const Span& span) {
  return span.hi > span.lo;
}

static std::unordered_map<int, Span> collect_line_token_spans(std::span<const Token> tokens) {
  std::unordered_map<int, Span> line_spans;
  for (const Token& tok : tokens) {
    if (tok.kind == TokenKind::EndOfFile || tok.line <= 0 || !has_valid_span(tok.span)) {
      continue;
    }
    auto it = line_spans.find(tok.line);
    if (it == line_spans.end()) {
      line_spans[tok.line] = tok.span;
      continue;
    }
    Span merged = it->second;
    if (tok.span.lo < merged.lo) {
      merged.lo = tok.span.lo;
    }
    if (tok.span.hi > merged.hi) {
      merged.hi = tok.span.hi;
    }
    it->second = merged;
  }
  return line_spans;
}

static std::unordered_map<int, LineTokenInfo> collect_line_token_info(ParserContext& ctx) {
  std::unordered_map<int, LineTokenInfo> line_info;
  while (true) {
    const Token tok = ctx.advance();
    if (tok.kind == TokenKind::EndOfFile) {
      break;
    }
    if (tok.kind == TokenKind::EndOfFile || tok.kind == TokenKind::Newline || tok.line <= 0) {
      continue;
    }
    auto it = line_info.find(tok.line);
    if (it == line_info.end()) {
      LineTokenInfo info;
      info.first = tok.kind;
      line_info[tok.line] = std::move(info);
      continue;
    }
    if (it->second.second == TokenKind::Unknown) {
      it->second.second = tok.kind;
    }
  }
  return line_info;
}

static std::unordered_map<int, LineTokenBounds> collect_line_token_bounds(std::span<const Token> tokens) {
  std::unordered_map<int, LineTokenBounds> bounds;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    const Token& tok = tokens[i];
    if (tok.line <= 0 || tok.kind == TokenKind::EndOfFile || tok.kind == TokenKind::Newline) {
      continue;
    }
    auto it = bounds.find(tok.line);
    if (it == bounds.end()) {
      LineTokenBounds line_bounds;
      line_bounds.begin = i;
      line_bounds.end = i + 1;
      line_bounds.has_tokens = true;
      bounds[tok.line] = line_bounds;
      continue;
    }
    it->second.end = i + 1;
  }
  return bounds;
}

static std::span<const Token> line_tokens_for(std::span<const Token> tokens, const SourceLine& line) {
  if (line.token_end <= line.token_begin || line.token_end > tokens.size()) {
    return std::span<const Token>{};
  }
  return tokens.subspan(line.token_begin, line.token_end - line.token_begin);
}

}  // namespace

AstProgram Parser::parse(const std::vector<Token>& tokens, const std::string& source) const {
  ParserContext ctx{std::span<const Token>(tokens.data(), tokens.size()), 0};
  auto token_line_spans = collect_line_token_spans(ctx.tokens);
  auto token_line_info = collect_line_token_info(ctx);
  auto token_line_bounds = collect_line_token_bounds(ctx.tokens);
  ctx.pos = 0;
  AstProgram program;
  program.source = source;
  program.line_spans = token_line_spans;

  std::vector<SourceLine> lines;
  std::istringstream in(source);
  std::string raw_line;
  int line_no = 0;
  std::size_t line_start_offset = 0;
  while (std::getline(in, raw_line)) {
    ++line_no;
    const std::size_t line_end_offset = line_start_offset + raw_line.size();
    const std::string stripped = strip_comments(raw_line);
    const std::string clean = trim(stripped);
    Span line_span{
        static_cast<std::uint32_t>(line_start_offset),
        static_cast<std::uint32_t>(line_end_offset),
        0,
    };
    auto span_it = token_line_spans.find(line_no);
    if (span_it != token_line_spans.end()) {
      line_span = span_it->second;
    }
    program.line_spans[line_no] = line_span;
    if (clean.empty()) {
      line_start_offset = line_end_offset;
      if (line_start_offset < source.size() && source[line_start_offset] == '\n') {
        ++line_start_offset;
      }
      continue;
    }
    program.top_level_lines.push_back(clean);
    SourceLine source_line;
    source_line.number = line_no;
    source_line.indent = leading_indent(raw_line);
    source_line.clean = clean;
    source_line.span = line_span;
    auto bounds_it = token_line_bounds.find(line_no);
    if (bounds_it != token_line_bounds.end() && bounds_it->second.has_tokens) {
      source_line.token_begin = bounds_it->second.begin;
      source_line.token_end = bounds_it->second.end;
    }
    lines.push_back(std::move(source_line));
    line_start_offset = line_end_offset;
    if (line_start_offset < source.size() && source[line_start_offset] == '\n') {
      ++line_start_offset;
    }
  }

  std::unordered_map<std::string, AstMacro> macros;
  std::unordered_map<std::string, std::string> comptime_known_values;
  std::size_t i = 0;
  while (i < lines.size()) {
    const SourceLine& line = lines[i];
    LineTokenInfo line_tok;
    auto line_tok_it = token_line_info.find(line.number);
    if (line_tok_it != token_line_info.end()) {
      line_tok = line_tok_it->second;
    }
    const bool line_is_pub = line_tok.first == TokenKind::KeywordPub;
    const bool line_is_intent = line_tok.first == TokenKind::KeywordIntent;
    const bool line_is_flow = line_tok.first == TokenKind::KeywordFlow;
    const bool line_is_macro = line_tok.first == TokenKind::KeywordMacro;
    const bool line_is_comptime = line_tok.first == TokenKind::KeywordComptime;
    const bool line_is_func = line_tok.first == TokenKind::KeywordFunc;
    const bool line_is_async_func = line_tok.first == TokenKind::KeywordAsync && line_tok.second == TokenKind::KeywordFunc;
    const bool line_is_intent_func =
        line_tok.first == TokenKind::KeywordIntent && line_tok.second == TokenKind::KeywordFunc;
    const bool line_is_flow_func = line_tok.first == TokenKind::KeywordFlow && line_tok.second == TokenKind::KeywordFunc;
    const bool line_is_import = line_tok.first == TokenKind::KeywordImport || line_tok.first == TokenKind::KeywordFrom;
    const bool line_is_extern = line_tok.first == TokenKind::KeywordExtern;
    const bool line_is_struct = line_tok.first == TokenKind::KeywordStruct;
    const bool line_is_enum = line_tok.first == TokenKind::KeywordEnum;
    const bool line_is_type = line_tok.first == TokenKind::KeywordType;
    const bool line_is_state = line_tok.first == TokenKind::KeywordState;
    const bool line_is_trait = line_tok.first == TokenKind::KeywordTrait;
    const bool line_is_impl = line_tok.first == TokenKind::KeywordImpl;

    collect_feature_counters(line.clean, program);
    std::string effective_line = line.clean;
    bool is_pub_decl = false;
    if (line_is_pub && starts_with(effective_line, "pub ")) {
      is_pub_decl = true;
      program.public_decls.push_back(line.clean);
      effective_line = trim(effective_line.substr(4));
    }
    if (line_is_intent || starts_with(effective_line, "intent ")) {
      program.intents.push_back(effective_line);
    }
    if (line_is_flow || starts_with(effective_line, "flow ")) {
      program.flows.push_back(effective_line);
    }
    if (line_is_intent_func || starts_with(effective_line, "intent func ")) {
      effective_line = trim(effective_line.substr(7));
    } else if (line_is_flow_func || starts_with(effective_line, "flow func ")) {
      effective_line = trim(effective_line.substr(5));
    }

    if (line_is_macro || starts_with(effective_line, "macro ")) {
      AstMacro macro;
      std::string macro_error;
      if (!parse_macro_declaration(effective_line, macro, macro_error)) {
        add_parse_error(program, line.number, macro_error);
      } else {
        macro.line = line.number;
        if (macros.find(macro.name) != macros.end()) {
          add_parse_error(program, line.number, "duplicate macro declaration '" + macro.name + "'");
        } else {
          macros[macro.name] = macro;
          program.macros.push_back(std::move(macro));
        }
      }
      ++i;
      continue;
    }

    if ((line_is_comptime && ends_with(effective_line, ":")) || starts_with(effective_line, "comptime:")) {
      ++i;
      if (i >= lines.size() || lines[i].indent <= line.indent) {
        add_parse_error(program, line.number, "comptime block must be indentation-scoped");
      }
      while (i < lines.size() && lines[i].indent > line.indent) {
        collect_feature_counters(lines[i].clean, program);
        AstStatement st = build_statement_from_line(program, lines[i], macros, line_tokens_for(ctx.tokens, lines[i]));
        if ((st.kind != StatementKind::Let && st.kind != StatementKind::Assign) || !st.has_expression ||
            !st.expression_valid) {
          add_parse_error(program, lines[i].number,
                          "comptime block supports only valid let/assign expressions");
          ++i;
          continue;
        }
        if (st.kind == StatementKind::Let) {
          if (st.target.empty() || !is_identifier(st.target)) {
            add_parse_error(program, lines[i].number, "comptime let binding must use identifier name");
            ++i;
            continue;
          }
          const std::string expression = substitute_known_identifiers(st.expression_normalized, comptime_known_values);
          comptime_known_values[st.target] = expression;
          program.comptime_bindings.push_back(AstComptimeBinding{st.target, expression, lines[i].number});
        } else {
          if (st.target.empty() || !is_identifier(st.target)) {
            add_parse_error(program, lines[i].number, "comptime assignment target must be identifier");
            ++i;
            continue;
          }
          auto known = comptime_known_values.find(st.target);
          if (known == comptime_known_values.end()) {
            add_parse_error(program, lines[i].number,
                            "comptime assignment target '" + st.target + "' is not defined");
            ++i;
            continue;
          }
          const std::string expression = substitute_known_identifiers(st.expression_normalized, comptime_known_values);
          comptime_known_values[st.target] = expression;
          program.comptime_bindings.push_back(AstComptimeBinding{st.target, expression, lines[i].number});
        }
        ++i;
      }
      continue;
    }

    if (line_is_func || line_is_async_func || starts_with(effective_line, "func ") || starts_with(effective_line, "async func ")) {
      const bool is_async_func = line_is_async_func || starts_with(effective_line, "async func ");
      const std::string function_header = is_async_func ? trim(effective_line.substr(6)) : effective_line;
      AstFunction fn;
      fn.name = function_name_from_header(function_header);
      fn.params = function_params_from_header(function_header);
      fn.param_types = function_param_types_from_header(function_header);
      fn.header_line = line.number;
      fn.header_indent = line.indent;
      fn.is_pub = is_pub_decl;
      fn.is_async = is_async_func;
      fn.span = line.span;

      if (!ends_with(function_header, ":")) {
        add_parse_error(program, line.number, "function header must be colon-terminated");
      }
      if (fn.name.empty()) {
        add_parse_error(program, line.number, "invalid function header");
      }
      for (const std::string& param : fn.params) {
        if (param.empty() || !is_simple_assignable_target(param) || param.find('.') != std::string::npos) {
          add_parse_error(program, line.number, "invalid function parameter '" + param + "'");
        }
      }
      fn.return_type = function_return_type_from_header(function_header);
      if (function_header.find("->") != std::string::npos && fn.return_type.empty()) {
        add_parse_error(program, line.number, "function return annotation '-> type' is not supported");
      }

      if (fn.name == "main") {
        program.has_main = true;
      }
      if (!fn.name.empty() && fn.name.find('.') == std::string::npos) {
        program.function_visibility[fn.name] = fn.is_pub;
      }

      ++i;
      if (i >= lines.size() || lines[i].indent <= fn.header_indent) {
        add_parse_error(program, line.number, "function body must be indentation-scoped");
      }

      while (i < lines.size() && lines[i].indent > fn.header_indent) {
        const SourceLine& body = lines[i];
        AstStatement st = build_statement_from_line(program, body, macros, line_tokens_for(ctx.tokens, body));
        st.span = body.span;
        if (st.kind == StatementKind::Return && fn.name == "main") {
          program.main_return_literal = parse_return_literal(body.clean);
        }
        fn.body.push_back(st);
        ++i;
      }

      program.functions.push_back(std::move(fn));
      continue;
    }

    if ((line_is_flow || starts_with(effective_line, "flow ")) &&
        !(line_is_flow_func || starts_with(effective_line, "flow func "))) {
      AstFlow flow;
      flow.header = effective_line;
      flow.name = flow_name_from_header(effective_line);
      flow.line = line.number;
      flow.indent = line.indent;
      if (!ends_with(effective_line, ":")) {
        add_parse_error(program, line.number, "flow header must be colon-terminated");
      }
      if (flow.name.empty()) {
        add_parse_error(program, line.number, "invalid flow header");
      }

      ++i;
      while (i < lines.size() && lines[i].indent > line.indent) {
        collect_feature_counters(lines[i].clean, program);
        const SourceLine& step_line = lines[i];
        auto step_tok_it = token_line_info.find(step_line.number);
        const bool is_step = step_tok_it != token_line_info.end() &&
                             step_tok_it->second.first == TokenKind::KeywordStep;
        if (!is_step && !starts_with(step_line.clean, "step ")) {
          add_parse_error(program, step_line.number, "flow block only accepts 'step' entries");
          ++i;
          continue;
        }
        AstFlowStep step;
        step.line = step_line.number;
        step.span = step_line.span;
        std::string step_error;
        if (!parse_flow_step_header(step_line.clean, step, step_error)) {
          add_parse_error(program, step_line.number, step_error);
          ++i;
          while (i < lines.size() && lines[i].indent > step_line.indent) {
            ++i;
          }
          continue;
        }
        ++i;
        while (i < lines.size() && lines[i].indent > step_line.indent) {
          collect_feature_counters(lines[i].clean, program);
          parse_flow_step_directive(program, step, lines[i]);
          ++i;
        }
        flow.steps.push_back(std::move(step));
      }
      if (flow.steps.empty()) {
        add_parse_error(program, line.number, "flow block must contain at least one step");
      }
      program.flow_defs.push_back(std::move(flow));
      continue;
    }

    if (line_is_import || starts_with(effective_line, "import ") || starts_with(effective_line, "from ")) {
      AstImport import_decl;
      std::string import_error;
      if (!parse_import_decl(effective_line, import_decl, import_error)) {
        add_parse_error(program, line.number, import_error);
      } else {
        import_decl.line = line.number;
        import_decl.column = 1;
        import_decl.span = line.span;
        program.imports.push_back(std::move(import_decl));
      }
      ++i;
      continue;
    }
    if (line_is_extern || starts_with(effective_line, "extern ")) {
      program.extern_decls.push_back(effective_line);
      AstExternFunction ext;
      if (!parse_extern_function_declaration(effective_line, ext)) {
        add_parse_error(program, line.number, "malformed extern declaration");
      } else {
        ext.line = line.number;
        program.extern_functions.push_back(std::move(ext));
      }
      ++i;
      continue;
    }
    if (line_is_struct || starts_with(effective_line, "struct ")) {
      if (!ends_with(effective_line, ":")) {
        add_parse_error(program, line.number, "struct header must be colon-terminated");
      }
      program.structs.push_back(effective_line);
      const std::string struct_name = struct_name_from_header(effective_line);
      if (struct_name.empty()) {
        add_parse_error(program, line.number, "invalid struct header");
      } else {
        program.struct_visibility[struct_name] = is_pub_decl;
      }
      ++i;
      while (i < lines.size() && lines[i].indent > line.indent) {
        collect_feature_counters(lines[i].clean, program);
        if (!struct_name.empty()) {
          const std::string field_name = struct_field_name_from_line(lines[i].clean);
          if (field_name.empty()) {
            add_parse_error(program, lines[i].number,
                            "invalid struct field declaration: '" + lines[i].clean + "'");
          } else {
            program.struct_fields[struct_name].push_back(field_name);
            program.struct_field_types[struct_name + "." + field_name] =
                struct_field_type_from_line(lines[i].clean);
          }
        }
        ++i;
      }
      continue;
    }
    if (line_is_enum || starts_with(effective_line, "enum ")) {
      if (!ends_with(effective_line, ":")) {
        add_parse_error(program, line.number, "enum header must be colon-terminated");
      }
      program.enums.push_back(effective_line);
      const std::string enum_name = enum_name_from_header(effective_line);
      if (enum_name.empty()) {
        add_parse_error(program, line.number, "invalid enum header");
      } else {
        program.enum_visibility[enum_name] = is_pub_decl;
      }
      int variant_index = 0;
      ++i;
      while (i < lines.size() && lines[i].indent > line.indent) {
        collect_feature_counters(lines[i].clean, program);
        const std::string variant = enum_variant_name_from_line(lines[i].clean);
        if (!variant.empty() && program.enum_variant_tags.find(variant) == program.enum_variant_tags.end()) {
          program.enum_variant_tags[variant] = variant_index++;
          const std::string payload_type = enum_variant_payload_type_from_line(lines[i].clean);
          if (!payload_type.empty()) {
            program.enum_variant_payload_types[variant] = payload_type;
          }
        }
        if (!enum_name.empty() && !variant.empty()) {
          program.enum_variants[enum_name].push_back(variant);
        }
        ++i;
      }
      continue;
    }
    if (line_is_type || starts_with(effective_line, "type ")) {
      program.type_aliases.push_back(effective_line);
      ++i;
      continue;
    }
    if (line_is_state || starts_with(effective_line, "state ")) {
      std::string state_name;
      std::vector<std::string> variants;
      std::string state_error;
      const bool parsed = parse_state_header(effective_line, state_name, variants, state_error);
      if (!parsed) {
        add_parse_error(program, line.number, "invalid state declaration");
      } else if (!state_error.empty()) {
        add_parse_error(program, line.number, state_error);
      } else {
        program.state_sets[state_name] = variants;
      }
      ++i;
      continue;
    }
    if (line_is_trait || starts_with(effective_line, "trait ")) {
      if (!ends_with(effective_line, ":")) {
        add_parse_error(program, line.number, "trait header must be colon-terminated");
      }
      program.traits.push_back(effective_line);
      const std::string trait_name = trim(effective_line.substr(6, effective_line.size() - 7));
      ++i;
      while (i < lines.size() && lines[i].indent > line.indent) {
        collect_feature_counters(lines[i].clean, program);
        const std::string method = method_name_from_line(lines[i].clean);
        if (!method.empty()) {
          program.trait_required_methods[trait_name].push_back(method);
        }
        ++i;
      }
      continue;
    }
    if (line_is_impl || starts_with(effective_line, "impl ")) {
      if (!ends_with(effective_line, ":")) {
        add_parse_error(program, line.number, "impl header must be colon-terminated");
      }
      program.impls.push_back(effective_line);
      std::string trait_name;
      std::string type_name;
      const bool is_impl_for = parse_impl_for_header(effective_line, trait_name, type_name);
      std::string impl_type_name;
      const bool is_type_impl = parse_impl_type_header(effective_line, impl_type_name);
      const std::string impl_key = trait_name + "|" + type_name;
      if (is_impl_for) {
        program.impl_for_headers.push_back(effective_line);
      }
      ++i;
      while (i < lines.size() && lines[i].indent > line.indent) {
        const SourceLine& member_line = lines[i];
        collect_feature_counters(member_line.clean, program);
        std::string effective_member = member_line.clean;
        if (starts_with(effective_member, "pub ")) {
          effective_member = trim(effective_member.substr(4));
        }
        if (is_impl_for) {
          const std::string method = method_name_from_line(member_line.clean);
          if (!method.empty()) {
            program.impl_for_methods[impl_key].push_back(method);
          }
        }
        if (is_type_impl && (starts_with(effective_member, "func ") || starts_with(effective_member, "async func "))) {
          const bool async_method = starts_with(effective_member, "async func ");
          const std::string method_header = async_method ? trim(effective_member.substr(6)) : effective_member;
          AstFunction fn;
          const std::string method_name = function_name_from_header(method_header);
          if (method_name.empty()) {
            add_parse_error(program, member_line.number, "invalid impl method header");
            ++i;
            while (i < lines.size() && lines[i].indent > member_line.indent) {
              ++i;
            }
            continue;
          }
          fn.name = impl_type_name + "." + method_name;
          fn.params = function_params_from_header(method_header);
          fn.param_types = function_param_types_from_header(method_header);
          if (fn.params.empty() || fn.params.front() != "self") {
            fn.params.insert(fn.params.begin(), "self");
            fn.param_types.insert(fn.param_types.begin(), "");
          }
          fn.header_line = member_line.number;
          fn.header_indent = member_line.indent;
          fn.return_type = function_return_type_from_header(method_header);
          fn.is_async = async_method;
          if (!ends_with(method_header, ":")) {
            add_parse_error(program, member_line.number, "impl method header must be colon-terminated");
          }
          if (method_header.find("->") != std::string::npos && fn.return_type.empty()) {
            add_parse_error(program, member_line.number, "impl method return annotation '-> type' is not supported");
          }
          if (!method_name.empty()) {
            auto& methods = program.struct_methods[impl_type_name];
            if (std::find(methods.begin(), methods.end(), method_name) == methods.end()) {
              methods.push_back(method_name);
            }
          }

          ++i;
          if (i >= lines.size() || lines[i].indent <= fn.header_indent) {
            add_parse_error(program, member_line.number, "impl method body must be indentation-scoped");
          }
          while (i < lines.size() && lines[i].indent > fn.header_indent) {
            AstStatement st = build_statement_from_line(program, lines[i], macros, line_tokens_for(ctx.tokens, lines[i]));
            st.span = lines[i].span;
            fn.body.push_back(std::move(st));
            ++i;
          }
          program.functions.push_back(std::move(fn));
          continue;
        }
        ++i;
      }
      continue;
    }

    if (line.indent != 0) {
      add_parse_error(program, line.number, "top-level executable statements must not be indented");
      ++i;
      continue;
    }

    AstStatement top = build_statement_from_line(program, line, macros, line_tokens_for(ctx.tokens, line));
    top.span = line.span;
    if (top.kind == StatementKind::Return) {
      add_parse_error(program, line.number, "top-level return is not allowed");
    }
    program.top_level_statements.push_back(top);

    ++i;
  }

  return program;
}

}  // namespace thagc::syntax
