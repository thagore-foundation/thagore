#include "internal.hpp"

namespace thagc::codegen {

std::string parse_let_name(const std::string& line) {
  if (!starts_with(line, "let ")) {
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
  return line.substr(start, i - start);
}

std::string parse_let_annotation(const std::string& line) {
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
  out = trim(out);
  return out;
}

bool split_dotted_name(const std::string& text, std::string& base, std::string& member) {
  const std::size_t dot = text.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= text.size()) {
    return false;
  }
  if (text.find('.', dot + 1) != std::string::npos) {
    return false;
  }
  base = text.substr(0, dot);
  member = text.substr(dot + 1);
  return true;
}

ParsedConstructorCall parse_constructor_call(const std::string& expr) {
  ParsedConstructorCall out;
  const std::string clean = trim(expr);
  const std::size_t lparen = clean.find('(');
  const std::size_t rparen = clean.rfind(')');
  if (lparen == std::string::npos || rparen == std::string::npos || rparen <= lparen) {
    return out;
  }
  if (!trim(clean.substr(rparen + 1)).empty()) {
    return out;
  }
  const std::string name = trim(clean.substr(0, lparen));
  if (name.empty() || !is_ident_start(name[0])) {
    return out;
  }
  for (std::size_t i = 1; i < name.size(); ++i) {
    if (!is_ident_body(name[i])) {
      return out;
    }
  }
  out.name = name;
  const std::string body = clean.substr(lparen + 1, rparen - lparen - 1);
  std::size_t start = 0;
  int depth = 0;
  bool in_string = false;
  for (std::size_t i = 0; i <= body.size(); ++i) {
    if (i == body.size() || (body[i] == ',' && depth == 0 && !in_string)) {
      std::string part = trim(body.substr(start, i - start));
      if (!part.empty()) {
        out.args.push_back(part);
      }
      start = i + 1;
      continue;
    }
    const char ch = body[i];
    if (ch == '"' && (i == 0 || body[i - 1] != '\\')) {
      in_string = !in_string;
      continue;
    }
    if (in_string) {
      continue;
    }
    if (ch == '(') {
      ++depth;
    } else if (ch == ')') {
      if (depth == 0) {
        out.error = "unexpected ')' in constructor arguments";
        return out;
      }
      --depth;
    }
  }
  if (depth != 0 || in_string) {
    out.error = "unterminated constructor argument list";
    return out;
  }
  out.ok = true;
  return out;
}

bool split_top_level_items(const std::string& text, std::vector<std::string>& out) {
  out.clear();
  std::string current;
  int depth_paren = 0;
  int depth_bracket = 0;
  bool in_string = false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char ch = text[i];
    if (in_string) {
      current.push_back(ch);
      if (ch == '\\' && i + 1 < text.size()) {
        current.push_back(text[++i]);
        continue;
      }
      if (ch == '"') {
        in_string = false;
      }
      continue;
    }
    if (ch == '"') {
      in_string = true;
      current.push_back(ch);
      continue;
    }
    if (ch == '(') {
      ++depth_paren;
      current.push_back(ch);
      continue;
    }
    if (ch == ')') {
      --depth_paren;
      current.push_back(ch);
      continue;
    }
    if (ch == '[') {
      ++depth_bracket;
      current.push_back(ch);
      continue;
    }
    if (ch == ']') {
      --depth_bracket;
      current.push_back(ch);
      continue;
    }
    if (ch == ',' && depth_paren == 0 && depth_bracket == 0) {
      const std::string item = trim(current);
      if (!item.empty()) {
        out.push_back(item);
      }
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  const std::string tail = trim(current);
  if (!tail.empty()) {
    out.push_back(tail);
  }
  return !in_string && depth_paren == 0 && depth_bracket == 0;
}

std::string parse_simple_identifier_expr(const std::string& expr) {
  const std::string clean = trim(expr);
  if (clean.empty() || !is_ident_start(clean[0])) {
    return "";
  }
  for (std::size_t i = 1; i < clean.size(); ++i) {
    if (!is_ident_body(clean[i])) {
      return "";
    }
  }
  return clean;
}

bool parse_tuple_literal_expr(const std::string& expr, std::vector<std::string>& elements) {
  std::string clean = trim(expr);
  if (clean.size() < 5 || clean.front() != '(' || clean.back() != ')' || clean.find(',') == std::string::npos) {
    return false;
  }
  clean = clean.substr(1, clean.size() - 2);
  if (!split_top_level_items(clean, elements)) {
    return false;
  }
  return elements.size() >= 2;
}

bool parse_array_literal_expr(const std::string& expr, std::vector<std::string>& elements) {
  std::string clean = trim(expr);
  if (clean.size() < 2 || clean.front() != '[' || clean.back() != ']') {
    return false;
  }
  clean = clean.substr(1, clean.size() - 2);
  if (trim(clean).empty()) {
    elements.clear();
    return true;
  }
  return split_top_level_items(clean, elements);
}

bool parse_tuple_destructure_let(const std::string& line, std::vector<std::string>& names, std::string& expr) {
  names.clear();
  expr.clear();
  const std::string clean = trim(line);
  if (!starts_with(clean, "let (")) {
    return false;
  }
  const std::size_t open = clean.find('(');
  const std::size_t close = clean.find(')', open == std::string::npos ? 0 : open + 1);
  const std::size_t eq = clean.find('=', close == std::string::npos ? 0 : close + 1);
  if (open == std::string::npos || close == std::string::npos || eq == std::string::npos || close >= eq) {
    return false;
  }
  std::vector<std::string> parts;
  if (!split_top_level_items(clean.substr(open + 1, close - open - 1), parts)) {
    return false;
  }
  for (const std::string& part : parts) {
    if (part.empty() || !is_ident_start(part[0])) {
      return false;
    }
    for (std::size_t i = 1; i < part.size(); ++i) {
      if (!is_ident_body(part[i])) {
        return false;
      }
    }
    names.push_back(part);
  }
  expr = trim(clean.substr(eq + 1));
  return !names.empty() && !expr.empty();
}

bool is_interpolated_literal(const std::string& text) {
  const std::string clean = trim(text);
  if (clean.size() >= 3 && clean[0] == 'v' && clean[1] == '"' && clean.back() == '"') {
    return true;
  }
  return clean.size() >= 2 && clean.front() == '"' && clean.back() == '"' && clean.find('{') != std::string::npos &&
         clean.find('}') != std::string::npos;
}

bool parse_closure_literal(const std::string& text, std::vector<std::string>& params, std::string& body_expr,
                                  bool& block_body) {
  const std::string clean = trim(text);
  if (clean.size() < 4 || clean[0] != '|') {
    return false;
  }
  const std::size_t second = clean.find('|', 1);
  if (second == std::string::npos || second <= 1) {
    return false;
  }
  const std::string param_text = trim(clean.substr(1, second - 1));
  body_expr = trim(clean.substr(second + 1));
  block_body = false;
  if (param_text.empty() || body_expr.empty()) {
    return false;
  }
  params.clear();
  std::size_t i = 0;
  while (i < param_text.size()) {
    std::size_t comma = param_text.find(',', i);
    if (comma == std::string::npos) {
      comma = param_text.size();
    }
    std::string part = trim(param_text.substr(i, comma - i));
    if (part.empty() || !is_ident_start(part[0])) {
      return false;
    }
    for (std::size_t k = 1; k < part.size(); ++k) {
      if (!is_ident_body(part[k])) {
        return false;
      }
    }
    params.push_back(part);
    i = comma + 1;
  }
  if (params.empty()) {
    return false;
  }
  if (body_expr.size() >= 2 && body_expr.front() == '{' && body_expr.back() == '}') {
    body_expr = trim(body_expr.substr(1, body_expr.size() - 2));
    block_body = true;
  }
  if (body_expr.empty()) {
    return false;
  }
  return true;
}

std::vector<std::string> collect_closure_captures(const std::vector<std::string>& params, const std::string& body) {
  std::unordered_set<std::string> param_set(params.begin(), params.end());
  std::unordered_set<std::string> seen;
  std::vector<std::string> out;
  for (std::size_t i = 0; i < body.size();) {
    if (!is_ident_start(body[i])) {
      ++i;
      continue;
    }
    const std::size_t start = i;
    while (i < body.size() && is_ident_body(body[i])) {
      ++i;
    }
    const std::string name = body.substr(start, i - start);
    if (param_set.find(name) != param_set.end()) {
      continue;
    }
    if (name == "true" || name == "false" || name == "Some" || name == "None" || name == "Ok" || name == "Err") {
      continue;
    }
    if (seen.insert(name).second) {
      out.push_back(name);
    }
  }
  return out;
}

bool parse_try_operator_expr(const std::string& text, std::string& inner) {
  std::string clean = trim(text);
  if (clean.empty() || clean.back() != '?') {
    return false;
  }
  inner = trim(clean.substr(0, clean.size() - 1));
  return !inner.empty();
}

bool parse_tuple_field_access(const std::string& text, std::string& base, std::size_t& index) {
  const std::string clean = trim(text);
  const std::size_t dot = clean.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= clean.size()) {
    return false;
  }
  if (clean.find('.', dot + 1) != std::string::npos) {
    return false;
  }
  base = clean.substr(0, dot);
  const std::string idx_text = clean.substr(dot + 1);
  if (base.empty() || !is_ident_start(base[0])) {
    return false;
  }
  for (std::size_t i = 1; i < base.size(); ++i) {
    if (!is_ident_body(base[i])) {
      return false;
    }
  }
  if (idx_text.empty()) {
    return false;
  }
  for (char ch : idx_text) {
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return false;
    }
  }
  index = static_cast<std::size_t>(std::stoul(idx_text));
  return true;
}

bool parse_array_index_access(const std::string& text, std::string& base, std::string& index_expr) {
  const std::string clean = trim(text);
  const std::size_t lbr = clean.find('[');
  const std::size_t rbr = clean.rfind(']');
  if (lbr == std::string::npos || rbr == std::string::npos || rbr <= lbr + 1) {
    return false;
  }
  if (trim(clean.substr(rbr + 1)).empty() == false) {
    return false;
  }
  base = trim(clean.substr(0, lbr));
  index_expr = trim(clean.substr(lbr + 1, rbr - lbr - 1));
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

bool parse_len_call(const std::string& text, std::string& base) {
  const std::string clean = trim(text);
  if (!starts_with(clean, "len(") || !ends_with(clean, ")")) {
    return false;
  }
  base = trim(clean.substr(4, clean.size() - 5));
  if (base.empty() || !is_ident_start(base[0])) {
    return false;
  }
  for (std::size_t i = 1; i < base.size(); ++i) {
    if (!is_ident_body(base[i])) {
      return false;
    }
  }
  return true;
}

std::optional<int> builtin_variant_tag(const std::string& name) {
  if (name == "None") return 0;
  if (name == "Some") return 1;
  if (name == "Ok") return 2;
  if (name == "Err") return 3;
  return std::nullopt;
}

int encode_enum_with_payload(int tag, int payload) {
  return ((tag + 1) << kEnumPayloadShift) | (payload & kEnumPayloadMask);
}

llvm::Value* extract_enum_tag_value(llvm::Value* encoded, llvm::IRBuilder<>& builder) {
  llvm::Value* shifted = builder.CreateAShr(encoded, builder.getInt32(kEnumPayloadShift));
  return builder.CreateSub(shifted, builder.getInt32(1));
}

llvm::Value* extract_enum_payload_value(llvm::Value* encoded, llvm::IRBuilder<>& builder) {
  const int payload_shift = 32 - kEnumPayloadShift;
  llvm::Value* shl = builder.CreateShl(encoded, builder.getInt32(payload_shift));
  return builder.CreateAShr(shl, builder.getInt32(payload_shift));
}

static const ExprTok& cur(const ExprCursor& cursor) {
  if (cursor.index >= cursor.tokens.size()) {
    static const ExprTok end{ExprTokKind::End, ""};
    return end;
  }
  return cursor.tokens[cursor.index];
}

static ExprValue parse_expression_equality(ExprCursor& cursor);

static ExprValue parse_primary(ExprCursor& cursor) {
  const ExprTok& tok = cur(cursor);
  if (tok.kind == ExprTokKind::Op && tok.text == "-") {
    ++cursor.index;
    ExprValue rhs = parse_primary(cursor);
    const bool numeric =
        rhs.type == ValueType::I32 || rhs.type == ValueType::I64 || rhs.type == ValueType::F32 ||
        rhs.type == ValueType::F64;
    if (!numeric || rhs.value == nullptr) {
      cursor.error = cursor.error.empty() ? "unary '-' requires numeric operand" : cursor.error;
      return {};
    }
    if (rhs.type == ValueType::I32) {
      return ExprValue{cursor.builder->CreateNeg(rhs.value), ValueType::I32};
    }
    if (rhs.type == ValueType::I64) {
      return ExprValue{cursor.builder->CreateNeg(rhs.value), ValueType::I64};
    }
    if (rhs.type == ValueType::F32) {
      return ExprValue{cursor.builder->CreateFNeg(rhs.value), ValueType::F32};
    }
    return ExprValue{cursor.builder->CreateFNeg(rhs.value), ValueType::F64};
  }

  if (tok.kind == ExprTokKind::Atom) {
    ++cursor.index;
    if (cur(cursor).kind == ExprTokKind::LParen) {
      ++cursor.index;  // '('
      std::vector<ExprValue> args;
      if (cur(cursor).kind != ExprTokKind::RParen) {
        while (true) {
          ExprValue arg = parse_expression_equality(cursor);
          if (arg.value == nullptr || arg.type == ValueType::Invalid) {
            return {};
          }
          args.push_back(arg);
          if (cur(cursor).kind == ExprTokKind::Comma) {
            ++cursor.index;
            continue;
          }
          break;
        }
      }
      if (cur(cursor).kind != ExprTokKind::RParen) {
        cursor.error = "missing ')' in function call";
        return {};
      }
      ++cursor.index;
      auto resolve_variant_tag = [&](int& out_tag) {
        if (cursor.enum_variant_tags != nullptr) {
          auto it = cursor.enum_variant_tags->find(tok.text);
          if (it != cursor.enum_variant_tags->end()) {
            out_tag = it->second;
            return true;
          }
        }
        const std::optional<int> builtin = builtin_variant_tag(tok.text);
        if (builtin.has_value()) {
          out_tag = *builtin;
          return true;
        }
        return false;
      };

      if (tok.text == "is_some" || tok.text == "is_none" || tok.text == "is_ok" || tok.text == "is_err") {
        if (args.size() != 1) {
          cursor.error = tok.text + "() expects exactly 1 argument";
          return {};
        }
        llvm::Value* encoded = args[0].type == ValueType::I32 ? args[0].value : nullptr;
        if (encoded == nullptr) {
          cursor.error = tok.text + "() argument must be i32-compatible";
          return {};
        }
        const int target_tag = (tok.text == "is_some") ? 1 : (tok.text == "is_none") ? 0 : (tok.text == "is_ok") ? 2 : 3;
        llvm::Value* tag = extract_enum_tag_value(encoded, *cursor.builder);
        return ExprValue{cursor.builder->CreateICmpEQ(tag, cursor.builder->getInt32(target_tag)), ValueType::I1};
      }

      if (tok.text == "unwrap") {
        if (args.size() != 1 || args[0].type != ValueType::I32) {
          cursor.error = "unwrap() expects a single i32-encoded Option/Result argument";
          return {};
        }
        return ExprValue{extract_enum_payload_value(args[0].value, *cursor.builder), ValueType::I32};
      }

      if (tok.text == "unwrap_or") {
        if (args.size() != 2) {
          cursor.error = "unwrap_or() expects 2 arguments";
          return {};
        }
        if (args[0].type != ValueType::I32) {
          cursor.error = "unwrap_or() first argument must be i32-encoded Option/Result";
          return {};
        }
        llvm::Value* fallback = nullptr;
        if (args[1].type == ValueType::I32) {
          fallback = args[1].value;
        } else if (args[1].type == ValueType::I1) {
          fallback = cursor.builder->CreateZExt(args[1].value, cursor.builder->getInt32Ty());
        } else if (args[1].type == ValueType::F32 || args[1].type == ValueType::F64) {
          fallback = cursor.builder->CreateFPToSI(args[1].value, cursor.builder->getInt32Ty());
        }
        if (fallback == nullptr) {
          cursor.error = "unwrap_or() fallback argument is not i32-compatible";
          return {};
        }
        llvm::Value* tag = extract_enum_tag_value(args[0].value, *cursor.builder);
        llvm::Value* payload = extract_enum_payload_value(args[0].value, *cursor.builder);
        llvm::Value* is_some = cursor.builder->CreateOr(
            cursor.builder->CreateICmpEQ(tag, cursor.builder->getInt32(1)),
            cursor.builder->CreateICmpEQ(tag, cursor.builder->getInt32(2)));
        return ExprValue{cursor.builder->CreateSelect(is_some, payload, fallback), ValueType::I32};
      }

      if (tok.text == "Rc" || tok.text == "Arc") {
        if (args.size() != 1) {
          cursor.error = tok.text + "(...) expects exactly one argument";
          return {};
        }
        const VariableSlot::Ownership owner =
            tok.text == "Arc" ? VariableSlot::Ownership::Arc : VariableSlot::Ownership::Rc;
        llvm::Value* value_i64 = to_i64(args[0], *cursor.builder);
        if (value_i64 == nullptr) {
          cursor.error = tok.text + "(...) argument must be i32/bool/float-compatible";
          return {};
        }
        llvm::Value* handle = emit_ref_new_call(owner, value_i64, cursor.current_function, *cursor.builder);
        if (handle == nullptr) {
          cursor.error = "failed to emit runtime call for " + tok.text;
          return {};
        }
        return ExprValue{handle, ValueType::I8Ptr};
      }

      if (tok.text == "open" || tok.text == "close" || tok.text == "read" || tok.text == "write") {
        if (args.empty()) {
          return ExprValue{cursor.builder->getInt32(0), ValueType::I32};
        }
        if (args[0].type == ValueType::I32) {
          return ExprValue{args[0].value, ValueType::I32};
        }
        if (args[0].type == ValueType::I1) {
          return ExprValue{cursor.builder->CreateZExt(args[0].value, cursor.builder->getInt32Ty()), ValueType::I32};
        }
        return ExprValue{cursor.builder->getInt32(0), ValueType::I32};
      }
      if (tok.text == "spawn") {
        if (args.size() != 1) {
          cursor.error = "spawn() expects exactly 1 argument";
          return {};
        }
        return ExprValue{cursor.builder->getInt32(0), ValueType::I32};
      }

      int variant_tag = -1;
      if (resolve_variant_tag(variant_tag)) {
        int payload_const = 0;
        llvm::Value* payload_value = nullptr;
        if (!args.empty()) {
          if (args.size() != 1) {
            cursor.error = "enum payload constructor expects at most one payload";
            return {};
          }
          if (args[0].type == ValueType::I32) {
            payload_value = args[0].value;
          } else if (args[0].type == ValueType::I1) {
            payload_value = cursor.builder->CreateZExt(args[0].value, cursor.builder->getInt32Ty());
          } else if (args[0].type == ValueType::F32 || args[0].type == ValueType::F64) {
            payload_value = cursor.builder->CreateFPToSI(args[0].value, cursor.builder->getInt32Ty());
          } else {
            cursor.error = "enum payload must be i32-compatible";
            return {};
          }
        } else {
          payload_const = 0;
        }
        llvm::Value* encoded = cursor.builder->getInt32(encode_enum_with_payload(variant_tag, payload_const));
        if (payload_value != nullptr) {
          llvm::Value* masked = cursor.builder->CreateAnd(payload_value, cursor.builder->getInt32(kEnumPayloadMask));
          llvm::Value* head = cursor.builder->getInt32((variant_tag + 1) << kEnumPayloadShift);
          encoded = cursor.builder->CreateOr(head, masked);
        }
        return ExprValue{encoded, ValueType::I32};
      }

      if (cursor.closures != nullptr) {
        auto closure_it = cursor.closures->find(tok.text);
        if (closure_it != cursor.closures->end()) {
          if (cursor.variables == nullptr || cursor.current_function == nullptr) {
            cursor.error = "closure call requires variable scope";
            return {};
          }
          if (args.size() != closure_it->second.params.size()) {
            cursor.error = "closure '" + tok.text + "' expects " + std::to_string(closure_it->second.params.size()) +
                           " arguments but got " + std::to_string(args.size());
            return {};
          }

          struct SavedBinding {
            std::string name;
            bool had_previous = false;
            VariableSlot slot;
          };
          std::vector<SavedBinding> saved;
          auto bind_i32 = [&](const std::string& name, llvm::Value* value) {
            SavedBinding entry;
            entry.name = name;
            auto prev_it = cursor.variables->find(name);
            if (prev_it != cursor.variables->end()) {
              entry.had_previous = true;
              entry.slot = prev_it->second;
            }
            saved.push_back(entry);
            llvm::AllocaInst* alloca = create_entry_alloca(cursor.current_function, cursor.builder->getInt32Ty(),
                                                           name + ".closure");
            cursor.builder->CreateStore(value, alloca);
            (*cursor.variables)[name] = VariableSlot{alloca, ValueType::I32};
          };

          for (std::size_t param_index = 0; param_index < closure_it->second.params.size(); ++param_index) {
            llvm::Value* arg_i32 = nullptr;
            const ExprValue& arg_value = args[param_index];
            if (arg_value.type == ValueType::I32) {
              arg_i32 = arg_value.value;
            } else if (arg_value.type == ValueType::I1) {
              arg_i32 = cursor.builder->CreateZExt(arg_value.value, cursor.builder->getInt32Ty());
            } else if (arg_value.type == ValueType::F32 || arg_value.type == ValueType::F64) {
              arg_i32 = cursor.builder->CreateFPToSI(arg_value.value, cursor.builder->getInt32Ty());
            }
            if (arg_i32 == nullptr) {
              cursor.error = "closure argument must be i32-compatible";
              return {};
            }
            bind_i32(closure_it->second.params[param_index], arg_i32);
          }

          for (const auto& [capture_name, capture_value] : closure_it->second.captured_i32_values) {
            if (capture_value == nullptr) {
              continue;
            }
            bind_i32(capture_name, capture_value);
          }

          std::string nested_error;
          ExprCursor nested;
          nested.tokens = tokenize_expression(closure_it->second.body_expr, nested_error);
          nested.builder = cursor.builder;
          nested.variables = cursor.variables;
          nested.enum_variant_tags = cursor.enum_variant_tags;
          nested.struct_fields = cursor.struct_fields;
          nested.struct_field_types = cursor.struct_field_types;
          nested.struct_instances = cursor.struct_instances;
          nested.tuple_instances = cursor.tuple_instances;
          nested.array_instances = cursor.array_instances;
          nested.functions = cursor.functions;
          nested.function_returns = cursor.function_returns;
          nested.closures = cursor.closures;
          nested.current_function = cursor.current_function;
          if (!nested_error.empty()) {
            cursor.error = "cannot tokenize closure body: " + nested_error;
            return {};
          }
          ExprValue closure_result = parse_expression_equality(nested);
          if (closure_result.value == nullptr || closure_result.type == ValueType::Invalid ||
              cur(nested).kind != ExprTokKind::End) {
            cursor.error = nested.error.empty() ? "cannot evaluate closure body" : nested.error;
            return {};
          }

          for (auto it = saved.rbegin(); it != saved.rend(); ++it) {
            if (it->had_previous) {
              (*cursor.variables)[it->name] = it->slot;
            } else {
              cursor.variables->erase(it->name);
            }
          }
          return closure_result;
        }
      }

      std::string call_base;
      std::string call_member;
      const bool dotted_call = split_dotted_name(tok.text, call_base, call_member);
      if (dotted_call && cursor.struct_instances != nullptr && cursor.functions != nullptr &&
          cursor.function_returns != nullptr) {
        auto receiver_it = cursor.struct_instances->find(call_base);
        if (receiver_it != cursor.struct_instances->end()) {
          const std::string method_symbol = receiver_it->second.struct_name + "." + call_member;
          auto fn_it = cursor.functions->find(method_symbol);
          auto ret_it = cursor.function_returns->find(method_symbol);
          if (fn_it == cursor.functions->end() || ret_it == cursor.function_returns->end()) {
            cursor.error = "unknown method '" + call_member + "' for struct '" + receiver_it->second.struct_name + "'";
            return {};
          }
          llvm::Function* callee = fn_it->second;
          if (callee == nullptr) {
            cursor.error = "null callee for method '" + method_symbol + "'";
            return {};
          }
          if (callee->arg_size() != args.size() + 1) {
            cursor.error = "method '" + call_member + "' expects " + std::to_string(callee->arg_size() - 1) +
                           " args but got " + std::to_string(args.size());
            return {};
          }
          std::vector<llvm::Value*> call_args;
          call_args.reserve(args.size() + 1);
          llvm::Value* self_ptr = receiver_it->second.ptr;
          if (self_ptr == nullptr || !self_ptr->getType()->isPointerTy()) {
            cursor.error = "method receiver '" + call_base + "' is not addressable";
            return {};
          }
          llvm::Type* self_target_ty = callee->getFunctionType()->getParamType(0);
          if (self_target_ty->isPointerTy() && self_ptr->getType() != self_target_ty) {
            self_ptr = cursor.builder->CreateBitCast(self_ptr, self_target_ty, call_base + ".self.cast");
          }
          call_args.push_back(self_ptr);
          std::size_t arg_index = 0;
          std::size_t callee_index = 0;
          for (llvm::Argument& arg : callee->args()) {
            if (callee_index == 0) {
              ++callee_index;
              continue;
            }
            ExprValue& value = args[arg_index];
            llvm::Type* target_ty = arg.getType();
            llvm::Value* coerced = nullptr;
            if (target_ty->isIntegerTy(32)) {
              if (value.type == ValueType::I32) {
                coerced = value.value;
              } else if (value.type == ValueType::I64) {
                coerced = cursor.builder->CreateTrunc(value.value, cursor.builder->getInt32Ty());
              } else if (value.type == ValueType::I1) {
                coerced = cursor.builder->CreateZExt(value.value, cursor.builder->getInt32Ty());
              } else if (value.type == ValueType::F32 || value.type == ValueType::F64) {
                coerced = cursor.builder->CreateFPToSI(value.value, cursor.builder->getInt32Ty());
              }
            } else if (target_ty->isIntegerTy(64)) {
              if (value.type == ValueType::I64) {
                coerced = value.value;
              } else if (value.type == ValueType::I32) {
                coerced = cursor.builder->CreateSExt(value.value, cursor.builder->getInt64Ty());
              } else if (value.type == ValueType::I1) {
                coerced = cursor.builder->CreateZExt(value.value, cursor.builder->getInt64Ty());
              } else if (value.type == ValueType::F32 || value.type == ValueType::F64) {
                coerced = cursor.builder->CreateFPToSI(value.value, cursor.builder->getInt64Ty());
              }
            } else if (target_ty->isFloatTy()) {
              if (value.type == ValueType::F32) {
                coerced = value.value;
              } else if (value.type == ValueType::F64) {
                coerced = cursor.builder->CreateFPTrunc(value.value, cursor.builder->getFloatTy());
              } else if (value.type == ValueType::I32) {
                coerced = cursor.builder->CreateSIToFP(value.value, cursor.builder->getFloatTy());
              } else if (value.type == ValueType::I64) {
                coerced = cursor.builder->CreateSIToFP(value.value, cursor.builder->getFloatTy());
              }
            } else if (target_ty->isDoubleTy()) {
              if (value.type == ValueType::F64) {
                coerced = value.value;
              } else if (value.type == ValueType::F32) {
                coerced = cursor.builder->CreateFPExt(value.value, cursor.builder->getDoubleTy());
              } else if (value.type == ValueType::I32) {
                coerced = cursor.builder->CreateSIToFP(value.value, cursor.builder->getDoubleTy());
              } else if (value.type == ValueType::I64) {
                coerced = cursor.builder->CreateSIToFP(value.value, cursor.builder->getDoubleTy());
              }
            } else if (target_ty->isIntegerTy(1)) {
              if (value.type == ValueType::I1) {
                coerced = value.value;
              } else if (value.type == ValueType::I32) {
                coerced = cursor.builder->CreateICmpNE(value.value, cursor.builder->getInt32(0));
              } else if (value.type == ValueType::I64) {
                coerced = cursor.builder->CreateICmpNE(value.value, cursor.builder->getInt64(0));
              }
            } else if (target_ty->isPointerTy()) {
              if (value.type == ValueType::I8Ptr) {
                coerced = value.value;
                if (coerced != nullptr && coerced->getType() != target_ty) {
                  coerced = cursor.builder->CreateBitCast(coerced, target_ty);
                }
              }
            }
            if (coerced == nullptr) {
              cursor.error = "cannot convert argument for method call '" + tok.text + "'";
              return {};
            }
            call_args.push_back(coerced);
            ++arg_index;
            ++callee_index;
          }
          llvm::CallInst* call = cursor.builder->CreateCall(callee, call_args);
          const ValueType ret_type = ret_it->second;
          if (ret_type == ValueType::Void) {
            return ExprValue{cursor.builder->getInt32(0), ValueType::I32};
          }
          return ExprValue{call, ret_type};
        }
      }

      if (cursor.functions == nullptr || cursor.function_returns == nullptr) {
        cursor.error = "function call resolution context is missing";
        return {};
      }
      auto fn_it = cursor.functions->find(tok.text);
      auto ret_it = cursor.function_returns->find(tok.text);
      if (fn_it == cursor.functions->end() || ret_it == cursor.function_returns->end()) {
        cursor.error = "unknown function call '" + tok.text + "'";
        return {};
      }
      llvm::Function* callee = fn_it->second;
      if (callee == nullptr) {
        cursor.error = "null callee for '" + tok.text + "'";
        return {};
      }
      std::vector<llvm::Value*> call_args;
      call_args.reserve(args.size());
      if (callee->arg_size() != args.size()) {
        cursor.error = "function '" + tok.text + "' expects " + std::to_string(callee->arg_size()) +
                       " args but got " + std::to_string(args.size());
        return {};
      }
      std::size_t arg_index = 0;
      for (llvm::Argument& arg : callee->args()) {
        ExprValue& value = args[arg_index];
        llvm::Type* target_ty = arg.getType();
        llvm::Value* coerced = nullptr;
        if (target_ty->isIntegerTy(32)) {
          if (value.type == ValueType::I32) {
            coerced = value.value;
          } else if (value.type == ValueType::I64) {
            coerced = cursor.builder->CreateTrunc(value.value, cursor.builder->getInt32Ty());
          } else if (value.type == ValueType::I1) {
            coerced = cursor.builder->CreateZExt(value.value, cursor.builder->getInt32Ty());
          } else if (value.type == ValueType::F32) {
            coerced = cursor.builder->CreateFPToSI(value.value, cursor.builder->getInt32Ty());
          } else if (value.type == ValueType::F64) {
            coerced = cursor.builder->CreateFPToSI(value.value, cursor.builder->getInt32Ty());
          }
        } else if (target_ty->isIntegerTy(64)) {
          if (value.type == ValueType::I64) {
            coerced = value.value;
          } else if (value.type == ValueType::I32) {
            coerced = cursor.builder->CreateSExt(value.value, cursor.builder->getInt64Ty());
          } else if (value.type == ValueType::I1) {
            coerced = cursor.builder->CreateZExt(value.value, cursor.builder->getInt64Ty());
          } else if (value.type == ValueType::F32 || value.type == ValueType::F64) {
            coerced = cursor.builder->CreateFPToSI(value.value, cursor.builder->getInt64Ty());
          }
        } else if (target_ty->isFloatTy()) {
          if (value.type == ValueType::F32) {
            coerced = value.value;
          } else if (value.type == ValueType::F64) {
            coerced = cursor.builder->CreateFPTrunc(value.value, cursor.builder->getFloatTy());
          } else if (value.type == ValueType::I32) {
            coerced = cursor.builder->CreateSIToFP(value.value, cursor.builder->getFloatTy());
          } else if (value.type == ValueType::I64) {
            coerced = cursor.builder->CreateSIToFP(value.value, cursor.builder->getFloatTy());
          }
        } else if (target_ty->isDoubleTy()) {
          if (value.type == ValueType::F64) {
            coerced = value.value;
          } else if (value.type == ValueType::F32) {
            coerced = cursor.builder->CreateFPExt(value.value, cursor.builder->getDoubleTy());
          } else if (value.type == ValueType::I32) {
            coerced = cursor.builder->CreateSIToFP(value.value, cursor.builder->getDoubleTy());
          } else if (value.type == ValueType::I64) {
            coerced = cursor.builder->CreateSIToFP(value.value, cursor.builder->getDoubleTy());
          }
        } else if (target_ty->isIntegerTy(1)) {
          if (value.type == ValueType::I1) {
            coerced = value.value;
          } else if (value.type == ValueType::I32) {
            coerced = cursor.builder->CreateICmpNE(value.value, cursor.builder->getInt32(0));
          } else if (value.type == ValueType::I64) {
            coerced = cursor.builder->CreateICmpNE(value.value, cursor.builder->getInt64(0));
          }
        } else if (target_ty->isPointerTy()) {
          if (value.type == ValueType::I8Ptr) {
            coerced = value.value;
            if (coerced != nullptr && coerced->getType() != target_ty) {
              coerced = cursor.builder->CreateBitCast(coerced, target_ty);
            }
          }
        }
        if (coerced == nullptr) {
          cursor.error = "cannot convert argument for call '" + tok.text + "'";
          return {};
        }
        call_args.push_back(coerced);
        ++arg_index;
      }
      llvm::CallInst* call = cursor.builder->CreateCall(callee, call_args);
      const ValueType ret_type = ret_it->second;
      if (ret_type == ValueType::Void) {
        return ExprValue{cursor.builder->getInt32(0), ValueType::I32};
      }
      return ExprValue{call, ret_type};
    }
    if (tok.text == "true") {
      return ExprValue{cursor.builder->getInt1(true), ValueType::I1};
    }
    if (tok.text == "false") {
      return ExprValue{cursor.builder->getInt1(false), ValueType::I1};
    }
    if (is_integer_atom(tok.text)) {
      std::int64_t parsed = 0;
      try {
        parsed = std::stoll(tok.text);
      } catch (const std::exception&) {
        cursor.error = "invalid integer literal: '" + tok.text + "'";
        return {};
      }
      if (parsed >= std::numeric_limits<std::int32_t>::min() &&
          parsed <= std::numeric_limits<std::int32_t>::max()) {
        return ExprValue{cursor.builder->getInt32(static_cast<int32_t>(parsed)), ValueType::I32};
      }
      return ExprValue{cursor.builder->getInt64(parsed), ValueType::I64};
    }
    if (is_float_atom(tok.text)) {
      return ExprValue{llvm::ConstantFP::get(cursor.builder->getDoubleTy(), std::stod(tok.text)), ValueType::F64};
    }
    if (is_string_atom(tok.text)) {
      const std::string text = unescape_string_body(tok.text.substr(1, tok.text.size() - 2));
      return ExprValue{create_global_cstr_ptr(*cursor.builder, text, "strlit"), ValueType::I8Ptr};
    }
    if (is_interpolated_literal(tok.text)) {
      std::string text = tok.text;
      if (text.size() >= 3 && text[0] == 'v' && text[1] == '"') {
        text = text.substr(2, text.size() - 3);
      } else if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        text = text.substr(1, text.size() - 2);
      }
      text = unescape_string_body(text);
      return ExprValue{create_global_cstr_ptr(*cursor.builder, text, "istrlit"), ValueType::I8Ptr};
    }
    std::vector<std::string> closure_params;
    std::string closure_body;
    bool closure_block = false;
    if (parse_closure_literal(tok.text, closure_params, closure_body, closure_block)) {
      return ExprValue{cursor.builder->getInt32(0), ValueType::I32};
    }
    std::string field_base;
    std::string field_name;
    std::size_t tuple_field_index = 0;
    if (parse_tuple_field_access(tok.text, field_base, tuple_field_index) && cursor.tuple_instances != nullptr) {
      auto tuple_it = cursor.tuple_instances->find(field_base);
      if (tuple_it != cursor.tuple_instances->end()) {
        if (tuple_field_index >= tuple_it->second.element_types.size()) {
          cursor.error = "tuple index out of bounds for '" + tok.text + "'";
          return {};
        }
        llvm::Value* field_ptr = cursor.builder->CreateStructGEP(tuple_it->second.llvm_type, tuple_it->second.alloca,
                                                                 static_cast<unsigned>(tuple_field_index),
                                                                 tok.text + ".ptr");
        const ValueType field_ty = tuple_it->second.element_types[tuple_field_index];
        llvm::Value* loaded = cursor.builder->CreateLoad(llvm_type_from_value_type(field_ty, *cursor.builder), field_ptr);
        return ExprValue{loaded, field_ty};
      }
    }
    if (split_dotted_name(tok.text, field_base, field_name) && cursor.struct_instances != nullptr &&
        cursor.struct_fields != nullptr && cursor.struct_field_types != nullptr) {
      auto inst_it = cursor.struct_instances->find(field_base);
      if (inst_it != cursor.struct_instances->end()) {
        const std::size_t field_index =
            field_index_for_struct(inst_it->second.struct_name, field_name, *cursor.struct_fields);
        if (field_index == static_cast<std::size_t>(-1)) {
          cursor.error = "unknown field '" + field_name + "' on struct '" + inst_it->second.struct_name + "'";
          return {};
        }
        llvm::Value* ptr = inst_it->second.ptr;
        if (ptr == nullptr || !ptr->getType()->isPointerTy()) {
          cursor.error = "field access receiver is not addressable";
          return {};
        }
        if (inst_it->second.llvm_type == nullptr) {
          cursor.error = "missing LLVM struct layout for '" + inst_it->second.struct_name + "'";
          return {};
        }
        llvm::Type* expected_ptr_ty = inst_it->second.llvm_type->getPointerTo();
        if (ptr->getType() != expected_ptr_ty) {
          ptr = cursor.builder->CreateBitCast(ptr, expected_ptr_ty, field_base + ".typed.ptr");
        }
        llvm::Value* field_ptr = cursor.builder->CreateStructGEP(inst_it->second.llvm_type, ptr,
                                                                 static_cast<unsigned>(field_index),
                                                                 field_base + "." + field_name + ".ptr");
        const ValueType field_ty =
            field_value_type_for_struct(inst_it->second.struct_name, field_name, *cursor.struct_field_types);
        llvm::Value* loaded =
            cursor.builder->CreateLoad(llvm_type_from_value_type(field_ty, *cursor.builder), field_ptr);
        return ExprValue{loaded, field_ty};
      }
    }
    if (cursor.variables != nullptr) {
      const auto it = cursor.variables->find(tok.text);
      if (it != cursor.variables->end() && it->second.alloca != nullptr) {
        llvm::Value* loaded = cursor.builder->CreateLoad(it->second.alloca->getAllocatedType(), it->second.alloca);
        return ExprValue{loaded, it->second.type};
      }
    }
    if (cursor.enum_variant_tags != nullptr) {
      const auto variant_it = cursor.enum_variant_tags->find(tok.text);
      if (variant_it != cursor.enum_variant_tags->end()) {
        return ExprValue{cursor.builder->getInt32(encode_enum_with_payload(variant_it->second, 0)), ValueType::I32};
      }
    }
    if (const std::optional<int> builtin = builtin_variant_tag(tok.text); builtin.has_value()) {
      return ExprValue{cursor.builder->getInt32(encode_enum_with_payload(*builtin, 0)), ValueType::I32};
    }
    if (cursor.closures != nullptr) {
      auto closure_it = cursor.closures->find(tok.text);
      if (closure_it != cursor.closures->end()) {
        return ExprValue{cursor.builder->getInt32(0), ValueType::I32};
      }
    }
    cursor.error = "unsupported atom in backend expression: '" + tok.text + "'";
    return {};
  }

  if (tok.kind == ExprTokKind::LParen) {
    ++cursor.index;
    ExprValue inner = parse_expression_equality(cursor);
    if (inner.type == ValueType::Invalid || inner.value == nullptr) {
      return {};
    }
    if (cur(cursor).kind != ExprTokKind::RParen) {
      cursor.error = "missing ')' in backend expression";
      return {};
    }
    ++cursor.index;
    return inner;
  }

  cursor.error = "expected expression atom in backend expression";
  return {};
}

static ExprValue parse_multiplicative(ExprCursor& cursor) {
  ExprValue lhs = parse_primary(cursor);
  if (lhs.type == ValueType::Invalid || lhs.value == nullptr) {
    return {};
  }
  while (cur(cursor).kind == ExprTokKind::Op && (cur(cursor).text == "*" || cur(cursor).text == "/")) {
    const std::string op = cur(cursor).text;
    ++cursor.index;
    ExprValue rhs = parse_primary(cursor);
    if (!is_numeric_type(lhs.type) || !is_numeric_type(rhs.type) || rhs.value == nullptr) {
      cursor.error = "operator '" + op + "' requires numeric operands";
      return {};
    }
    const bool use_float = is_float_type(lhs.type) || is_float_type(rhs.type);
    if (use_float) {
      const ValueType float_type = promoted_float_type(lhs.type, rhs.type);
      llvm::Value* lhs_f = to_float_value(lhs, float_type, *cursor.builder);
      llvm::Value* rhs_f = to_float_value(rhs, float_type, *cursor.builder);
      if (lhs_f == nullptr || rhs_f == nullptr) {
        cursor.error = "cannot convert operands to float for operator '" + op + "'";
        return {};
      }
      lhs.value = (op == "*") ? cursor.builder->CreateFMul(lhs_f, rhs_f) : cursor.builder->CreateFDiv(lhs_f, rhs_f);
      lhs.type = float_type;
    } else {
      const ValueType int_type = promoted_integer_type(lhs.type, rhs.type);
      llvm::Value* lhs_i = to_integer_numeric_value(lhs, int_type, *cursor.builder);
      llvm::Value* rhs_i = to_integer_numeric_value(rhs, int_type, *cursor.builder);
      if (lhs_i == nullptr || rhs_i == nullptr) {
        cursor.error = "cannot convert operands to integer for operator '" + op + "'";
        return {};
      }
      lhs.value = (op == "*") ? cursor.builder->CreateMul(lhs_i, rhs_i) : cursor.builder->CreateSDiv(lhs_i, rhs_i);
      lhs.type = int_type;
    }
  }
  return lhs;
}

static ExprValue parse_additive(ExprCursor& cursor) {
  ExprValue lhs = parse_multiplicative(cursor);
  if (lhs.type == ValueType::Invalid || lhs.value == nullptr) {
    return {};
  }
  while (cur(cursor).kind == ExprTokKind::Op && (cur(cursor).text == "+" || cur(cursor).text == "-")) {
    const std::string op = cur(cursor).text;
    ++cursor.index;
    ExprValue rhs = parse_multiplicative(cursor);
    if (!is_numeric_type(lhs.type) || !is_numeric_type(rhs.type) || rhs.value == nullptr) {
      cursor.error = "operator '" + op + "' requires numeric operands";
      return {};
    }
    const bool use_float = is_float_type(lhs.type) || is_float_type(rhs.type);
    if (use_float) {
      const ValueType float_type = promoted_float_type(lhs.type, rhs.type);
      llvm::Value* lhs_f = to_float_value(lhs, float_type, *cursor.builder);
      llvm::Value* rhs_f = to_float_value(rhs, float_type, *cursor.builder);
      if (lhs_f == nullptr || rhs_f == nullptr) {
        cursor.error = "cannot convert operands to float for operator '" + op + "'";
        return {};
      }
      lhs.value = (op == "+") ? cursor.builder->CreateFAdd(lhs_f, rhs_f) : cursor.builder->CreateFSub(lhs_f, rhs_f);
      lhs.type = float_type;
    } else {
      const ValueType int_type = promoted_integer_type(lhs.type, rhs.type);
      llvm::Value* lhs_i = to_integer_numeric_value(lhs, int_type, *cursor.builder);
      llvm::Value* rhs_i = to_integer_numeric_value(rhs, int_type, *cursor.builder);
      if (lhs_i == nullptr || rhs_i == nullptr) {
        cursor.error = "cannot convert operands to integer for operator '" + op + "'";
        return {};
      }
      lhs.value = (op == "+") ? cursor.builder->CreateAdd(lhs_i, rhs_i) : cursor.builder->CreateSub(lhs_i, rhs_i);
      lhs.type = int_type;
    }
  }
  return lhs;
}

static ExprValue parse_comparison(ExprCursor& cursor) {
  ExprValue lhs = parse_additive(cursor);
  if (lhs.type == ValueType::Invalid || lhs.value == nullptr) {
    return {};
  }
  while (cur(cursor).kind == ExprTokKind::Op &&
         (cur(cursor).text == "<" || cur(cursor).text == "<=" || cur(cursor).text == ">" ||
          cur(cursor).text == ">=")) {
    const std::string op = cur(cursor).text;
    ++cursor.index;
    ExprValue rhs = parse_additive(cursor);
    if (!is_numeric_type(lhs.type) || !is_numeric_type(rhs.type) || rhs.value == nullptr) {
      cursor.error = "comparison operator '" + op + "' requires numeric operands";
      return {};
    }
    const bool use_float = is_float_type(lhs.type) || is_float_type(rhs.type);
    if (use_float) {
      const ValueType float_type = promoted_float_type(lhs.type, rhs.type);
      llvm::Value* lhs_f = to_float_value(lhs, float_type, *cursor.builder);
      llvm::Value* rhs_f = to_float_value(rhs, float_type, *cursor.builder);
      if (lhs_f == nullptr || rhs_f == nullptr) {
        cursor.error = "cannot convert operands to float for comparison";
        return {};
      }
      if (op == "<") {
        lhs.value = cursor.builder->CreateFCmpOLT(lhs_f, rhs_f);
      } else if (op == "<=") {
        lhs.value = cursor.builder->CreateFCmpOLE(lhs_f, rhs_f);
      } else if (op == ">") {
        lhs.value = cursor.builder->CreateFCmpOGT(lhs_f, rhs_f);
      } else {
        lhs.value = cursor.builder->CreateFCmpOGE(lhs_f, rhs_f);
      }
    } else {
      const ValueType int_type = promoted_integer_type(lhs.type, rhs.type);
      llvm::Value* lhs_i = to_integer_numeric_value(lhs, int_type, *cursor.builder);
      llvm::Value* rhs_i = to_integer_numeric_value(rhs, int_type, *cursor.builder);
      if (lhs_i == nullptr || rhs_i == nullptr) {
        cursor.error = "cannot convert operands to integer for comparison";
        return {};
      }
      if (op == "<") {
        lhs.value = cursor.builder->CreateICmpSLT(lhs_i, rhs_i);
      } else if (op == "<=") {
        lhs.value = cursor.builder->CreateICmpSLE(lhs_i, rhs_i);
      } else if (op == ">") {
        lhs.value = cursor.builder->CreateICmpSGT(lhs_i, rhs_i);
      } else {
        lhs.value = cursor.builder->CreateICmpSGE(lhs_i, rhs_i);
      }
    }
    lhs.type = ValueType::I1;
  }
  return lhs;
}

static ExprValue parse_expression_equality(ExprCursor& cursor) {
  ExprValue lhs = parse_comparison(cursor);
  if (lhs.type == ValueType::Invalid || lhs.value == nullptr) {
    return {};
  }
  while (cur(cursor).kind == ExprTokKind::Op && cur(cursor).text == "?") {
    ++cursor.index;
    if (lhs.type != ValueType::I32) {
      cursor.error = "operator '?' requires i32-encoded Result/Option";
      return {};
    }
    lhs.value = extract_enum_payload_value(lhs.value, *cursor.builder);
    lhs.type = ValueType::I32;
  }
  while (cur(cursor).kind == ExprTokKind::Op && (cur(cursor).text == "==" || cur(cursor).text == "!=")) {
    const std::string op = cur(cursor).text;
    ++cursor.index;
    ExprValue rhs = parse_comparison(cursor);
    if (rhs.value == nullptr) {
      cursor.error = "equality operator '" + op + "' requires valid operands";
      return {};
    }
    if (is_numeric_type(lhs.type) && is_numeric_type(rhs.type)) {
      if (is_integer_numeric_type(lhs.type) && is_integer_numeric_type(rhs.type)) {
        const ValueType int_type = promoted_integer_type(lhs.type, rhs.type);
        llvm::Value* lhs_i = to_integer_numeric_value(lhs, int_type, *cursor.builder);
        llvm::Value* rhs_i = to_integer_numeric_value(rhs, int_type, *cursor.builder);
        if (lhs_i == nullptr || rhs_i == nullptr) {
          cursor.error = "cannot convert operands for equality operator '" + op + "'";
          return {};
        }
        lhs.value = (op == "==") ? cursor.builder->CreateICmpEQ(lhs_i, rhs_i)
                                 : cursor.builder->CreateICmpNE(lhs_i, rhs_i);
      } else {
        const ValueType float_type = promoted_float_type(lhs.type, rhs.type);
        llvm::Value* lhs_f = to_float_value(lhs, float_type, *cursor.builder);
        llvm::Value* rhs_f = to_float_value(rhs, float_type, *cursor.builder);
        if (lhs_f == nullptr || rhs_f == nullptr) {
          cursor.error = "cannot convert operands for equality operator '" + op + "'";
          return {};
        }
        lhs.value = (op == "==") ? cursor.builder->CreateFCmpOEQ(lhs_f, rhs_f)
                                 : cursor.builder->CreateFCmpONE(lhs_f, rhs_f);
      }
      lhs.type = ValueType::I1;
      continue;
    }
    if (rhs.type != lhs.type) {
      cursor.error = "equality operator '" + op + "' requires same-type operands";
      return {};
    }
    lhs.value = (op == "==") ? cursor.builder->CreateICmpEQ(lhs.value, rhs.value)
                             : cursor.builder->CreateICmpNE(lhs.value, rhs.value);
    lhs.type = ValueType::I1;
  }
  return lhs;
}

ExprValue evaluate_expression(const std::string& expr_text, llvm::IRBuilder<>& builder,
                                     std::unordered_map<std::string, VariableSlot>& variables,
                                     const std::unordered_map<std::string, int>& enum_variant_tags,
                                     const std::unordered_map<std::string, std::vector<std::string>>& struct_fields,
                                     const std::unordered_map<std::string, std::string>& struct_field_types,
                                     const std::unordered_map<std::string, StructInstance>& struct_instances,
                                     std::unordered_map<std::string, TupleInstance>& tuple_instances,
                                     std::unordered_map<std::string, ArrayInstance>& array_instances,
                                     const std::unordered_map<std::string, llvm::Function*>& functions,
                                     const std::unordered_map<std::string, ValueType>& function_returns,
                                     std::unordered_map<std::string, ClosureDef>* closures,
                                     llvm::Function* current_function,
                                     support::DiagnosticSink& diag) {
  std::string clean = trim(expr_text);
  while (starts_with(clean, "await ")) {
    clean = trim(clean.substr(6));
  }
  if (clean.empty()) {
    return ExprValue{builder.getInt32(0), ValueType::I32};
  }
  if (expr_text.empty()) {
    return ExprValue{builder.getInt32(0), ValueType::I32};
  }

  std::string len_base;
  if (parse_len_call(clean, len_base)) {
    auto array_it = array_instances.find(len_base);
    if (array_it != array_instances.end()) {
      return ExprValue{builder.getInt32(static_cast<int>(array_it->second.length)), ValueType::I32};
    }
    auto tuple_it = tuple_instances.find(len_base);
    if (tuple_it != tuple_instances.end()) {
      return ExprValue{builder.getInt32(static_cast<int>(tuple_it->second.element_types.size())), ValueType::I32};
    }
    diag.error("E2050", "len() expects tuple/array variable, got '" + len_base + "'");
    return {};
  }

  std::string tuple_base;
  std::size_t tuple_index = 0;
  if (parse_tuple_field_access(clean, tuple_base, tuple_index)) {
    auto tuple_it = tuple_instances.find(tuple_base);
    if (tuple_it != tuple_instances.end()) {
      if (tuple_index >= tuple_it->second.element_types.size()) {
        diag.error("E2051", "tuple index out of bounds: '" + clean + "'");
        return {};
      }
      llvm::Value* field_ptr = builder.CreateStructGEP(tuple_it->second.llvm_type, tuple_it->second.alloca,
                                                       static_cast<unsigned>(tuple_index), clean + ".ptr");
      const ValueType element_ty = tuple_it->second.element_types[tuple_index];
      llvm::Value* loaded = builder.CreateLoad(llvm_type_from_value_type(element_ty, builder), field_ptr);
      return ExprValue{loaded, element_ty};
    }
  }

  std::string array_base;
  std::string array_index_expr;
  if (parse_array_index_access(clean, array_base, array_index_expr)) {
    auto array_it = array_instances.find(array_base);
    if (array_it != array_instances.end()) {
      ExprValue index_value =
          evaluate_expression(array_index_expr, builder, variables, enum_variant_tags, struct_fields, struct_field_types,
                              struct_instances, tuple_instances, array_instances, functions, function_returns, closures,
                              current_function, diag);
      if (index_value.value == nullptr || index_value.type == ValueType::Invalid) {
        return {};
      }
      llvm::Value* index_i32 = nullptr;
      if (index_value.type == ValueType::I32) {
        index_i32 = index_value.value;
      } else if (index_value.type == ValueType::I64) {
        index_i32 = builder.CreateTrunc(index_value.value, builder.getInt32Ty());
      } else if (index_value.type == ValueType::I1) {
        index_i32 = builder.CreateZExt(index_value.value, builder.getInt32Ty());
      } else if (index_value.type == ValueType::F32 || index_value.type == ValueType::F64) {
        index_i32 = builder.CreateFPToSI(index_value.value, builder.getInt32Ty());
      }
      if (index_i32 == nullptr) {
        diag.error("E2052", "array index must be numeric");
        return {};
      }
      llvm::Value* non_negative = builder.CreateICmpSGE(index_i32, builder.getInt32(0));
      llvm::Value* under_bound = builder.CreateICmpSLT(index_i32, builder.getInt32(static_cast<int>(array_it->second.length)));
      llvm::Value* in_bounds = builder.CreateAnd(non_negative, under_bound);
      llvm::Function* fn = current_function == nullptr ? builder.GetInsertBlock()->getParent() : current_function;
      auto* ok_bb = llvm::BasicBlock::Create(builder.getContext(), "arr.idx.ok", fn);
      auto* panic_bb = llvm::BasicBlock::Create(builder.getContext(), "arr.idx.panic", fn);
      builder.CreateCondBr(in_bounds, ok_bb, panic_bb);
      builder.SetInsertPoint(panic_bb);
      llvm::Module* module = fn->getParent();
      llvm::FunctionCallee puts_fn = module->getOrInsertFunction("puts", llvm::FunctionType::get(builder.getInt32Ty(), pointer_type(builder), false));
      llvm::FunctionCallee exit_fn =
          module->getOrInsertFunction("exit", llvm::FunctionType::get(builder.getVoidTy(), {builder.getInt32Ty()}, false));
      llvm::Value* panic_msg = create_global_cstr_ptr(builder, "panic: array index out of bounds", "arr_oob");
      builder.CreateCall(puts_fn, {panic_msg});
      builder.CreateCall(exit_fn, {builder.getInt32(1)});
      builder.CreateUnreachable();
      builder.SetInsertPoint(ok_bb);
      llvm::Type* arr_alloc_ty = array_it->second.alloca->getAllocatedType();
      llvm::Type* elem_ty = llvm_type_from_value_type(array_it->second.element_type, builder);
      llvm::Value* elem_ptr = builder.CreateInBoundsGEP(arr_alloc_ty, array_it->second.alloca,
                                                        {builder.getInt32(0), index_i32}, array_base + ".idx.ptr");
      llvm::Value* loaded = builder.CreateLoad(elem_ty, elem_ptr);
      return ExprValue{loaded, array_it->second.element_type};
    }
  }

  std::string tok_error;
  ExprCursor cursor;
  cursor.tokens = tokenize_expression(clean, tok_error);
  cursor.builder = &builder;
  cursor.variables = &variables;
  cursor.enum_variant_tags = &enum_variant_tags;
  cursor.struct_fields = &struct_fields;
  cursor.struct_field_types = &struct_field_types;
  cursor.struct_instances = &struct_instances;
  cursor.tuple_instances = &tuple_instances;
  cursor.array_instances = &array_instances;
  cursor.functions = &functions;
  cursor.function_returns = &function_returns;
  cursor.closures = closures;
  cursor.current_function = current_function;
  if (!tok_error.empty()) {
    diag.error("E2007", "cannot tokenize backend expression '" + clean + "': " + tok_error);
    return {};
  }
  ExprValue value = parse_expression_equality(cursor);
  if (value.type == ValueType::Invalid || value.value == nullptr) {
    const std::string detail = cursor.error.empty() ? "unknown expression failure" : cursor.error;
    diag.error("E2008", "cannot lower backend expression '" + clean + "': " + detail);
    return {};
  }
  if (cur(cursor).kind != ExprTokKind::End) {
    diag.error("E2009", "unexpected trailing token in backend expression: '" + cur(cursor).text + "'");
    return {};
  }
  return value;
}

static std::string render_expression_ast(const syntax::AstExprPtr& expr) {
  if (!expr) {
    return "";
  }
  switch (expr->kind) {
    case syntax::AstExprKind::Raw:
    case syntax::AstExprKind::Atom:
      return expr->text;
    case syntax::AstExprKind::Unary:
      if (expr->children.empty()) {
        return expr->op;
      }
      return expr->op + render_expression_ast(expr->children[0]);
    case syntax::AstExprKind::Binary:
      if (expr->children.size() < 2) {
        return expr->text;
      }
      return render_expression_ast(expr->children[0]) + " " + expr->op + " " + render_expression_ast(expr->children[1]);
    case syntax::AstExprKind::Call: {
      if (expr->children.empty()) {
        return expr->text;
      }
      std::string out = render_expression_ast(expr->children[0]) + "(";
      for (std::size_t i = 1; i < expr->children.size(); ++i) {
        if (i > 1) {
          out += ", ";
        }
        out += render_expression_ast(expr->children[i]);
      }
      out += ")";
      return out;
    }
    case syntax::AstExprKind::Field:
      if (expr->children.empty()) {
        return expr->text;
      }
      return render_expression_ast(expr->children[0]) + "." + expr->op;
    case syntax::AstExprKind::Index:
      if (expr->children.size() < 2) {
        return expr->text;
      }
      return render_expression_ast(expr->children[0]) + "[" + render_expression_ast(expr->children[1]) + "]";
    case syntax::AstExprKind::Tuple: {
      std::string out = "(";
      for (std::size_t i = 0; i < expr->children.size(); ++i) {
        if (i > 0) {
          out += ", ";
        }
        out += render_expression_ast(expr->children[i]);
      }
      if (expr->children.size() == 1) {
        out += ",";
      }
      out += ")";
      return out;
    }
    case syntax::AstExprKind::Array: {
      std::string out = "[";
      for (std::size_t i = 0; i < expr->children.size(); ++i) {
        if (i > 0) {
          out += ", ";
        }
        out += render_expression_ast(expr->children[i]);
      }
      out += "]";
      return out;
    }
    default:
      return expr->text;
  }
}

static ExprValue evaluate_atom_direct(const std::string& atom_text, llvm::IRBuilder<>& builder,
                                      std::unordered_map<std::string, VariableSlot>& variables,
                                      const std::unordered_map<std::string, int>& enum_variant_tags,
                                      const std::unordered_map<std::string, std::vector<std::string>>& struct_fields,
                                      const std::unordered_map<std::string, std::string>& struct_field_types,
                                      const std::unordered_map<std::string, StructInstance>& struct_instances,
                                      std::unordered_map<std::string, TupleInstance>& tuple_instances,
                                      std::unordered_map<std::string, ArrayInstance>& array_instances,
                                      std::unordered_map<std::string, ClosureDef>* closures) {
  const std::string tok = trim(atom_text);
  if (tok.empty()) {
    return {};
  }
  if (tok == "true") {
    return ExprValue{builder.getInt1(true), ValueType::I1};
  }
  if (tok == "false") {
    return ExprValue{builder.getInt1(false), ValueType::I1};
  }
  if (is_integer_atom(tok)) {
    std::int64_t parsed = 0;
    try {
      parsed = std::stoll(tok);
    } catch (const std::exception&) {
      return {};
    }
    if (parsed >= std::numeric_limits<std::int32_t>::min() &&
        parsed <= std::numeric_limits<std::int32_t>::max()) {
      return ExprValue{builder.getInt32(static_cast<int32_t>(parsed)), ValueType::I32};
    }
    return ExprValue{builder.getInt64(parsed), ValueType::I64};
  }
  if (is_float_atom(tok)) {
    try {
      return ExprValue{llvm::ConstantFP::get(builder.getDoubleTy(), std::stod(tok)), ValueType::F64};
    } catch (const std::exception&) {
      return {};
    }
  }
  if (is_string_atom(tok)) {
    const std::string text = unescape_string_body(tok.substr(1, tok.size() - 2));
    return ExprValue{create_global_cstr_ptr(builder, text, "strlit"), ValueType::I8Ptr};
  }
  if (is_interpolated_literal(tok)) {
    std::string text = tok;
    if (text.size() >= 3 && text[0] == 'v' && text[1] == '"') {
      text = text.substr(2, text.size() - 3);
    } else if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
      text = text.substr(1, text.size() - 2);
    }
    text = unescape_string_body(text);
    return ExprValue{create_global_cstr_ptr(builder, text, "istrlit"), ValueType::I8Ptr};
  }
  std::string tuple_base;
  std::size_t tuple_field_index = 0;
  if (parse_tuple_field_access(tok, tuple_base, tuple_field_index)) {
    auto tuple_it = tuple_instances.find(tuple_base);
    if (tuple_it != tuple_instances.end() && tuple_it->second.alloca != nullptr && tuple_it->second.llvm_type != nullptr &&
        tuple_field_index < tuple_it->second.element_types.size()) {
      llvm::Value* field_ptr = builder.CreateStructGEP(tuple_it->second.llvm_type, tuple_it->second.alloca,
                                                       static_cast<unsigned>(tuple_field_index), tok + ".ptr");
      const ValueType field_ty = tuple_it->second.element_types[tuple_field_index];
      llvm::Value* loaded = builder.CreateLoad(llvm_type_from_value_type(field_ty, builder), field_ptr);
      return ExprValue{loaded, field_ty};
    }
  }
  std::string field_base;
  std::string field_name;
  if (split_dotted_name(tok, field_base, field_name)) {
    auto inst_it = struct_instances.find(field_base);
    if (inst_it != struct_instances.end() && inst_it->second.ptr != nullptr && inst_it->second.llvm_type != nullptr) {
      llvm::Value* base_ptr = inst_it->second.ptr;
      llvm::Type* expected_ptr_ty = inst_it->second.llvm_type->getPointerTo();
      if (base_ptr->getType() != expected_ptr_ty) {
        base_ptr = builder.CreateBitCast(base_ptr, expected_ptr_ty, field_base + ".typed.ptr");
      }
      const std::size_t field_index = field_index_for_struct(inst_it->second.struct_name, field_name, struct_fields);
      if (field_index != static_cast<std::size_t>(-1)) {
        llvm::Value* field_ptr = builder.CreateStructGEP(inst_it->second.llvm_type, base_ptr,
                                                         static_cast<unsigned>(field_index),
                                                         field_base + "." + field_name + ".ptr");
        const ValueType field_ty = field_value_type_for_struct(inst_it->second.struct_name, field_name, struct_field_types);
        llvm::Value* loaded = builder.CreateLoad(llvm_type_from_value_type(field_ty, builder), field_ptr);
        return ExprValue{loaded, field_ty};
      }
    }
  }
  auto var_it = variables.find(tok);
  if (var_it != variables.end() && var_it->second.alloca != nullptr) {
    llvm::Value* loaded = builder.CreateLoad(var_it->second.alloca->getAllocatedType(), var_it->second.alloca);
    return ExprValue{loaded, var_it->second.type};
  }
  auto variant_it = enum_variant_tags.find(tok);
  if (variant_it != enum_variant_tags.end()) {
    return ExprValue{builder.getInt32(encode_enum_with_payload(variant_it->second, 0)), ValueType::I32};
  }
  if (const std::optional<int> builtin = builtin_variant_tag(tok); builtin.has_value()) {
    return ExprValue{builder.getInt32(encode_enum_with_payload(*builtin, 0)), ValueType::I32};
  }
  if (closures != nullptr) {
    auto closure_it = closures->find(tok);
    if (closure_it != closures->end()) {
      return ExprValue{builder.getInt32(0), ValueType::I32};
    }
  }
  return {};
}

ExprValue evaluate_expression(const syntax::AstExprPtr& expr_ast, const std::string& fallback_expr,
                              llvm::IRBuilder<>& builder, std::unordered_map<std::string, VariableSlot>& variables,
                              const std::unordered_map<std::string, int>& enum_variant_tags,
                              const std::unordered_map<std::string, std::vector<std::string>>& struct_fields,
                              const std::unordered_map<std::string, std::string>& struct_field_types,
                              const std::unordered_map<std::string, StructInstance>& struct_instances,
                              std::unordered_map<std::string, TupleInstance>& tuple_instances,
                              std::unordered_map<std::string, ArrayInstance>& array_instances,
                              const std::unordered_map<std::string, llvm::Function*>& functions,
                              const std::unordered_map<std::string, ValueType>& function_returns,
                              std::unordered_map<std::string, ClosureDef>* closures,
                              llvm::Function* current_function,
                              support::DiagnosticSink& diag) {
  if (!expr_ast) {
    return evaluate_expression(fallback_expr, builder, variables, enum_variant_tags, struct_fields, struct_field_types,
                               struct_instances, tuple_instances, array_instances, functions, function_returns, closures,
                               current_function, diag);
  }

  const auto eval_child = [&](const syntax::AstExprPtr& child) {
    return evaluate_expression(child, render_expression_ast(child), builder, variables, enum_variant_tags, struct_fields,
                               struct_field_types, struct_instances, tuple_instances, array_instances, functions,
                               function_returns, closures, current_function, diag);
  };

  switch (expr_ast->kind) {
    case syntax::AstExprKind::Raw: {
      const std::string text = !trim(expr_ast->text).empty() ? expr_ast->text : fallback_expr;
      return evaluate_expression(text, builder, variables, enum_variant_tags, struct_fields, struct_field_types,
                                 struct_instances, tuple_instances, array_instances, functions, function_returns, closures,
                                 current_function, diag);
    }
    case syntax::AstExprKind::Atom: {
      ExprValue direct = evaluate_atom_direct(expr_ast->text, builder, variables, enum_variant_tags, struct_fields,
                                              struct_field_types, struct_instances, tuple_instances, array_instances, closures);
      if (direct.value != nullptr && direct.type != ValueType::Invalid) {
        return direct;
      }
      const std::string fallback = !trim(fallback_expr).empty() ? fallback_expr : expr_ast->text;
      return evaluate_expression(fallback, builder, variables, enum_variant_tags, struct_fields, struct_field_types,
                                 struct_instances, tuple_instances, array_instances, functions, function_returns, closures,
                                 current_function, diag);
    }
    case syntax::AstExprKind::Unary: {
      if (expr_ast->children.empty()) {
        break;
      }
      ExprValue rhs = eval_child(expr_ast->children[0]);
      if (rhs.value == nullptr || rhs.type == ValueType::Invalid) {
        return {};
      }
      const std::string op = expr_ast->op;
      if (op == "await") {
        return rhs;
      }
      if (op == "!") {
        llvm::Value* as_i1 = to_i1(rhs, builder);
        if (as_i1 == nullptr) {
          return {};
        }
        return ExprValue{builder.CreateNot(as_i1), ValueType::I1};
      }
      if (op == "-") {
        if (rhs.type == ValueType::I32) {
          return ExprValue{builder.CreateNeg(rhs.value), ValueType::I32};
        }
        if (rhs.type == ValueType::I64) {
          return ExprValue{builder.CreateNeg(rhs.value), ValueType::I64};
        }
        if (rhs.type == ValueType::F32 || rhs.type == ValueType::F64) {
          llvm::Value* as_f = to_float_value(rhs, rhs.type, builder);
          if (as_f == nullptr) {
            return {};
          }
          return ExprValue{builder.CreateFNeg(as_f), rhs.type};
        }
      }
      break;
    }
    case syntax::AstExprKind::Binary: {
      if (expr_ast->children.size() < 2) {
        break;
      }
      ExprValue lhs = eval_child(expr_ast->children[0]);
      ExprValue rhs = eval_child(expr_ast->children[1]);
      if (lhs.value == nullptr || rhs.value == nullptr || lhs.type == ValueType::Invalid || rhs.type == ValueType::Invalid) {
        return {};
      }
      const std::string op = expr_ast->op;
      if (op == "&&" || op == "||") {
        llvm::Value* l = to_i1(lhs, builder);
        llvm::Value* r = to_i1(rhs, builder);
        if (l == nullptr || r == nullptr) {
          return {};
        }
        return ExprValue{op == "&&" ? builder.CreateAnd(l, r) : builder.CreateOr(l, r), ValueType::I1};
      }
      if (op == "==" || op == "!=") {
        if (lhs.type == rhs.type) {
          llvm::Value* cmp = nullptr;
          if (is_float_type(lhs.type)) {
            cmp = op == "==" ? builder.CreateFCmpOEQ(lhs.value, rhs.value) : builder.CreateFCmpONE(lhs.value, rhs.value);
          } else {
            cmp = op == "==" ? builder.CreateICmpEQ(lhs.value, rhs.value) : builder.CreateICmpNE(lhs.value, rhs.value);
          }
          return ExprValue{cmp, ValueType::I1};
        }
      }
      if (op == "<" || op == "<=" || op == ">" || op == ">=") {
        if (is_float_type(lhs.type) || is_float_type(rhs.type)) {
          const ValueType target = promoted_float_type(lhs.type, rhs.type);
          llvm::Value* l = to_float_value(lhs, target, builder);
          llvm::Value* r = to_float_value(rhs, target, builder);
          if (l == nullptr || r == nullptr) {
            return {};
          }
          llvm::Value* cmp = nullptr;
          if (op == "<") cmp = builder.CreateFCmpOLT(l, r);
          if (op == "<=") cmp = builder.CreateFCmpOLE(l, r);
          if (op == ">") cmp = builder.CreateFCmpOGT(l, r);
          if (op == ">=") cmp = builder.CreateFCmpOGE(l, r);
          return ExprValue{cmp, ValueType::I1};
        }
        if (is_integer_numeric_type(lhs.type) && is_integer_numeric_type(rhs.type)) {
          const ValueType target = promoted_integer_type(lhs.type, rhs.type);
          llvm::Value* l = to_integer_numeric_value(lhs, target, builder);
          llvm::Value* r = to_integer_numeric_value(rhs, target, builder);
          if (l == nullptr || r == nullptr) {
            return {};
          }
          llvm::Value* cmp = nullptr;
          if (op == "<") cmp = builder.CreateICmpSLT(l, r);
          if (op == "<=") cmp = builder.CreateICmpSLE(l, r);
          if (op == ">") cmp = builder.CreateICmpSGT(l, r);
          if (op == ">=") cmp = builder.CreateICmpSGE(l, r);
          return ExprValue{cmp, ValueType::I1};
        }
      }
      if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") {
        if (is_float_type(lhs.type) || is_float_type(rhs.type)) {
          const ValueType target = promoted_float_type(lhs.type, rhs.type);
          llvm::Value* l = to_float_value(lhs, target, builder);
          llvm::Value* r = to_float_value(rhs, target, builder);
          if (l == nullptr || r == nullptr || op == "%") {
            break;
          }
          llvm::Value* out = nullptr;
          if (op == "+") out = builder.CreateFAdd(l, r);
          if (op == "-") out = builder.CreateFSub(l, r);
          if (op == "*") out = builder.CreateFMul(l, r);
          if (op == "/") out = builder.CreateFDiv(l, r);
          if (out != nullptr) {
            return ExprValue{out, target};
          }
        }
        if (is_integer_numeric_type(lhs.type) && is_integer_numeric_type(rhs.type)) {
          const ValueType target = promoted_integer_type(lhs.type, rhs.type);
          llvm::Value* l = to_integer_numeric_value(lhs, target, builder);
          llvm::Value* r = to_integer_numeric_value(rhs, target, builder);
          if (l == nullptr || r == nullptr) {
            return {};
          }
          llvm::Value* out = nullptr;
          if (op == "+") out = builder.CreateAdd(l, r);
          if (op == "-") out = builder.CreateSub(l, r);
          if (op == "*") out = builder.CreateMul(l, r);
          if (op == "/") out = builder.CreateSDiv(l, r);
          if (op == "%") out = builder.CreateSRem(l, r);
          if (out != nullptr) {
            return ExprValue{out, target};
          }
        }
      }
      break;
    }
    default:
      break;
  }

  const std::string rendered = render_expression_ast(expr_ast);
  const std::string fallback = rendered.empty() ? fallback_expr : rendered;
  return evaluate_expression(fallback, builder, variables, enum_variant_tags, struct_fields, struct_field_types,
                             struct_instances, tuple_instances, array_instances, functions, function_returns, closures,
                             current_function, diag);
}

static bool parse_interpolated_chunks(const std::string& literal, std::vector<InterpChunk>& out_chunks,
                                      std::string& error) {
  out_chunks.clear();
  if (!is_interpolated_literal(literal)) {
    error = "not an interpolated literal";
    return false;
  }
  std::string clean = trim(literal);
  if (clean.size() >= 3 && clean[0] == 'v' && clean[1] == '"') {
    clean = clean.substr(1);
  }
  if (clean.size() < 2 || clean.front() != '"' || clean.back() != '"') {
    error = "invalid interpolated literal quotes";
    return false;
  }
  const std::string body = clean.substr(1, clean.size() - 2);
  std::string segment;
  for (std::size_t i = 0; i < body.size();) {
    if (body[i] == '{') {
      if (!segment.empty()) {
        out_chunks.push_back(InterpChunk{false, unescape_string_body(segment)});
        segment.clear();
      }
      const std::size_t close = body.find('}', i + 1);
      if (close == std::string::npos) {
        error = "missing '}' in interpolated literal";
        return false;
      }
      const std::string expr = trim(body.substr(i + 1, close - i - 1));
      if (expr.empty()) {
        error = "empty interpolation slot";
        return false;
      }
      out_chunks.push_back(InterpChunk{true, expr});
      i = close + 1;
      continue;
    }
    segment.push_back(body[i]);
    ++i;
  }
  if (!segment.empty()) {
    out_chunks.push_back(InterpChunk{false, unescape_string_body(segment)});
  }
  return true;
}

bool emit_expression_statement(const std::string& line, bool has_expression, const std::string& expression,
                                      const syntax::AstExprPtr& expression_ast,
                                      llvm::IRBuilder<>& builder, llvm::Function* fn, llvm::FunctionCallee printf_fn,
                                      llvm::Value* printf_i32_fmt, llvm::Value* printf_f64_fmt,
                                      llvm::Value* printf_i64_fmt, llvm::Value* printf_str_fmt,
                                      llvm::Value* printf_i32_raw_fmt, llvm::Value* printf_i64_raw_fmt,
                                      llvm::Value* printf_f64_raw_fmt, llvm::Value* printf_raw_str_fmt,
                                      llvm::Value* printf_newline_fmt,
                                      std::unordered_map<std::string, VariableSlot>& variables,
                                      const std::unordered_map<std::string, int>& enum_variant_tags,
                                      const std::unordered_map<std::string, std::vector<std::string>>& struct_fields,
                                      const std::unordered_map<std::string, std::string>& struct_field_types,
                                      const std::unordered_map<std::string, StructInstance>& struct_instances,
                                      std::unordered_map<std::string, TupleInstance>& tuple_instances,
                                      std::unordered_map<std::string, ArrayInstance>& array_instances,
                                      const std::unordered_map<std::string, llvm::Function*>& functions,
                                      const std::unordered_map<std::string, ValueType>& function_returns,
                                      const std::unordered_map<std::string, bool>& function_async_flags,
                                      std::unordered_map<std::string, ClosureDef>& closures,
                                      bool await_expression, AsyncLoweringContext* async_ctx,
                                      support::DiagnosticSink& diag) {
  std::string effective_line = line;
  std::string effective_expr = expression;
  if (has_expression && starts_with(effective_expr, "print(") && ends_with(effective_expr, ")")) {
    effective_line = effective_expr;
    effective_expr = trim(effective_expr.substr(6, effective_expr.size() - 7));
  }

  if (starts_with(effective_line, "print(") && ends_with(effective_line, ")")) {
    const std::string inner = has_expression ? effective_expr : trim(effective_line.substr(6, effective_line.size() - 7));
    if (is_interpolated_literal(inner)) {
      std::vector<InterpChunk> chunks;
      std::string interp_error;
      if (!parse_interpolated_chunks(inner, chunks, interp_error)) {
        diag.error("E2030", "invalid interpolated literal: " + interp_error);
        return false;
      }
      for (const auto& chunk : chunks) {
        if (!chunk.placeholder) {
          llvm::Value* str = create_global_cstr_ptr(builder, chunk.text, "interp_seg");
          builder.CreateCall(printf_fn, {printf_raw_str_fmt, str});
          continue;
        }
        ExprValue value = evaluate_expression(chunk.text, builder, variables, enum_variant_tags, struct_fields,
                                              struct_field_types, struct_instances, tuple_instances, array_instances,
                                              functions, function_returns,
                                              &closures, fn, diag);
        if (value.value == nullptr || value.type == ValueType::Invalid) {
          return false;
        }
        if (value.type == ValueType::I8Ptr) {
          builder.CreateCall(printf_fn, {printf_raw_str_fmt, value.value});
        } else if (value.type == ValueType::I64) {
          llvm::Value* as_i64 = to_i64(value, builder);
          if (as_i64 == nullptr) {
            diag.error("E2032", "interpolation expects i64/i32/bool/string-compatible expressions");
            return false;
          }
          builder.CreateCall(printf_fn, {printf_i64_raw_fmt, as_i64});
        } else if (value.type == ValueType::F32 || value.type == ValueType::F64) {
          llvm::Value* as_f64 = to_f64_value(value, builder);
          if (as_f64 == nullptr) {
            diag.error("E2031", "interpolation failed float conversion");
            return false;
          }
          builder.CreateCall(printf_fn, {printf_f64_raw_fmt, as_f64});
        } else {
          llvm::Value* as_i32 = to_i32(value, builder);
          if (as_i32 == nullptr) {
            diag.error("E2032", "interpolation expects i32/bool/string-compatible expressions");
            return false;
          }
          builder.CreateCall(printf_fn, {printf_i32_raw_fmt, as_i32});
        }
      }
      builder.CreateCall(printf_fn, {printf_newline_fmt});
      if (await_expression &&
          !emit_await_semantics_for_expression(inner, function_async_flags, async_ctx, builder, fn, diag)) {
        return false;
      }
      return true;
    }

    ExprValue value = evaluate_expression(nullptr, inner, builder, variables, enum_variant_tags, struct_fields,
                                          struct_field_types, struct_instances, tuple_instances, array_instances,
                                          functions, function_returns, &closures, fn, diag);
    if (value.value == nullptr || value.type == ValueType::Invalid) {
      return false;
    }
    if (value.type == ValueType::I8Ptr) {
      builder.CreateCall(printf_fn, {printf_str_fmt, value.value});
    } else if (value.type == ValueType::I64) {
      llvm::Value* as_i64 = to_i64(value, builder);
      if (as_i64 == nullptr) {
        diag.error("E2012", "print() failed i64 conversion");
        return false;
      }
      builder.CreateCall(printf_fn, {printf_i64_fmt, as_i64});
    } else if (value.type == ValueType::F32 || value.type == ValueType::F64) {
      llvm::Value* as_f64 = to_f64_value(value, builder);
      if (as_f64 == nullptr) {
        diag.error("E2012", "print() failed float conversion");
        return false;
      }
      builder.CreateCall(printf_fn, {printf_f64_fmt, as_f64});
    } else {
      llvm::Value* as_i32 = to_i32(value, builder);
      if (as_i32 == nullptr) {
        diag.error("E2012", "print() expects i32/bool/string-compatible expression");
        return false;
      }
      builder.CreateCall(printf_fn, {printf_i32_fmt, as_i32});
    }
    if (await_expression &&
        !emit_await_semantics_for_expression(inner, function_async_flags, async_ctx, builder, fn, diag)) {
      return false;
    }
    return true;
  }

    if (has_expression) {
      ExprValue value =
        evaluate_expression(expression_ast, effective_expr, builder, variables, enum_variant_tags, struct_fields,
                            struct_field_types, struct_instances, tuple_instances, array_instances, functions,
                            function_returns, &closures, fn, diag);
    if (value.value == nullptr || value.type == ValueType::Invalid) {
      return false;
    }
    if (await_expression &&
        !emit_await_semantics_for_expression(effective_expr, function_async_flags, async_ctx, builder, fn, diag)) {
      return false;
    }
  }
  return true;
}

static std::string strip_await_prefixes(const std::string& expression) {
  std::string clean = trim(expression);
  while (starts_with(clean, "await ")) {
    clean = trim(clean.substr(6));
  }
  return clean;
}

static std::string parse_await_call_target(const std::string& expression) {
  const std::string clean = strip_await_prefixes(expression);
  const std::size_t lparen = clean.find('(');
  const std::size_t rparen = clean.rfind(')');
  if (lparen == std::string::npos || rparen == std::string::npos || rparen <= lparen) {
    return "";
  }
  std::string name = trim(clean.substr(0, lparen));
  if (name.empty()) {
    return "";
  }
  while (!name.empty() && name.back() == '?') {
    name.pop_back();
    name = trim(name);
  }
  if (name.empty() || !is_ident_start(name[0])) {
    return "";
  }
  for (std::size_t i = 1; i < name.size(); ++i) {
    if (name[i] == '.') {
      continue;
    }
    if (!is_ident_body(name[i])) {
      return "";
    }
  }
  return name;
}

bool emit_await_semantics_for_expression(const std::string& expression,
                                                const std::unordered_map<std::string, bool>& function_async_flags,
                                                AsyncLoweringContext* async_ctx, llvm::IRBuilder<>& builder,
                                                llvm::Function* fn, support::DiagnosticSink& diag) {
  const std::string call_target = parse_await_call_target(expression);
  if (!call_target.empty()) {
    auto async_it = function_async_flags.find(call_target);
    if (async_it != function_async_flags.end() && async_it->second) {
      emit_await_spawn_wrapper(builder, fn, async_ctx == nullptr ? nullptr : async_ctx->coro_handle);
    }
  }
  return emit_async_suspend_point(async_ctx, builder, fn, diag);
}


}  // namespace thagc::codegen
