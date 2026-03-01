#include "thagc/backend/llvm_emitter.hpp"

#include <cctype>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>
#include <llvm/Transforms/Coroutines/CoroCleanup.h>
#include <llvm/Transforms/Coroutines/CoroEarly.h>
#include <llvm/Transforms/Coroutines/CoroElide.h>
#include <llvm/Transforms/Coroutines/CoroSplit.h>

namespace thagc::codegen {

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

enum class ValueType {
  I32,
  I64,
  F32,
  F64,
  I1,
  I8Ptr,
  Void,
  Invalid,
};

struct ExprValue {
  llvm::Value* value = nullptr;
  ValueType type = ValueType::Invalid;
};

struct VariableSlot {
  llvm::AllocaInst* alloca = nullptr;
  ValueType type = ValueType::Invalid;
  enum class Ownership {
    None,
    Rc,
    Arc,
  } ownership = Ownership::None;
};

struct StructInstance {
  llvm::Value* ptr = nullptr;
  llvm::StructType* llvm_type = nullptr;
  std::string struct_name;
};

struct ClosureDef {
  std::vector<std::string> params;
  std::string body_expr;
  bool block_body = false;
  std::unordered_map<std::string, llvm::Value*> captured_i32_values;
};

struct TupleInstance {
  llvm::AllocaInst* alloca = nullptr;
  llvm::StructType* llvm_type = nullptr;
  std::vector<ValueType> element_types;
};

struct ArrayInstance {
  llvm::AllocaInst* alloca = nullptr;
  ValueType element_type = ValueType::Invalid;
  std::size_t length = 0;
};

struct AsyncLoweringContext {
  bool enabled = false;
  ValueType expected_return_type = ValueType::Invalid;
  llvm::Value* coro_id = nullptr;
  llvm::Value* coro_handle = nullptr;
  llvm::AllocaInst* return_slot = nullptr;
  llvm::BasicBlock* finalize_block = nullptr;
  llvm::FunctionCallee coro_save;
  llvm::FunctionCallee coro_suspend;
  llvm::FunctionCallee coro_end;
  llvm::FunctionCallee coro_free;
  llvm::FunctionCallee free_fn;
};

static constexpr int kEnumPayloadShift = 20;
static constexpr int kEnumPayloadMask = (1 << kEnumPayloadShift) - 1;

struct ExprCursor {
  std::vector<ExprTok> tokens;
  std::size_t index = 0;
  std::string error;
  llvm::IRBuilder<>* builder = nullptr;
  std::unordered_map<std::string, VariableSlot>* variables = nullptr;
  const std::unordered_map<std::string, int>* enum_variant_tags = nullptr;
  const std::unordered_map<std::string, llvm::Function*>* functions = nullptr;
  const std::unordered_map<std::string, ValueType>* function_returns = nullptr;
  const std::unordered_map<std::string, std::vector<std::string>>* struct_fields = nullptr;
  const std::unordered_map<std::string, std::string>* struct_field_types = nullptr;
  const std::unordered_map<std::string, StructInstance>* struct_instances = nullptr;
  std::unordered_map<std::string, TupleInstance>* tuple_instances = nullptr;
  std::unordered_map<std::string, ArrayInstance>* array_instances = nullptr;
  std::unordered_map<std::string, ClosureDef>* closures = nullptr;
  llvm::Function* current_function = nullptr;
};

static std::string trim(const std::string& text) {
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

static std::string unescape_string_body(const std::string& body) {
  auto hex_value = [](char ch) -> int {
    if (ch >= '0' && ch <= '9') {
      return static_cast<int>(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
      return 10 + static_cast<int>(ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
      return 10 + static_cast<int>(ch - 'A');
    }
    return -1;
  };
  auto is_octal = [](char ch) -> bool { return ch >= '0' && ch <= '7'; };

  std::string out;
  out.reserve(body.size());
  for (std::size_t i = 0; i < body.size(); ++i) {
    const char ch = body[i];
    if (ch != '\\' || i + 1 >= body.size()) {
      out.push_back(ch);
      continue;
    }
    const char esc = body[++i];
    if (esc == 'n') {
      out.push_back('\n');
      continue;
    }
    if (esc == 'r') {
      out.push_back('\r');
      continue;
    }
    if (esc == 't') {
      out.push_back('\t');
      continue;
    }
    if (esc == '\\') {
      out.push_back('\\');
      continue;
    }
    if (esc == '"') {
      out.push_back('"');
      continue;
    }
    if (esc == 'e' || esc == 'E') {
      out.push_back(static_cast<char>(27));
      continue;
    }
    if (esc == 'x' || esc == 'X') {
      if (i + 2 < body.size()) {
        const int hi = hex_value(body[i + 1]);
        const int lo = hex_value(body[i + 2]);
        if (hi >= 0 && lo >= 0) {
          out.push_back(static_cast<char>((hi << 4) | lo));
          i += 2;
          continue;
        }
      }
      out.push_back('x');
      continue;
    }
    if (is_octal(esc)) {
      int value = static_cast<int>(esc - '0');
      int consumed = 0;
      while (consumed < 2 && i + 1 < body.size() && is_octal(body[i + 1])) {
        value = (value * 8) + static_cast<int>(body[i + 1] - '0');
        ++i;
        ++consumed;
      }
      out.push_back(static_cast<char>(value & 0xFF));
      continue;
    }
    out.push_back(esc);
  }
  return out;
}

static bool starts_with(const std::string& text, std::string_view prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

static bool ends_with(const std::string& text, std::string_view suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static bool is_ident_start(char ch) {
  return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}

static bool is_ident_body(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

static std::string parse_let_name(const std::string& line) {
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
  out = trim(out);
  return out;
}

static bool split_dotted_name(const std::string& text, std::string& base, std::string& member) {
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

static std::size_t field_index_for_struct(const std::string& struct_name, const std::string& field_name,
                                          const std::unordered_map<std::string, std::vector<std::string>>& struct_fields) {
  auto fields_it = struct_fields.find(struct_name);
  if (fields_it == struct_fields.end()) {
    return static_cast<std::size_t>(-1);
  }
  const auto& fields = fields_it->second;
  auto found = std::find(fields.begin(), fields.end(), field_name);
  if (found == fields.end()) {
    return static_cast<std::size_t>(-1);
  }
  return static_cast<std::size_t>(std::distance(fields.begin(), found));
}

static ValueType field_value_type_for_struct(const std::string& struct_name, const std::string& field_name,
                                             const std::unordered_map<std::string, std::string>& struct_field_types);

struct ParsedConstructorCall {
  bool ok = false;
  std::string name;
  std::vector<std::string> args;
  std::string error;
};

static ParsedConstructorCall parse_constructor_call(const std::string& expr) {
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

static bool split_top_level_items(const std::string& text, std::vector<std::string>& out) {
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

static std::string parse_simple_identifier_expr(const std::string& expr) {
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

static bool parse_tuple_literal_expr(const std::string& expr, std::vector<std::string>& elements) {
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

static bool parse_array_literal_expr(const std::string& expr, std::vector<std::string>& elements) {
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

static bool parse_tuple_destructure_let(const std::string& line, std::vector<std::string>& names, std::string& expr) {
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

static llvm::AllocaInst* create_entry_alloca(llvm::Function* fn, llvm::Type* type, const std::string& name) {
  llvm::IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().begin());
  return tmp.CreateAlloca(type, nullptr, name);
}

static llvm::Value* create_global_cstr_ptr(llvm::IRBuilder<>& builder, const std::string& value,
                                           const std::string& name_hint) {
  llvm::GlobalVariable* global = builder.CreateGlobalString(value, name_hint);
  llvm::Value* zero = builder.getInt32(0);
  return builder.CreateInBoundsGEP(global->getValueType(), global, {zero, zero}, name_hint + ".ptr");
}

static bool is_interpolated_literal(const std::string& text) {
  const std::string clean = trim(text);
  if (clean.size() >= 3 && clean[0] == 'v' && clean[1] == '"' && clean.back() == '"') {
    return true;
  }
  return clean.size() >= 2 && clean.front() == '"' && clean.back() == '"' && clean.find('{') != std::string::npos &&
         clean.find('}') != std::string::npos;
}

static bool parse_closure_literal(const std::string& text, std::vector<std::string>& params, std::string& body_expr,
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

static std::vector<std::string> collect_closure_captures(const std::vector<std::string>& params, const std::string& body) {
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

static bool parse_try_operator_expr(const std::string& text, std::string& inner) {
  std::string clean = trim(text);
  if (clean.empty() || clean.back() != '?') {
    return false;
  }
  inner = trim(clean.substr(0, clean.size() - 1));
  return !inner.empty();
}

static bool parse_tuple_field_access(const std::string& text, std::string& base, std::size_t& index) {
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

static bool parse_array_index_access(const std::string& text, std::string& base, std::string& index_expr) {
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

static bool parse_len_call(const std::string& text, std::string& base) {
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

static std::optional<int> builtin_variant_tag(const std::string& name) {
  if (name == "None") return 0;
  if (name == "Some") return 1;
  if (name == "Ok") return 2;
  if (name == "Err") return 3;
  return std::nullopt;
}

static int encode_enum_with_payload(int tag, int payload) {
  return ((tag + 1) << kEnumPayloadShift) | (payload & kEnumPayloadMask);
}

static llvm::Value* extract_enum_tag_value(llvm::Value* encoded, llvm::IRBuilder<>& builder) {
  llvm::Value* shifted = builder.CreateAShr(encoded, builder.getInt32(kEnumPayloadShift));
  return builder.CreateSub(shifted, builder.getInt32(1));
}

static llvm::Value* extract_enum_payload_value(llvm::Value* encoded, llvm::IRBuilder<>& builder) {
  const int payload_shift = 32 - kEnumPayloadShift;
  llvm::Value* shl = builder.CreateShl(encoded, builder.getInt32(payload_shift));
  return builder.CreateAShr(shl, builder.getInt32(payload_shift));
}

static bool is_integer_atom(const std::string& text) {
  if (text.empty()) {
    return false;
  }
  for (char ch : text) {
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return false;
    }
  }
  return true;
}

static bool is_float_atom(const std::string& text) {
  if (text.empty()) {
    return false;
  }
  bool seen_dot = false;
  bool seen_digit = false;
  for (char ch : text) {
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

static bool is_string_atom(const std::string& text) {
  return text.size() >= 2 && text.front() == '"' && text.back() == '"';
}

static std::vector<ExprTok> tokenize_expression(const std::string& text, std::string& error) {
  std::vector<ExprTok> out;
  std::vector<std::string> closure_params;
  std::string closure_body;
  bool closure_block = false;
  if (parse_closure_literal(text, closure_params, closure_body, closure_block)) {
    out.push_back(ExprTok{ExprTokKind::Atom, trim(text)});
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
      const std::size_t start = i++;
      while (i < text.size() && text[i] != '"') {
        if (text[i] == '\\' && i + 1 < text.size()) {
          i += 2;
          continue;
        }
        ++i;
      }
      if (i >= text.size() || text[i] != '"') {
        error = "unterminated string literal in backend expression";
        return {};
      }
      ++i;
      out.push_back(ExprTok{ExprTokKind::Atom, text.substr(start, i - start)});
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(ch))) {
      const std::size_t start = i;
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
        error = "invalid float literal";
        return {};
      }
      out.push_back(ExprTok{ExprTokKind::Atom, text.substr(start, i - start)});
      continue;
    }
    if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
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
          error = "unterminated interpolated string literal in backend expression";
          return {};
        }
        ++i;
        out.push_back(ExprTok{ExprTokKind::Atom, text.substr(start, i - start)});
        continue;
      }
      const std::size_t start = i;
      while (i < text.size()) {
        if (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_') {
          ++i;
          continue;
        }
        if (text[i] == '.' && i + 1 < text.size() &&
            (std::isalpha(static_cast<unsigned char>(text[i + 1])) || text[i + 1] == '_' ||
             std::isdigit(static_cast<unsigned char>(text[i + 1])))) {
          ++i;
          continue;
        }
        break;
      }
      out.push_back(ExprTok{ExprTokKind::Atom, text.substr(start, i - start)});
      continue;
    }
    error = std::string("invalid expression token: '") + ch + "'";
    return {};
  }
  out.push_back(ExprTok{ExprTokKind::End, ""});
  return out;
}

static const ExprTok& cur(const ExprCursor& cursor) {
  if (cursor.index >= cursor.tokens.size()) {
    static const ExprTok end{ExprTokKind::End, ""};
    return end;
  }
  return cursor.tokens[cursor.index];
}

static llvm::Type* llvm_type_from_value_type(ValueType type, llvm::IRBuilder<>& builder);
static ExprValue parse_expression_equality(ExprCursor& cursor);
static llvm::Value* to_i64(ExprValue value, llvm::IRBuilder<>& builder);
static llvm::Value* emit_ref_new_call(VariableSlot::Ownership ownership, llvm::Value* value_i64, llvm::Function* fn,
                                      llvm::IRBuilder<>& builder);
static llvm::Value* emit_ref_clone_call(VariableSlot::Ownership ownership, llvm::Value* handle, llvm::Function* fn,
                                        llvm::IRBuilder<>& builder);
static void emit_ref_drop_call(VariableSlot::Ownership ownership, llvm::Value* handle, llvm::Function* fn,
                               llvm::IRBuilder<>& builder);

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

static bool is_numeric_type(ValueType type) {
  return type == ValueType::I32 || type == ValueType::I64 || type == ValueType::F32 || type == ValueType::F64;
}

static bool is_float_type(ValueType type) {
  return type == ValueType::F32 || type == ValueType::F64;
}

static bool is_integer_numeric_type(ValueType type) {
  return type == ValueType::I32 || type == ValueType::I64;
}

static ValueType promoted_integer_type(ValueType lhs, ValueType rhs) {
  if (lhs == ValueType::I64 || rhs == ValueType::I64) {
    return ValueType::I64;
  }
  return ValueType::I32;
}

static llvm::Value* to_integer_numeric_value(ExprValue value, ValueType target, llvm::IRBuilder<>& builder) {
  if (value.value == nullptr) {
    return nullptr;
  }
  if (target != ValueType::I32 && target != ValueType::I64) {
    return nullptr;
  }
  if (target == ValueType::I32) {
    if (value.type == ValueType::I32) {
      return value.value;
    }
    if (value.type == ValueType::I64) {
      return builder.CreateTrunc(value.value, builder.getInt32Ty());
    }
    if (value.type == ValueType::I1) {
      return builder.CreateZExt(value.value, builder.getInt32Ty());
    }
    if (value.type == ValueType::F32 || value.type == ValueType::F64) {
      return builder.CreateFPToSI(value.value, builder.getInt32Ty());
    }
    return nullptr;
  }
  if (value.type == ValueType::I64) {
    return value.value;
  }
  if (value.type == ValueType::I32) {
    return builder.CreateSExt(value.value, builder.getInt64Ty());
  }
  if (value.type == ValueType::I1) {
    return builder.CreateZExt(value.value, builder.getInt64Ty());
  }
  if (value.type == ValueType::F32 || value.type == ValueType::F64) {
    return builder.CreateFPToSI(value.value, builder.getInt64Ty());
  }
  return nullptr;
}

static ValueType promoted_float_type(ValueType lhs, ValueType rhs) {
  if (lhs == ValueType::F64 || rhs == ValueType::F64) {
    return ValueType::F64;
  }
  if (lhs == ValueType::F32 || rhs == ValueType::F32) {
    return ValueType::F32;
  }
  return ValueType::F64;
}

static llvm::Value* to_float_value(ExprValue value, ValueType target, llvm::IRBuilder<>& builder) {
  if (value.value == nullptr) {
    return nullptr;
  }
  if (target != ValueType::F32 && target != ValueType::F64) {
    return nullptr;
  }
  if (target == ValueType::F64) {
    if (value.type == ValueType::F64) {
      return value.value;
    }
    if (value.type == ValueType::F32) {
      return builder.CreateFPExt(value.value, builder.getDoubleTy());
    }
    if (value.type == ValueType::I32) {
      return builder.CreateSIToFP(value.value, builder.getDoubleTy());
    }
    if (value.type == ValueType::I64) {
      return builder.CreateSIToFP(value.value, builder.getDoubleTy());
    }
    if (value.type == ValueType::I1) {
      llvm::Value* as_i32 = builder.CreateZExt(value.value, builder.getInt32Ty());
      return builder.CreateSIToFP(as_i32, builder.getDoubleTy());
    }
    return nullptr;
  }
  if (value.type == ValueType::F32) {
    return value.value;
  }
  if (value.type == ValueType::F64) {
    return builder.CreateFPTrunc(value.value, builder.getFloatTy());
  }
  if (value.type == ValueType::I32) {
    return builder.CreateSIToFP(value.value, builder.getFloatTy());
  }
  if (value.type == ValueType::I64) {
    return builder.CreateSIToFP(value.value, builder.getFloatTy());
  }
  if (value.type == ValueType::I1) {
    llvm::Value* as_i32 = builder.CreateZExt(value.value, builder.getInt32Ty());
    return builder.CreateSIToFP(as_i32, builder.getFloatTy());
  }
  return nullptr;
}

static llvm::Value* to_f64_value(ExprValue value, llvm::IRBuilder<>& builder) {
  return to_float_value(value, ValueType::F64, builder);
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

static ExprValue evaluate_expression(const std::string& expr_text, llvm::IRBuilder<>& builder,
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
      llvm::FunctionCallee puts_fn = module->getOrInsertFunction("puts", llvm::FunctionType::get(builder.getInt32Ty(), builder.getPtrTy(), false));
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

static llvm::Value* to_i32(ExprValue value, llvm::IRBuilder<>& builder);
static llvm::Value* to_i64(ExprValue value, llvm::IRBuilder<>& builder);
static bool emit_await_semantics_for_expression(const std::string& expression,
                                                const std::unordered_map<std::string, bool>& function_async_flags,
                                                AsyncLoweringContext* async_ctx, llvm::IRBuilder<>& builder,
                                                llvm::Function* fn, support::DiagnosticSink& diag);

struct InterpChunk {
  bool placeholder = false;
  std::string text;
};

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

static bool emit_expression_statement(const std::string& line, bool has_expression, const std::string& expression,
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

    ExprValue value = evaluate_expression(inner, builder, variables, enum_variant_tags, struct_fields,
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
        evaluate_expression(effective_expr, builder, variables, enum_variant_tags, struct_fields, struct_field_types,
                            struct_instances, tuple_instances, array_instances, functions, function_returns, &closures,
                            fn, diag);
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

static llvm::Value* to_i32(ExprValue value, llvm::IRBuilder<>& builder) {
  if (value.value == nullptr) {
    return nullptr;
  }
  if (value.type == ValueType::I32) {
    return value.value;
  }
  if (value.type == ValueType::I64) {
    return builder.CreateTrunc(value.value, builder.getInt32Ty());
  }
  if (value.type == ValueType::I1) {
    return builder.CreateZExt(value.value, builder.getInt32Ty());
  }
  if (value.type == ValueType::F32 || value.type == ValueType::F64) {
    return builder.CreateFPToSI(value.value, builder.getInt32Ty());
  }
  return nullptr;
}

static llvm::Value* to_i64(ExprValue value, llvm::IRBuilder<>& builder) {
  if (value.value == nullptr) {
    return nullptr;
  }
  if (value.type == ValueType::I64) {
    return value.value;
  }
  if (value.type == ValueType::I32) {
    return builder.CreateSExt(value.value, builder.getInt64Ty());
  }
  if (value.type == ValueType::I1) {
    llvm::Value* as_i32 = builder.CreateZExt(value.value, builder.getInt32Ty());
    return builder.CreateSExt(as_i32, builder.getInt64Ty());
  }
  if (value.type == ValueType::F32 || value.type == ValueType::F64) {
    return builder.CreateFPToSI(value.value, builder.getInt64Ty());
  }
  return nullptr;
}

static llvm::Value* emit_ref_new_call(VariableSlot::Ownership ownership, llvm::Value* value_i64,
                                      llvm::Function* fn, llvm::IRBuilder<>& builder) {
  if (value_i64 == nullptr || fn == nullptr) {
    return nullptr;
  }
  llvm::Module* module = fn->getParent();
  if (module == nullptr) {
    return nullptr;
  }
  const char* symbol = ownership == VariableSlot::Ownership::Arc ? "thag_arc_new" : "thag_rc_new";
  llvm::FunctionCallee callee =
      module->getOrInsertFunction(symbol, llvm::FunctionType::get(builder.getPtrTy(), {builder.getInt64Ty(), builder.getPtrTy()}, false));
  llvm::AllocaInst* tmp = create_entry_alloca(fn, builder.getInt64Ty(), std::string(symbol) + ".tmp");
  builder.CreateStore(value_i64, tmp);
  return builder.CreateCall(callee, {builder.getInt64(8), tmp});
}

static llvm::Value* emit_ref_clone_call(VariableSlot::Ownership ownership, llvm::Value* handle, llvm::Function* fn,
                                        llvm::IRBuilder<>& builder) {
  if (handle == nullptr || fn == nullptr) {
    return nullptr;
  }
  llvm::Module* module = fn->getParent();
  if (module == nullptr) {
    return nullptr;
  }
  const char* symbol = ownership == VariableSlot::Ownership::Arc ? "thag_arc_clone" : "thag_rc_clone";
  llvm::FunctionCallee callee =
      module->getOrInsertFunction(symbol, llvm::FunctionType::get(builder.getPtrTy(), {builder.getPtrTy()}, false));
  return builder.CreateCall(callee, {handle});
}

static void emit_ref_drop_call(VariableSlot::Ownership ownership, llvm::Value* handle, llvm::Function* fn,
                               llvm::IRBuilder<>& builder) {
  if (handle == nullptr || fn == nullptr) {
    return;
  }
  llvm::Module* module = fn->getParent();
  if (module == nullptr) {
    return;
  }
  const char* symbol = ownership == VariableSlot::Ownership::Arc ? "thag_arc_drop" : "thag_rc_drop";
  llvm::FunctionCallee callee =
      module->getOrInsertFunction(symbol, llvm::FunctionType::get(builder.getVoidTy(), {builder.getPtrTy()}, false));
  builder.CreateCall(callee, {handle});
}

static llvm::Value* to_i1(ExprValue value, llvm::IRBuilder<>& builder) {
  if (value.value == nullptr) {
    return nullptr;
  }
  if (value.type == ValueType::I1) {
    return value.value;
  }
  if (value.type == ValueType::I32) {
    return builder.CreateICmpNE(value.value, builder.getInt32(0));
  }
  if (value.type == ValueType::I64) {
    return builder.CreateICmpNE(value.value, builder.getInt64(0));
  }
  if (value.type == ValueType::F32) {
    return builder.CreateFCmpONE(value.value, llvm::ConstantFP::get(builder.getFloatTy(), 0.0));
  }
  if (value.type == ValueType::F64) {
    return builder.CreateFCmpONE(value.value, llvm::ConstantFP::get(builder.getDoubleTy(), 0.0));
  }
  return nullptr;
}

static ValueType value_type_from_return_type(const std::string& type_name) {
  if (type_name == "i32" || type_name.empty()) {
    return ValueType::I32;
  }
  if (type_name == "i64") {
    return ValueType::I64;
  }
  if (type_name == "f32") {
    return ValueType::F32;
  }
  if (type_name == "f64") {
    return ValueType::F64;
  }
  if (type_name == "bool") {
    return ValueType::I1;
  }
  if (type_name == "Rc" || type_name == "Arc" || starts_with(type_name, "Rc<") || starts_with(type_name, "Arc<")) {
    return ValueType::I8Ptr;
  }
  if (type_name == "ptr" || type_name == "string" || type_name == "String") {
    return ValueType::I8Ptr;
  }
  if (type_name == "Option" || type_name == "Result" || starts_with(type_name, "Option<") ||
      starts_with(type_name, "Result<")) {
    return ValueType::I32;
  }
  if (type_name == "void") {
    return ValueType::Void;
  }
  return ValueType::I32;
}

static llvm::Type* llvm_type_from_value_type(ValueType type, llvm::IRBuilder<>& builder) {
  if (type == ValueType::I32) {
    return builder.getInt32Ty();
  }
  if (type == ValueType::I64) {
    return builder.getInt64Ty();
  }
  if (type == ValueType::F32) {
    return builder.getFloatTy();
  }
  if (type == ValueType::F64) {
    return builder.getDoubleTy();
  }
  if (type == ValueType::I1) {
    return builder.getInt1Ty();
  }
  if (type == ValueType::Void) {
    return builder.getVoidTy();
  }
  if (type == ValueType::I8Ptr) {
    return llvm::PointerType::get(builder.getContext(), 0);
  }
  return builder.getInt32Ty();
}

static llvm::Value* cast_value_to_type(ExprValue value, ValueType target, llvm::IRBuilder<>& builder) {
  if (target == ValueType::Void) {
    return nullptr;
  }
  if (value.value == nullptr) {
    return nullptr;
  }
  if (value.type == target) {
    return value.value;
  }
  if (target == ValueType::I32) {
    return to_i32(value, builder);
  }
  if (target == ValueType::I64) {
    return to_i64(value, builder);
  }
  if (target == ValueType::I1) {
    return to_i1(value, builder);
  }
  if (target == ValueType::F64) {
    return to_f64_value(value, builder);
  }
  if (target == ValueType::F32) {
    llvm::Value* as_f64 = to_f64_value(value, builder);
    if (as_f64 == nullptr) {
      return nullptr;
    }
    return builder.CreateFPTrunc(as_f64, builder.getFloatTy());
  }
  if (target == ValueType::I8Ptr) {
    if (value.type == ValueType::I8Ptr) {
      return value.value;
    }
    return nullptr;
  }
  return nullptr;
}

static llvm::Value* default_value_for_type(ValueType type, llvm::IRBuilder<>& builder) {
  if (type == ValueType::I1) {
    return builder.getInt1(false);
  }
  if (type == ValueType::I64) {
    return builder.getInt64(0);
  }
  if (type == ValueType::F32) {
    return llvm::ConstantFP::get(builder.getFloatTy(), 0.0);
  }
  if (type == ValueType::F64) {
    return llvm::ConstantFP::get(builder.getDoubleTy(), 0.0);
  }
  if (type == ValueType::I8Ptr) {
    return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(builder.getPtrTy()));
  }
  if (type == ValueType::Void) {
    return nullptr;
  }
  return builder.getInt32(0);
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

static void emit_await_spawn_wrapper(llvm::IRBuilder<>& builder, llvm::Function* fn, llvm::Value* coro_handle) {
  if (fn == nullptr || fn->getParent() == nullptr) {
    return;
  }
  llvm::Module* module = fn->getParent();
  llvm::FunctionCallee spawn_fn =
      module->getOrInsertFunction("thag_task_scope_spawn",
                                  llvm::FunctionType::get(builder.getInt32Ty(),
                                                          {builder.getPtrTy(), builder.getPtrTy(), builder.getPtrTy()},
                                                          false));
  llvm::FunctionCallee done_fn =
      module->getOrInsertFunction("thag_coro_done",
                                  llvm::FunctionType::get(builder.getInt1Ty(), {builder.getPtrTy()}, false));
  llvm::FunctionCallee resume_fn =
      module->getOrInsertFunction("thag_coro_resume",
                                  llvm::FunctionType::get(builder.getVoidTy(), {builder.getPtrTy()}, false));

  llvm::Value* effective_handle = coro_handle;
  if (effective_handle == nullptr) {
    effective_handle = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(builder.getPtrTy()));
  }
  llvm::Value* done = builder.CreateCall(done_fn, {effective_handle}, "await.done");
  auto* spawn_bb = llvm::BasicBlock::Create(builder.getContext(), "await.spawn", fn);
  auto* cont_bb = llvm::BasicBlock::Create(builder.getContext(), "await.spawn.cont", fn);
  builder.CreateCondBr(done, cont_bb, spawn_bb);

  builder.SetInsertPoint(spawn_bb);
  llvm::Value* null_scope = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(builder.getPtrTy()));
  llvm::Value* resume_ptr = builder.CreateBitCast(resume_fn.getCallee(), builder.getPtrTy());
  builder.CreateCall(spawn_fn, {null_scope, resume_ptr, effective_handle});
  builder.CreateBr(cont_bb);

  builder.SetInsertPoint(cont_bb);
}

static bool emit_async_suspend_point(AsyncLoweringContext* async_ctx, llvm::IRBuilder<>& builder, llvm::Function* fn,
                                     support::DiagnosticSink& diag) {
  if (async_ctx == nullptr || !async_ctx->enabled) {
    return true;
  }
  if (fn == nullptr || fn->getParent() == nullptr || async_ctx->coro_handle == nullptr ||
      async_ctx->finalize_block == nullptr) {
    diag.error("E2065", "internal async lowering error: missing coroutine context");
    return false;
  }

  llvm::Value* save = builder.CreateCall(async_ctx->coro_save, {async_ctx->coro_handle}, "await.save");
  llvm::Value* suspend = builder.CreateCall(async_ctx->coro_suspend, {save, builder.getFalse()}, "await.suspend");

  auto* resume_bb = llvm::BasicBlock::Create(builder.getContext(), "await.resume", fn);
  auto* finalize_bb = llvm::BasicBlock::Create(builder.getContext(), "await.finalize", fn);
  llvm::SwitchInst* sw = builder.CreateSwitch(suspend, resume_bb, 2);
  sw->addCase(builder.getInt8(1), finalize_bb);
  sw->addCase(builder.getInt8(2), finalize_bb);

  builder.SetInsertPoint(finalize_bb);
  builder.CreateBr(async_ctx->finalize_block);

  builder.SetInsertPoint(resume_bb);
  return true;
}

static bool emit_await_semantics_for_expression(const std::string& expression,
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

static std::size_t find_block_end(const std::vector<lowering::CoreStmt>& stmts, std::size_t start_index,
                                  int parent_indent) {
  std::size_t i = start_index;
  while (i < stmts.size() && stmts[i].indent > parent_indent) {
    ++i;
  }
  return i;
}

static bool function_contains_await(const lowering::CoreFunction& fn_def) {
  for (const auto& st : fn_def.statements) {
    if (st.has_await) {
      return true;
    }
  }
  return starts_with(trim(fn_def.return_expression), "await ");
}

static std::string parse_constructor_call_name(
    const std::string& expr, const std::unordered_map<std::string, std::vector<std::string>>& struct_fields) {
  std::string clean = trim(expr);
  const std::size_t lparen = clean.find('(');
  const std::size_t rparen = clean.rfind(')');
  if (lparen == std::string::npos || rparen == std::string::npos || rparen <= lparen) {
    return "";
  }
  if (trim(clean.substr(lparen + 1, rparen - lparen - 1)).empty() == false) {
    return "";
  }
  const std::string name = trim(clean.substr(0, lparen));
  if (struct_fields.find(name) == struct_fields.end()) {
    return "";
  }
  return name;
}

static ValueType value_type_from_field_annotation(const std::string& type_name) {
  if (type_name == "i32" || type_name.empty()) {
    return ValueType::I32;
  }
  if (type_name == "i64") {
    return ValueType::I64;
  }
  if (type_name == "f32") {
    return ValueType::F32;
  }
  if (type_name == "f64") {
    return ValueType::F64;
  }
  if (type_name == "bool") {
    return ValueType::I1;
  }
  if (type_name == "Option" || type_name == "Result" || starts_with(type_name, "Option<") ||
      starts_with(type_name, "Result<")) {
    return ValueType::I32;
  }
  if (type_name == "Rc" || type_name == "Arc" || starts_with(type_name, "Rc<") || starts_with(type_name, "Arc<")) {
    return ValueType::I8Ptr;
  }
  return ValueType::I32;
}

static ValueType field_value_type_for_struct(const std::string& struct_name, const std::string& field_name,
                                             const std::unordered_map<std::string, std::string>& struct_field_types) {
  auto type_it = struct_field_types.find(struct_name + "." + field_name);
  if (type_it == struct_field_types.end()) {
    return ValueType::I32;
  }
  return value_type_from_field_annotation(type_it->second);
}

struct ForHeader {
  bool ok = false;
  std::string label;
  std::string loop_var;
  std::string start_expr;
  std::string end_expr;
};

struct LoopControlTarget {
  std::string label;
  llvm::BasicBlock* continue_target = nullptr;
  llvm::BasicBlock* break_target = nullptr;
};

static bool parse_loop_header_label(const std::string& line, const std::string& keyword, std::string& label,
                                    std::string& normalized) {
  label.clear();
  normalized = trim(line);
  if (starts_with(normalized, keyword + " ")) {
    return true;
  }
  const std::size_t colon = normalized.find(':');
  if (colon == std::string::npos || colon + 1 >= normalized.size()) {
    return false;
  }
  std::string maybe_label = trim(normalized.substr(0, colon));
  normalized = trim(normalized.substr(colon + 1));
  if (!starts_with(normalized, keyword + " ")) {
    return false;
  }
  if (!maybe_label.empty() && maybe_label.front() == '\'') {
    maybe_label = trim(maybe_label.substr(1));
  }
  if (maybe_label.empty() || !is_ident_start(maybe_label[0])) {
    return false;
  }
  for (std::size_t i = 1; i < maybe_label.size(); ++i) {
    if (!is_ident_body(maybe_label[i])) {
      return false;
    }
  }
  label = maybe_label;
  return true;
}

static bool parse_loop_control_statement(const std::string& line, const std::string& keyword, std::string& label) {
  label.clear();
  const std::string clean = trim(line);
  if (clean == keyword) {
    return true;
  }
  if (!starts_with(clean, keyword + " ")) {
    return false;
  }
  std::string rest = trim(clean.substr(keyword.size()));
  if (!rest.empty() && rest.front() == '\'') {
    rest = trim(rest.substr(1));
  }
  if (rest.empty() || !is_ident_start(rest[0])) {
    return false;
  }
  for (std::size_t i = 1; i < rest.size(); ++i) {
    if (!is_ident_body(rest[i])) {
      return false;
    }
  }
  label = rest;
  return true;
}

static ForHeader parse_for_header(const lowering::CoreStmt& st) {
  ForHeader out;
  std::string line;
  if (!parse_loop_header_label(st.text, "for", out.label, line) || !ends_with(line, ":")) {
    return out;
  }
  line = trim(line.substr(4, line.size() - 5));
  const std::size_t in_pos = line.find(" in ");
  if (in_pos == std::string::npos || in_pos == 0) {
    return out;
  }
  out.loop_var = trim(line.substr(0, in_pos));
  if (out.loop_var.empty()) {
    return out;
  }
  std::string range = trim(line.substr(in_pos + 4));
  const std::size_t dots = range.find("..");
  if (dots == std::string::npos) {
    return out;
  }
  out.start_expr = trim(range.substr(0, dots));
  out.end_expr = trim(range.substr(dots + 2));
  if (out.start_expr.empty() || out.end_expr.empty()) {
    return out;
  }
  out.ok = true;
  return out;
}

struct MatchArmLabel {
  bool valid = false;
  bool wildcard = false;
  int value = 0;
  bool enum_variant = false;
  std::string payload_binding;
};

static MatchArmLabel parse_match_arm_label(const std::string& line,
                                           const std::unordered_map<std::string, int>& enum_variant_tags) {
  MatchArmLabel out;
  std::string text = trim(line);
  if (!ends_with(text, ":")) {
    return out;
  }
  text = trim(text.substr(0, text.size() - 1));
  if (text == "_" || text == "else") {
    out.valid = true;
    out.wildcard = true;
    return out;
  }
  if (text == "true") {
    out.valid = true;
    out.value = 1;
    return out;
  }
  if (text == "false") {
    out.valid = true;
    out.value = 0;
    return out;
  }
  std::string variant_name = text;
  std::string payload_binding;
  const std::size_t lparen = text.find('(');
  const std::size_t rparen = text.rfind(')');
  if (lparen != std::string::npos || rparen != std::string::npos) {
    if (lparen == std::string::npos || rparen == std::string::npos || rparen <= lparen) {
      return out;
    }
    variant_name = trim(text.substr(0, lparen));
    payload_binding = trim(text.substr(lparen + 1, rparen - lparen - 1));
    if (payload_binding == "_") {
      payload_binding.clear();
    } else if (!payload_binding.empty()) {
      if (!is_ident_start(payload_binding[0])) {
        return out;
      }
      for (std::size_t i = 1; i < payload_binding.size(); ++i) {
        if (!is_ident_body(payload_binding[i])) {
          return out;
        }
      }
    }
  }

  const auto variant_it = enum_variant_tags.find(variant_name);
  if (variant_it != enum_variant_tags.end()) {
    out.valid = true;
    out.value = variant_it->second;
    out.enum_variant = true;
    out.payload_binding = payload_binding;
    return out;
  }
  if (const std::optional<int> builtin = builtin_variant_tag(variant_name); builtin.has_value()) {
    out.valid = true;
    out.value = *builtin;
    out.enum_variant = true;
    out.payload_binding = payload_binding;
    return out;
  }
  bool neg = false;
  std::size_t i = 0;
  if (!text.empty() && text[0] == '-') {
    neg = true;
    i = 1;
  }
  if (i >= text.size()) {
    return out;
  }
  for (std::size_t k = i; k < text.size(); ++k) {
    if (!std::isdigit(static_cast<unsigned char>(text[k]))) {
      return out;
    }
  }
  long long parsed = 0;
  try {
    parsed = std::stoll(text.substr(i));
  } catch (const std::exception&) {
    return out;
  }
  if (parsed > std::numeric_limits<int>::max() || parsed < std::numeric_limits<int>::min()) {
    return out;
  }
  out.valid = true;
  const int as_int = static_cast<int>(parsed);
  out.value = neg ? -as_int : as_int;
  return out;
}

static bool emit_block(const std::vector<lowering::CoreStmt>& stmts,
                       const std::unordered_map<std::string, int>& enum_variant_tags,
                       const std::unordered_map<std::string, std::vector<std::string>>& struct_fields,
                       const std::unordered_map<std::string, std::string>& struct_field_types,
                       const std::unordered_map<std::string, llvm::StructType*>& llvm_struct_types,
                       const std::unordered_map<std::string, llvm::Function*>& functions,
                       const std::unordered_map<std::string, ValueType>& function_returns,
                       const std::unordered_map<std::string, bool>& function_async_flags,
                       ValueType expected_return_type,
                       std::size_t& index, int parent_indent, llvm::Function* fn, llvm::Module* module,
                       llvm::IRBuilder<>& builder, std::unordered_map<std::string, VariableSlot>& variables,
                       std::unordered_map<std::string, StructInstance>& struct_instances,
                       std::unordered_map<std::string, TupleInstance>& tuple_instances,
                       std::unordered_map<std::string, ArrayInstance>& array_instances,
                       std::vector<LoopControlTarget>& loop_targets,
                       std::unordered_map<std::string, ClosureDef>& closures, AsyncLoweringContext* async_ctx,
                       support::DiagnosticSink& diag) {
  llvm::FunctionCallee printf_fn = module->getOrInsertFunction(
      "printf",
      llvm::FunctionType::get(builder.getInt32Ty(), builder.getPtrTy(), true));
  llvm::Value* printf_i32_fmt = create_global_cstr_ptr(builder, "%d\n", "printf_i32_fmt");
  llvm::Value* printf_i64_fmt = create_global_cstr_ptr(builder, "%lld\n", "printf_i64_fmt");
  llvm::Value* printf_f64_fmt = create_global_cstr_ptr(builder, "%f\n", "printf_f64_fmt");
  llvm::Value* printf_str_fmt = create_global_cstr_ptr(builder, "%s\n", "printf_str_fmt");
  llvm::Value* printf_i32_raw_fmt = create_global_cstr_ptr(builder, "%d", "printf_i32_raw_fmt");
  llvm::Value* printf_i64_raw_fmt = create_global_cstr_ptr(builder, "%lld", "printf_i64_raw_fmt");
  llvm::Value* printf_f64_raw_fmt = create_global_cstr_ptr(builder, "%f", "printf_f64_raw_fmt");
  llvm::Value* printf_raw_str_fmt = create_global_cstr_ptr(builder, "%s", "printf_raw_str_fmt");
  llvm::Value* printf_newline_fmt = create_global_cstr_ptr(builder, "\n", "printf_newline_fmt");
  std::vector<lowering::CoreStmt> deferred;

  auto flush_deferred = [&]() {
    for (auto it = deferred.rbegin(); it != deferred.rend(); ++it) {
      const std::string deferred_line = it->has_expression ? trim(it->expression) : trim(it->text);
      if (!emit_expression_statement(deferred_line, it->has_expression, it->expression, builder, fn, printf_fn,
                                     printf_i32_fmt, printf_f64_fmt, printf_i64_fmt, printf_str_fmt,
                                     printf_i32_raw_fmt, printf_i64_raw_fmt,
                                     printf_f64_raw_fmt, printf_raw_str_fmt, printf_newline_fmt, variables,
                                     enum_variant_tags, struct_fields, struct_field_types, struct_instances,
                                     tuple_instances, array_instances, functions, function_returns, function_async_flags,
                                     closures, it->has_await, async_ctx, diag)) {
        return false;
      }
    }
    deferred.clear();
    return true;
  };

  auto drop_owned_variables = [&]() {
    for (auto& [_, slot] : variables) {
      if (slot.type != ValueType::I8Ptr || slot.alloca == nullptr) {
        continue;
      }
      if (slot.ownership != VariableSlot::Ownership::Rc && slot.ownership != VariableSlot::Ownership::Arc) {
        continue;
      }
      llvm::Value* handle = builder.CreateLoad(slot.alloca->getAllocatedType(), slot.alloca);
      emit_ref_drop_call(slot.ownership, handle, fn, builder);
    }
  };

  auto eval_with_try = [&](const std::string& expr_text, bool has_await, ExprValue& out_value) {
    std::string try_inner;
    if (!parse_try_operator_expr(expr_text, try_inner)) {
      out_value = evaluate_expression(expr_text, builder, variables, enum_variant_tags, struct_fields, struct_field_types,
                                      struct_instances, tuple_instances, array_instances, functions, function_returns,
                                      &closures, fn, diag);
      if (out_value.value == nullptr || out_value.type == ValueType::Invalid) {
        return false;
      }
      if (has_await &&
          !emit_await_semantics_for_expression(expr_text, function_async_flags, async_ctx, builder, fn, diag)) {
        return false;
      }
      return true;
    }
    if (expected_return_type != ValueType::I32) {
      diag.error("E2053", "operator '?' requires function returning Result/Option-compatible i32 encoding");
      return false;
    }
    ExprValue encoded = evaluate_expression(try_inner, builder, variables, enum_variant_tags, struct_fields,
                                            struct_field_types, struct_instances, tuple_instances, array_instances,
                                            functions, function_returns, &closures, fn, diag);
    if (encoded.value == nullptr || encoded.type == ValueType::Invalid) {
      return false;
    }
    if (encoded.type != ValueType::I32) {
      diag.error("E2054", "operator '?' expects i32-encoded Result/Option value");
      return false;
    }
    if (has_await &&
        !emit_await_semantics_for_expression(expr_text, function_async_flags, async_ctx, builder, fn, diag)) {
      return false;
    }
    llvm::Value* tag = extract_enum_tag_value(encoded.value, builder);
    llvm::Value* is_err = builder.CreateICmpEQ(tag, builder.getInt32(3));
    llvm::Value* is_none = builder.CreateICmpEQ(tag, builder.getInt32(0));
    llvm::Value* should_return = builder.CreateOr(is_err, is_none);
    auto* ok_bb = llvm::BasicBlock::Create(builder.getContext(), "try.ok", fn);
    auto* err_bb = llvm::BasicBlock::Create(builder.getContext(), "try.err", fn);
    builder.CreateCondBr(should_return, err_bb, ok_bb);

    builder.SetInsertPoint(err_bb);
    for (auto it = deferred.rbegin(); it != deferred.rend(); ++it) {
      const std::string deferred_line = it->has_expression ? trim(it->expression) : trim(it->text);
      if (!emit_expression_statement(deferred_line, it->has_expression, it->expression, builder, fn, printf_fn,
                                     printf_i32_fmt, printf_f64_fmt, printf_i64_fmt, printf_str_fmt,
                                     printf_i32_raw_fmt, printf_i64_raw_fmt,
                                     printf_f64_raw_fmt, printf_raw_str_fmt, printf_newline_fmt, variables,
                                     enum_variant_tags, struct_fields, struct_field_types, struct_instances,
                                     tuple_instances, array_instances, functions, function_returns, function_async_flags,
                                     closures, it->has_await, async_ctx, diag)) {
        return false;
      }
    }
    drop_owned_variables();
    if (async_ctx != nullptr && async_ctx->enabled) {
      if (async_ctx->return_slot != nullptr) {
        builder.CreateStore(encoded.value, async_ctx->return_slot);
      }
      builder.CreateBr(async_ctx->finalize_block);
    } else {
      builder.CreateRet(encoded.value);
    }

    builder.SetInsertPoint(ok_bb);
    out_value = ExprValue{extract_enum_payload_value(encoded.value, builder), ValueType::I32};
    return true;
  };

  while (index < stmts.size() && stmts[index].indent > parent_indent) {
    const lowering::CoreStmt& st = stmts[index];
    if (st.kind == lowering::CoreStmtKind::Let) {
      std::vector<std::string> tuple_bindings;
      std::string tuple_source_expr;
      if (parse_tuple_destructure_let(st.text, tuple_bindings, tuple_source_expr)) {
        const std::string tuple_source = trim(tuple_source_expr);
        auto tuple_it = tuple_instances.find(tuple_source);
        if (tuple_it == tuple_instances.end()) {
          diag.error("E2055", "tuple destructuring source is not a tuple variable: '" + tuple_source + "'");
          return false;
        }
        if (tuple_bindings.size() != tuple_it->second.element_types.size()) {
          diag.error("E2056", "tuple destructuring arity mismatch: expected " +
                                  std::to_string(tuple_it->second.element_types.size()) + " got " +
                                  std::to_string(tuple_bindings.size()));
          return false;
        }
        for (std::size_t i = 0; i < tuple_bindings.size(); ++i) {
          const std::string& bind_name = tuple_bindings[i];
          const ValueType bind_type = tuple_it->second.element_types[i];
          llvm::Value* field_ptr =
              builder.CreateStructGEP(tuple_it->second.llvm_type, tuple_it->second.alloca, static_cast<unsigned>(i),
                                      tuple_source + ".destructure." + std::to_string(i) + ".ptr");
          llvm::Value* loaded = builder.CreateLoad(llvm_type_from_value_type(bind_type, builder), field_ptr);
          auto var_it = variables.find(bind_name);
          if (var_it == variables.end()) {
            llvm::AllocaInst* alloca = create_entry_alloca(fn, llvm_type_from_value_type(bind_type, builder), bind_name);
            variables[bind_name] = VariableSlot{alloca, bind_type};
            var_it = variables.find(bind_name);
          }
          if (var_it->second.type != bind_type) {
            diag.error("E2057", "tuple destructuring changes existing variable type for '" + bind_name + "'");
            return false;
          }
          builder.CreateStore(loaded, var_it->second.alloca);
        }
        ++index;
        continue;
      }

      const std::string name = parse_let_name(st.text);
      if (name.empty() || !st.has_expression) {
        diag.error("E2010", "invalid let statement in backend: '" + st.text + "'");
        return false;
      }
      std::vector<std::string> closure_params;
      std::string closure_body;
      bool closure_block = false;
      if (parse_closure_literal(st.expression, closure_params, closure_body, closure_block)) {
        ClosureDef closure;
        closure.params = closure_params;
        closure.body_expr = closure_body;
        closure.block_body = closure_block;
        for (const std::string& capture : collect_closure_captures(closure_params, closure_body)) {
          auto capture_it = variables.find(capture);
          if (capture_it == variables.end() || capture_it->second.alloca == nullptr) {
            continue;
          }
          llvm::Value* loaded =
              builder.CreateLoad(capture_it->second.alloca->getAllocatedType(), capture_it->second.alloca, capture + ".capt");
          if (capture_it->second.type == ValueType::I1) {
            loaded = builder.CreateZExt(loaded, builder.getInt32Ty());
          } else if (capture_it->second.type == ValueType::F32 || capture_it->second.type == ValueType::F64) {
            loaded = builder.CreateFPToSI(loaded, builder.getInt32Ty());
          }
          closure.captured_i32_values[capture] = loaded;
        }
        closures[name] = std::move(closure);
        llvm::AllocaInst* marker = create_entry_alloca(fn, builder.getInt32Ty(), name + ".closure");
        variables[name] = VariableSlot{marker, ValueType::I32};
        builder.CreateStore(builder.getInt32(0), marker);
        ++index;
        continue;
      }
      const ParsedConstructorCall ctor_call = parse_constructor_call(st.expression);
      if (!ctor_call.error.empty()) {
        diag.error("E2033", "invalid constructor call for '" + name + "': " + ctor_call.error);
        return false;
      }
      if (ctor_call.ok) {
        auto struct_ty_it = llvm_struct_types.find(ctor_call.name);
        auto fields_it = struct_fields.find(ctor_call.name);
        if (struct_ty_it != llvm_struct_types.end() && fields_it != struct_fields.end()) {
          llvm::StructType* struct_ty = struct_ty_it->second;
          llvm::AllocaInst* instance_alloca = create_entry_alloca(fn, struct_ty, name + ".struct");
          const auto& fields = fields_it->second;
          if (ctor_call.args.size() > fields.size()) {
            diag.error("E2034", "struct constructor '" + ctor_call.name + "' expects at most " +
                                    std::to_string(fields.size()) + " args but got " +
                                    std::to_string(ctor_call.args.size()));
            return false;
          }
          for (std::size_t field_index = 0; field_index < fields.size(); ++field_index) {
            const std::string& field_name = fields[field_index];
            llvm::Value* field_ptr =
                builder.CreateStructGEP(struct_ty, instance_alloca, static_cast<unsigned>(field_index),
                                        name + "." + field_name + ".ptr");
            const ValueType field_type = field_value_type_for_struct(ctor_call.name, field_name, struct_field_types);
            llvm::Value* init_value = nullptr;
            if (field_index < ctor_call.args.size()) {
              ExprValue arg_value = evaluate_expression(
                  ctor_call.args[field_index], builder, variables, enum_variant_tags, struct_fields, struct_field_types,
                  struct_instances, tuple_instances, array_instances, functions, function_returns, &closures, fn, diag);
              init_value = cast_value_to_type(arg_value, field_type, builder);
              if (init_value == nullptr) {
                diag.error("E2035", "constructor argument type mismatch for field '" + field_name + "'");
                return false;
              }
            } else if (field_type == ValueType::I1) {
              init_value = builder.getInt1(false);
            } else if (field_type == ValueType::F32) {
              init_value = llvm::ConstantFP::get(builder.getFloatTy(), 0.0);
            } else if (field_type == ValueType::F64) {
              init_value = llvm::ConstantFP::get(builder.getDoubleTy(), 0.0);
            } else {
              init_value = builder.getInt32(0);
            }
            builder.CreateStore(init_value, field_ptr);
          }
          struct_instances[name] = StructInstance{instance_alloca, struct_ty, ctor_call.name};
          llvm::AllocaInst* marker = create_entry_alloca(fn, builder.getInt32Ty(), name);
          variables[name] = VariableSlot{marker, ValueType::I32};
          builder.CreateStore(builder.getInt32(0), marker);
          ++index;
          continue;
        }
      }

      std::vector<std::string> tuple_elements;
      if (parse_tuple_literal_expr(st.expression, tuple_elements)) {
        std::vector<ExprValue> tuple_values;
        std::vector<ValueType> tuple_types;
        std::vector<llvm::Type*> llvm_types;
        tuple_values.reserve(tuple_elements.size());
        tuple_types.reserve(tuple_elements.size());
        llvm_types.reserve(tuple_elements.size());
        for (const std::string& element_expr : tuple_elements) {
          ExprValue element_value;
          if (!eval_with_try(element_expr, false, element_value)) {
            return false;
          }
          tuple_values.push_back(element_value);
          tuple_types.push_back(element_value.type);
          llvm_types.push_back(llvm_type_from_value_type(element_value.type, builder));
        }
        llvm::StructType* tuple_ty = llvm::StructType::get(builder.getContext(), llvm_types, false);
        llvm::AllocaInst* tuple_alloca = create_entry_alloca(fn, tuple_ty, name + ".tuple");
        for (std::size_t i = 0; i < tuple_values.size(); ++i) {
          llvm::Value* field_ptr =
              builder.CreateStructGEP(tuple_ty, tuple_alloca, static_cast<unsigned>(i), name + ".tuple.ptr");
          builder.CreateStore(tuple_values[i].value, field_ptr);
        }
        tuple_instances[name] = TupleInstance{tuple_alloca, tuple_ty, tuple_types};
        llvm::AllocaInst* marker = create_entry_alloca(fn, builder.getInt32Ty(), name);
        variables[name] = VariableSlot{marker, ValueType::I32};
        builder.CreateStore(builder.getInt32(0), marker);
        ++index;
        continue;
      }

      std::vector<std::string> array_elements;
      if (parse_array_literal_expr(st.expression, array_elements)) {
        std::vector<ExprValue> array_values;
        array_values.reserve(array_elements.size());
        ValueType element_type = ValueType::I32;
        for (const std::string& element_expr : array_elements) {
          ExprValue element_value;
          if (!eval_with_try(element_expr, false, element_value)) {
            return false;
          }
          if (array_values.empty()) {
            element_type = element_value.type;
          } else if (element_type != element_value.type) {
            if ((element_type == ValueType::F64 || element_value.type == ValueType::F64) &&
                is_numeric_type(element_type) && is_numeric_type(element_value.type)) {
              element_type = ValueType::F64;
            } else if ((element_type == ValueType::F32 || element_value.type == ValueType::F32) &&
                       is_numeric_type(element_type) && is_numeric_type(element_value.type)) {
              element_type = ValueType::F32;
            } else if (is_numeric_type(element_type) && is_numeric_type(element_value.type)) {
              element_type = ValueType::I32;
            } else {
              diag.error("E2058", "array literal elements must have compatible numeric or same types");
              return false;
            }
          }
          array_values.push_back(element_value);
        }
        llvm::Type* element_llvm_type = llvm_type_from_value_type(element_type, builder);
        llvm::ArrayType* array_ty = llvm::ArrayType::get(element_llvm_type, array_values.size());
        llvm::AllocaInst* array_alloca = create_entry_alloca(fn, array_ty, name + ".array");
        for (std::size_t i = 0; i < array_values.size(); ++i) {
          llvm::Value* casted = cast_value_to_type(array_values[i], element_type, builder);
          if (casted == nullptr) {
            diag.error("E2059", "array literal element cannot be converted to element type");
            return false;
          }
          llvm::Value* elem_ptr =
              builder.CreateInBoundsGEP(array_ty, array_alloca, {builder.getInt32(0), builder.getInt32(static_cast<int>(i))},
                                        name + ".array.elem.ptr");
          builder.CreateStore(casted, elem_ptr);
        }
        array_instances[name] = ArrayInstance{array_alloca, element_type, array_values.size()};
        llvm::AllocaInst* marker = create_entry_alloca(fn, builder.getInt32Ty(), name);
        variables[name] = VariableSlot{marker, ValueType::I32};
        builder.CreateStore(builder.getInt32(0), marker);
        ++index;
        continue;
      }

      ExprValue value;
      if (!eval_with_try(st.expression, st.has_await, value)) {
        return false;
      }
      const std::string annotation = parse_let_annotation(st.text);
      VariableSlot::Ownership ownership = VariableSlot::Ownership::None;
      if (!annotation.empty() && annotation != "Send" && annotation != "Sync") {
        ValueType declared_type = ValueType::Invalid;
        if (annotation == "i32" || annotation == "Option" || annotation == "Result" ||
            starts_with(annotation, "Option<") || starts_with(annotation, "Result<")) {
          declared_type = ValueType::I32;
        } else if (annotation == "i64") {
          declared_type = ValueType::I64;
        } else if (annotation == "f32") {
          declared_type = ValueType::F32;
        } else if (annotation == "f64") {
          declared_type = ValueType::F64;
        } else if (annotation == "bool") {
          declared_type = ValueType::I1;
        } else if (annotation == "Rc" || starts_with(annotation, "Rc<")) {
          declared_type = ValueType::I8Ptr;
          ownership = VariableSlot::Ownership::Rc;
        } else if (annotation == "Arc" || starts_with(annotation, "Arc<")) {
          declared_type = ValueType::I8Ptr;
          ownership = VariableSlot::Ownership::Arc;
        } else if (annotation == "string" || annotation == "String" || annotation == "ptr") {
          declared_type = ValueType::I8Ptr;
        }
        if (declared_type == ValueType::Invalid) {
          diag.error("E2010", "unsupported let annotation in backend: '" + annotation + "'");
          return false;
        }
        llvm::Value* casted = nullptr;
        if (ownership == VariableSlot::Ownership::Rc || ownership == VariableSlot::Ownership::Arc) {
          const std::string source_ident = parse_simple_identifier_expr(st.expression);
          if (!source_ident.empty()) {
            auto source_it = variables.find(source_ident);
            if (source_it != variables.end() && source_it->second.type == ValueType::I8Ptr &&
                source_it->second.ownership == ownership) {
              llvm::Value* src_handle = builder.CreateLoad(source_it->second.alloca->getAllocatedType(), source_it->second.alloca);
              casted = emit_ref_clone_call(ownership, src_handle, fn, builder);
            }
          }
          if (casted == nullptr && value.type == ValueType::I8Ptr) {
            casted = value.value;
          }
          if (casted == nullptr) {
            llvm::Value* as_i64 = to_i64(value, builder);
            casted = emit_ref_new_call(ownership, as_i64, fn, builder);
          }
        } else {
          casted = cast_value_to_type(value, declared_type, builder);
        }
        if (casted == nullptr) {
          diag.error("E2011", "cannot cast let value for '" + name + "' to declared type '" + annotation + "'");
          return false;
        }
        value = ExprValue{casted, declared_type};
      }
      auto it = variables.find(name);
      if (it == variables.end()) {
        llvm::Type* type = llvm_type_from_value_type(value.type, builder);
        llvm::AllocaInst* alloca = create_entry_alloca(fn, type, name);
        variables[name] = VariableSlot{alloca, value.type};
        it = variables.find(name);
      }
      if (it->second.type != value.type) {
        diag.error("E2011", "type change for variable '" + name + "' is not supported in backend");
        return false;
      }
      if (ownership == VariableSlot::Ownership::None && value.type == ValueType::I8Ptr) {
        if (starts_with(trim(st.expression), "Rc(")) {
          ownership = VariableSlot::Ownership::Rc;
        } else if (starts_with(trim(st.expression), "Arc(")) {
          ownership = VariableSlot::Ownership::Arc;
        }
      }
      if (it->second.type == ValueType::I8Ptr &&
          (it->second.ownership == VariableSlot::Ownership::Rc || it->second.ownership == VariableSlot::Ownership::Arc) &&
          it->second.alloca != nullptr) {
        llvm::Value* prev = builder.CreateLoad(it->second.alloca->getAllocatedType(), it->second.alloca);
        emit_ref_drop_call(it->second.ownership, prev, fn, builder);
      }
      builder.CreateStore(value.value, it->second.alloca);
      it->second.ownership = ownership == VariableSlot::Ownership::None ? it->second.ownership : ownership;
      ++index;
      continue;
    }

    if (st.kind == lowering::CoreStmtKind::Assign) {
      const std::string target = trim(st.target.empty() ? st.text : st.target);
      if (target.empty() || !st.has_expression) {
        diag.error("E2022", "invalid assignment statement in backend: '" + st.text + "'");
        return false;
      }
      std::string base_name;
      std::string field_name;
      if (split_dotted_name(target, base_name, field_name)) {
        auto inst_it = struct_instances.find(base_name);
        if (inst_it != struct_instances.end()) {
          const std::size_t field_index =
              field_index_for_struct(inst_it->second.struct_name, field_name, struct_fields);
          if (field_index == static_cast<std::size_t>(-1)) {
            diag.error("E2023", "unknown field '" + field_name + "' on struct '" + inst_it->second.struct_name + "'");
            return false;
          }
          ExprValue value =
              ExprValue{};
          if (!eval_with_try(st.expression, st.has_await, value)) {
            return false;
          }
          const ValueType field_type =
              field_value_type_for_struct(inst_it->second.struct_name, field_name, struct_field_types);
          llvm::Value* casted = cast_value_to_type(value, field_type, builder);
          if (casted == nullptr) {
            diag.error("E2024", "assignment type mismatch for field '" + target + "'");
            return false;
          }
          if (inst_it->second.ptr == nullptr || inst_it->second.llvm_type == nullptr) {
            diag.error("E2023", "field assignment target is not addressable: '" + target + "'");
            return false;
          }
          llvm::Value* field_ptr =
              builder.CreateStructGEP(inst_it->second.llvm_type, inst_it->second.ptr, static_cast<unsigned>(field_index),
                                      target + ".ptr");
          builder.CreateStore(casted, field_ptr);
          ++index;
          continue;
        }
      }
      auto it = variables.find(target);
      if (it == variables.end()) {
        diag.error("E2023", "assignment target is not declared: '" + target + "'");
        return false;
      }
      ExprValue value =
          ExprValue{};
      if (!eval_with_try(st.expression, st.has_await, value)) {
        return false;
      }
      llvm::Value* casted = nullptr;
      if (it->second.type == ValueType::I8Ptr &&
          (it->second.ownership == VariableSlot::Ownership::Rc || it->second.ownership == VariableSlot::Ownership::Arc)) {
        const std::string source_ident = parse_simple_identifier_expr(st.expression);
        if (!source_ident.empty()) {
          auto source_it = variables.find(source_ident);
          if (source_it != variables.end() && source_it->second.type == ValueType::I8Ptr &&
              source_it->second.ownership == it->second.ownership) {
            llvm::Value* src_handle = builder.CreateLoad(source_it->second.alloca->getAllocatedType(), source_it->second.alloca);
            casted = emit_ref_clone_call(it->second.ownership, src_handle, fn, builder);
          }
        }
        if (casted == nullptr && value.type == ValueType::I8Ptr) {
          casted = value.value;
        }
        if (casted == nullptr) {
          llvm::Value* as_i64 = to_i64(value, builder);
          casted = emit_ref_new_call(it->second.ownership, as_i64, fn, builder);
        }
      } else {
        casted = cast_value_to_type(value, it->second.type, builder);
      }
      if (casted == nullptr) {
        diag.error("E2024", "assignment type mismatch for '" + target + "'");
        return false;
      }
      if (it->second.type == ValueType::I8Ptr &&
          (it->second.ownership == VariableSlot::Ownership::Rc || it->second.ownership == VariableSlot::Ownership::Arc)) {
        llvm::Value* prev = builder.CreateLoad(it->second.alloca->getAllocatedType(), it->second.alloca);
        emit_ref_drop_call(it->second.ownership, prev, fn, builder);
      }
      builder.CreateStore(casted, it->second.alloca);
      ++index;
      continue;
    }

    if (st.kind == lowering::CoreStmtKind::Defer) {
      if (!st.has_expression) {
        diag.error("E2029", "defer statement requires expression");
        return false;
      }
      deferred.push_back(st);
      ++index;
      continue;
    }

    if (st.kind == lowering::CoreStmtKind::Break || st.kind == lowering::CoreStmtKind::Continue) {
      const std::string keyword = st.kind == lowering::CoreStmtKind::Break ? "break" : "continue";
      std::string loop_label;
      if (!parse_loop_control_statement(st.text, keyword, loop_label)) {
        diag.error("E2060", "invalid " + keyword + " statement: '" + st.text + "'");
        return false;
      }
      const LoopControlTarget* target = nullptr;
      if (loop_label.empty()) {
        if (!loop_targets.empty()) {
          target = &loop_targets.back();
        }
      } else {
        for (auto it = loop_targets.rbegin(); it != loop_targets.rend(); ++it) {
          if (it->label == loop_label) {
            target = &(*it);
            break;
          }
        }
      }
      if (target == nullptr) {
        diag.error("E2061", "no matching loop target for " + keyword +
                               (loop_label.empty() ? std::string() : " '" + loop_label + "'"));
        return false;
      }
      if (!flush_deferred()) {
        return false;
      }
      llvm::BasicBlock* jump_target =
          st.kind == lowering::CoreStmtKind::Break ? target->break_target : target->continue_target;
      if (jump_target == nullptr) {
        diag.error("E2062", "internal loop target is null for " + keyword);
        return false;
      }
      builder.CreateBr(jump_target);
      index = find_block_end(stmts, index + 1, parent_indent);
      return true;
    }

    if (st.kind == lowering::CoreStmtKind::Expr) {
      if (!emit_expression_statement(trim(st.text), st.has_expression, st.expression, builder, fn, printf_fn,
                                     printf_i32_fmt, printf_f64_fmt, printf_i64_fmt, printf_str_fmt,
                                     printf_i32_raw_fmt, printf_i64_raw_fmt,
                                     printf_f64_raw_fmt, printf_raw_str_fmt, printf_newline_fmt, variables,
                                     enum_variant_tags, struct_fields, struct_field_types, struct_instances,
                                     tuple_instances, array_instances, functions, function_returns, function_async_flags,
                                     closures, st.has_await, async_ctx, diag)) {
        return false;
      }
      ++index;
      continue;
    }

    if (st.kind == lowering::CoreStmtKind::Return) {
      llvm::Value* ret = nullptr;
      if (st.has_expression) {
        ExprValue value;
        if (!eval_with_try(st.expression, st.has_await, value)) {
          return false;
        }
        ret = cast_value_to_type(value, expected_return_type, builder);
        if (ret == nullptr && expected_return_type != ValueType::Void) {
          diag.error("E2013", "return type mismatch");
          return false;
        }
      } else if (expected_return_type != ValueType::Void) {
        if (expected_return_type == ValueType::I1) {
          ret = builder.getInt1(false);
        } else if (expected_return_type == ValueType::I64) {
          ret = builder.getInt64(0);
        } else if (expected_return_type == ValueType::F32) {
          ret = llvm::ConstantFP::get(builder.getFloatTy(), 0.0);
        } else if (expected_return_type == ValueType::F64) {
          ret = llvm::ConstantFP::get(builder.getDoubleTy(), 0.0);
        } else {
          ret = builder.getInt32(0);
        }
      }
      if (!flush_deferred()) {
        return false;
      }
      drop_owned_variables();
      if (async_ctx != nullptr && async_ctx->enabled) {
        if (expected_return_type != ValueType::Void && async_ctx->return_slot != nullptr) {
          llvm::Value* stored = ret;
          if (stored == nullptr) {
            stored = default_value_for_type(expected_return_type, builder);
          }
          builder.CreateStore(stored, async_ctx->return_slot);
        }
        builder.CreateBr(async_ctx->finalize_block);
      } else {
        if (expected_return_type == ValueType::Void) {
          builder.CreateRetVoid();
        } else {
          builder.CreateRet(ret);
        }
      }
      index = find_block_end(stmts, index + 1, parent_indent);
      return true;
    }

    if (st.kind == lowering::CoreStmtKind::If) {
      if (!st.has_expression) {
        diag.error("E2014", "if statement missing condition expression");
        return false;
      }
      const std::size_t then_start = index + 1;
      const std::size_t then_end = find_block_end(stmts, then_start, st.indent);
      const bool has_else =
          then_end < stmts.size() && stmts[then_end].kind == lowering::CoreStmtKind::Else && stmts[then_end].indent == st.indent;

      ExprValue cond_value =
          evaluate_expression(st.expression, builder, variables, enum_variant_tags, struct_fields, struct_field_types,
                              struct_instances, tuple_instances, array_instances, functions, function_returns, &closures,
                              fn, diag);
      if (st.has_await &&
          !emit_await_semantics_for_expression(st.expression, function_async_flags, async_ctx, builder, fn, diag)) {
        return false;
      }
      llvm::Value* cond = to_i1(cond_value, builder);
      if (cond == nullptr) {
        diag.error("E2015", "if condition must be bool/i32-compatible");
        return false;
      }

      auto* then_bb = llvm::BasicBlock::Create(builder.getContext(), "if.then", fn);
      auto* else_bb = has_else ? llvm::BasicBlock::Create(builder.getContext(), "if.else", fn) : nullptr;
      auto* merge_bb = llvm::BasicBlock::Create(builder.getContext(), "if.end", fn);
      builder.CreateCondBr(cond, then_bb, has_else ? else_bb : merge_bb);

      builder.SetInsertPoint(then_bb);
      index = then_start;
      if (!emit_block(stmts, enum_variant_tags, struct_fields, struct_field_types, llvm_struct_types, functions,
                      function_returns, function_async_flags,
                      expected_return_type, index, st.indent, fn, module, builder, variables, struct_instances,
                      tuple_instances, array_instances, loop_targets, closures, async_ctx, diag)) {
        return false;
      }
      if (!builder.GetInsertBlock()->getTerminator()) {
        builder.CreateBr(merge_bb);
      }

      if (has_else) {
        builder.SetInsertPoint(else_bb);
        index = then_end + 1;
        if (!emit_block(stmts, enum_variant_tags, struct_fields, struct_field_types, llvm_struct_types, functions,
                        function_returns, function_async_flags,
                        expected_return_type, index, st.indent, fn, module, builder, variables, struct_instances,
                        tuple_instances, array_instances, loop_targets, closures, async_ctx, diag)) {
          return false;
        }
        if (!builder.GetInsertBlock()->getTerminator()) {
          builder.CreateBr(merge_bb);
        }
      } else {
        index = then_end;
      }

      builder.SetInsertPoint(merge_bb);
      continue;
    }

    if (st.kind == lowering::CoreStmtKind::While) {
      if (!st.has_expression) {
        diag.error("E2016", "while statement missing condition expression");
        return false;
      }
      std::string while_label;
      std::string normalized_while;
      if (!parse_loop_header_label(st.text, "while", while_label, normalized_while)) {
        diag.error("E2063", "invalid while header: '" + st.text + "'");
        return false;
      }
      auto* cond_bb = llvm::BasicBlock::Create(builder.getContext(), "while.cond", fn);
      auto* body_bb = llvm::BasicBlock::Create(builder.getContext(), "while.body", fn);
      auto* after_bb = llvm::BasicBlock::Create(builder.getContext(), "while.end", fn);
      builder.CreateBr(cond_bb);

      builder.SetInsertPoint(cond_bb);
      ExprValue cond_value =
          evaluate_expression(st.expression, builder, variables, enum_variant_tags, struct_fields, struct_field_types,
                              struct_instances, tuple_instances, array_instances, functions, function_returns, &closures,
                              fn, diag);
      if (st.has_await &&
          !emit_await_semantics_for_expression(st.expression, function_async_flags, async_ctx, builder, fn, diag)) {
        return false;
      }
      llvm::Value* cond = to_i1(cond_value, builder);
      if (cond == nullptr) {
        diag.error("E2017", "while condition must be bool/i32-compatible");
        return false;
      }
      builder.CreateCondBr(cond, body_bb, after_bb);

      builder.SetInsertPoint(body_bb);
      loop_targets.push_back(LoopControlTarget{while_label, cond_bb, after_bb});
      ++index;
      if (!emit_block(stmts, enum_variant_tags, struct_fields, struct_field_types, llvm_struct_types, functions,
                      function_returns, function_async_flags,
                      expected_return_type, index, st.indent, fn, module, builder, variables, struct_instances,
                      tuple_instances, array_instances, loop_targets, closures, async_ctx, diag)) {
        loop_targets.pop_back();
        return false;
      }
      loop_targets.pop_back();
      if (!builder.GetInsertBlock()->getTerminator()) {
        builder.CreateBr(cond_bb);
      }
      builder.SetInsertPoint(after_bb);
      continue;
    }

    if (st.kind == lowering::CoreStmtKind::Else) {
      if (!flush_deferred()) {
        return false;
      }
      return true;
    }

    if (st.kind == lowering::CoreStmtKind::For) {
      const ForHeader header = parse_for_header(st);
      if (!header.ok) {
        diag.error("E2018", "invalid for header in backend: '" + st.text + "'");
        return false;
      }
      ExprValue start_value =
          evaluate_expression(header.start_expr, builder, variables, enum_variant_tags, struct_fields, struct_field_types,
                              struct_instances, tuple_instances, array_instances, functions, function_returns, &closures,
                              fn, diag);
      ExprValue end_value =
          evaluate_expression(header.end_expr, builder, variables, enum_variant_tags, struct_fields, struct_field_types,
                              struct_instances, tuple_instances, array_instances, functions, function_returns, &closures,
                              fn, diag);
      llvm::Value* start_i32 = to_i32(start_value, builder);
      llvm::Value* end_i32 = to_i32(end_value, builder);
      if (start_i32 == nullptr || end_i32 == nullptr) {
        diag.error("E2018", "for range bounds must be i32-compatible");
        return false;
      }

      auto previous = variables.find(header.loop_var);
      const bool had_previous = previous != variables.end();
      VariableSlot previous_slot;
      if (had_previous) {
        previous_slot = previous->second;
      }

      llvm::AllocaInst* loop_alloca = create_entry_alloca(fn, builder.getInt32Ty(), header.loop_var);
      builder.CreateStore(start_i32, loop_alloca);
      variables[header.loop_var] = VariableSlot{loop_alloca, ValueType::I32};

      auto* cond_bb = llvm::BasicBlock::Create(builder.getContext(), "for.cond", fn);
      auto* body_bb = llvm::BasicBlock::Create(builder.getContext(), "for.body", fn);
      auto* step_bb = llvm::BasicBlock::Create(builder.getContext(), "for.step", fn);
      auto* after_bb = llvm::BasicBlock::Create(builder.getContext(), "for.end", fn);
      builder.CreateBr(cond_bb);

      builder.SetInsertPoint(cond_bb);
      llvm::Value* cur_i = builder.CreateLoad(builder.getInt32Ty(), loop_alloca);
      llvm::Value* cond = builder.CreateICmpSLT(cur_i, end_i32);
      builder.CreateCondBr(cond, body_bb, after_bb);

      builder.SetInsertPoint(body_bb);
      loop_targets.push_back(LoopControlTarget{header.label, step_bb, after_bb});
      ++index;
      if (!emit_block(stmts, enum_variant_tags, struct_fields, struct_field_types, llvm_struct_types, functions,
                      function_returns, function_async_flags,
                      expected_return_type, index, st.indent, fn, module, builder, variables, struct_instances,
                      tuple_instances, array_instances, loop_targets, closures, async_ctx, diag)) {
        loop_targets.pop_back();
        return false;
      }
      loop_targets.pop_back();
      if (!builder.GetInsertBlock()->getTerminator()) {
        builder.CreateBr(step_bb);
      }

      builder.SetInsertPoint(step_bb);
      llvm::Value* loaded_i = builder.CreateLoad(builder.getInt32Ty(), loop_alloca);
      llvm::Value* next_i = builder.CreateAdd(loaded_i, builder.getInt32(1));
      builder.CreateStore(next_i, loop_alloca);
      builder.CreateBr(cond_bb);

      builder.SetInsertPoint(after_bb);
      if (had_previous) {
        variables[header.loop_var] = previous_slot;
      } else {
        variables.erase(header.loop_var);
      }
      continue;
    }

    if (st.kind == lowering::CoreStmtKind::Match) {
      if (!st.has_expression) {
        diag.error("E2018", "match statement missing expression");
        return false;
      }
      ExprValue match_expr =
          evaluate_expression(st.expression, builder, variables, enum_variant_tags, struct_fields, struct_field_types,
                              struct_instances, tuple_instances, array_instances, functions, function_returns, &closures,
                              fn, diag);
      if (st.has_await &&
          !emit_await_semantics_for_expression(st.expression, function_async_flags, async_ctx, builder, fn, diag)) {
        return false;
      }
      llvm::Value* match_i32 = to_i32(match_expr, builder);
      if (match_i32 == nullptr) {
        diag.error("E2018", "match expression must be i32/bool-compatible");
        return false;
      }

      const std::size_t match_body_start = index + 1;
      const std::size_t match_end = find_block_end(stmts, match_body_start, st.indent);
      if (match_body_start >= stmts.size() || match_body_start >= match_end) {
        diag.error("E2018", "match statement has no arms");
        return false;
      }

      auto* after_bb = llvm::BasicBlock::Create(builder.getContext(), "match.end", fn);
      llvm::BasicBlock* current_cmp_bb = builder.GetInsertBlock();
      std::size_t arm_index = match_body_start;
      bool has_any_arm = false;
      while (arm_index < match_end) {
        const lowering::CoreStmt& arm_label_stmt = stmts[arm_index];
        if (arm_label_stmt.indent <= st.indent) {
          break;
        }
        const MatchArmLabel label = parse_match_arm_label(arm_label_stmt.text, enum_variant_tags);
        if (!label.valid) {
          diag.error("E2018", "invalid match arm label: '" + arm_label_stmt.text + "'");
          return false;
        }
        has_any_arm = true;
        const std::size_t arm_body_start = arm_index + 1;
        const std::size_t arm_body_end = find_block_end(stmts, arm_body_start, arm_label_stmt.indent);

        auto* arm_bb = llvm::BasicBlock::Create(builder.getContext(), "match.arm", fn);
        llvm::BasicBlock* next_cmp_bb = nullptr;
        if (!label.wildcard) {
          next_cmp_bb = llvm::BasicBlock::Create(builder.getContext(), "match.next", fn);
        }

        builder.SetInsertPoint(current_cmp_bb);
        if (label.wildcard) {
          builder.CreateBr(arm_bb);
        } else {
          llvm::Value* lhs = match_i32;
          if (label.enum_variant) {
            lhs = extract_enum_tag_value(match_i32, builder);
          }
          llvm::Value* cmp = builder.CreateICmpEQ(lhs, builder.getInt32(label.value));
          builder.CreateCondBr(cmp, arm_bb, next_cmp_bb);
        }

        builder.SetInsertPoint(arm_bb);
        bool bound_payload = false;
        VariableSlot previous_payload_slot;
        if (label.enum_variant && !label.payload_binding.empty()) {
          auto payload_prev = variables.find(label.payload_binding);
          if (payload_prev != variables.end()) {
            previous_payload_slot = payload_prev->second;
          }
          llvm::AllocaInst* payload_alloca = create_entry_alloca(fn, builder.getInt32Ty(), label.payload_binding);
          builder.CreateStore(extract_enum_payload_value(match_i32, builder), payload_alloca);
          variables[label.payload_binding] = VariableSlot{payload_alloca, ValueType::I32};
          bound_payload = true;
        }
        std::size_t nested_index = arm_body_start;
        if (!emit_block(stmts, enum_variant_tags, struct_fields, struct_field_types, llvm_struct_types, functions,
                        function_returns, function_async_flags,
                        expected_return_type, nested_index, arm_label_stmt.indent, fn, module, builder, variables,
                        struct_instances, tuple_instances, array_instances, loop_targets, closures, async_ctx, diag)) {
          return false;
        }
        if (bound_payload) {
          if (previous_payload_slot.alloca != nullptr) {
            variables[label.payload_binding] = previous_payload_slot;
          } else {
            variables.erase(label.payload_binding);
          }
        }
        if (!builder.GetInsertBlock()->getTerminator()) {
          builder.CreateBr(after_bb);
        }

        arm_index = arm_body_end;
        if (label.wildcard) {
          current_cmp_bb = nullptr;
          break;
        }
        current_cmp_bb = next_cmp_bb;
      }

      if (!has_any_arm) {
        diag.error("E2018", "match statement has no valid arms");
        return false;
      }
      if (current_cmp_bb != nullptr) {
        builder.SetInsertPoint(current_cmp_bb);
        builder.CreateBr(after_bb);
      }
      builder.SetInsertPoint(after_bb);
      index = match_end;
      continue;
    }

    ++index;
  }
  if (!flush_deferred()) {
    return false;
  }
  return true;
}

static std::unique_ptr<llvm::Module> build_module(llvm::LLVMContext& context, const std::string& module_name,
                                                  const lowering::CoreProgram& core,
                                                  support::DiagnosticSink& diag) {
  auto module = std::make_unique<llvm::Module>(module_name, context);
  llvm::IRBuilder<> builder(context);

  std::vector<lowering::CoreFunction> functions = core.functions;
  if (functions.empty()) {
    lowering::CoreFunction legacy_main;
    legacy_main.name = "main";
    legacy_main.return_type = "i32";
    legacy_main.return_literal = core.main_return_literal;
    legacy_main.return_expression = core.main_return_expression;
    legacy_main.statements = core.main_statements;
    functions.push_back(std::move(legacy_main));
  }

  std::unordered_map<std::string, llvm::StructType*> llvm_struct_types;
  for (const auto& struct_def : core.struct_types) {
    if (struct_def.name.empty()) {
      continue;
    }
    llvm_struct_types[struct_def.name] = llvm::StructType::create(context, struct_def.name);
  }
  for (const auto& struct_def : core.struct_types) {
    auto llvm_struct_it = llvm_struct_types.find(struct_def.name);
    if (llvm_struct_it == llvm_struct_types.end() || llvm_struct_it->second == nullptr) {
      continue;
    }
    std::vector<llvm::Type*> field_types;
    field_types.reserve(struct_def.field_types.size());
    for (std::size_t i = 0; i < struct_def.field_types.size(); ++i) {
      const std::string& field_type_name = struct_def.field_types[i];
      auto nested_struct_it = llvm_struct_types.find(field_type_name);
      if (nested_struct_it != llvm_struct_types.end() && nested_struct_it->second != nullptr) {
        field_types.push_back(nested_struct_it->second);
        continue;
      }
      field_types.push_back(llvm_type_from_value_type(value_type_from_return_type(field_type_name), builder));
    }
    llvm_struct_it->second->setBody(field_types, false);
  }

  std::unordered_map<std::string, llvm::Function*> llvm_functions;
  std::unordered_map<std::string, ValueType> function_returns;
  std::unordered_map<std::string, bool> function_async_flags;
  for (const auto& ext : core.extern_functions) {
    std::vector<llvm::Type*> params;
    params.reserve(ext.param_types.size());
    for (const auto& param : ext.param_types) {
      params.push_back(llvm_type_from_value_type(value_type_from_return_type(param), builder));
    }
    llvm::FunctionType* fn_type =
        llvm::FunctionType::get(llvm_type_from_value_type(value_type_from_return_type(ext.return_type), builder),
                                params, false);
    llvm::Function* fn =
        llvm::Function::Create(fn_type, llvm::GlobalValue::ExternalLinkage, ext.name, module.get());
    llvm_functions[ext.name] = fn;
    function_returns[ext.name] = value_type_from_return_type(ext.return_type);
    function_async_flags[ext.name] = false;
  }
  for (const auto& fn_def : functions) {
    ValueType return_type = value_type_from_return_type(fn_def.return_type);
    if (fn_def.name == "main") {
      return_type = ValueType::I32;
    }
    std::vector<llvm::Type*> params;
    params.reserve(fn_def.params.size() + (fn_def.is_method ? 1 : 0));
    if (fn_def.is_method) {
      auto owner_it = llvm_struct_types.find(fn_def.owner_type);
      if (owner_it == llvm_struct_types.end() || owner_it->second == nullptr) {
        diag.error("E2026", "missing struct layout for method owner '" + fn_def.owner_type + "'");
        return nullptr;
      }
      params.push_back(builder.getPtrTy());
    }
    for (std::size_t i = 0; i < fn_def.params.size(); ++i) {
      ValueType param_type = ValueType::I32;
      if (i < fn_def.param_types.size()) {
        const ValueType annotated = value_type_from_return_type(fn_def.param_types[i]);
        if (annotated != ValueType::Invalid) {
          param_type = annotated;
        }
      }
      params.push_back(llvm_type_from_value_type(param_type, builder));
    }
    llvm::FunctionType* fn_type =
        llvm::FunctionType::get(llvm_type_from_value_type(return_type, builder), params, false);
    const auto linkage = fn_def.name == "main" ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::InternalLinkage;
    llvm::Function* fn = llvm::Function::Create(fn_type, linkage, fn_def.name, module.get());
    llvm_functions[fn_def.name] = fn;
    function_returns[fn_def.name] = return_type;
    function_async_flags[fn_def.name] = fn_def.is_async && function_contains_await(fn_def);
  }

  for (const auto& fn_def : functions) {
    llvm::Function* fn = llvm_functions[fn_def.name];
    if (fn == nullptr) {
      diag.error("E2025", "internal backend error: missing llvm function '" + fn_def.name + "'");
      return nullptr;
    }
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, fn_def.name + ".entry", fn);
    builder.SetInsertPoint(entry);
    AsyncLoweringContext async_ctx;
    async_ctx.enabled = function_async_flags[fn_def.name];
    async_ctx.expected_return_type = function_returns[fn_def.name];
    if (async_ctx.enabled) {
      fn->addFnAttr("presplitcoroutine");
      llvm::Type* ptr_ty = builder.getPtrTy();
      llvm::Value* null_ptr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty));
      llvm::Function* coro_id_decl = llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::coro_id);
      llvm::Function* coro_size_decl =
          llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::coro_size, {builder.getInt64Ty()});
      llvm::Function* coro_begin_decl = llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::coro_begin);
      llvm::Function* coro_save_decl = llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::coro_save);
      llvm::Function* coro_suspend_decl =
          llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::coro_suspend);
      llvm::Function* coro_end_decl = llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::coro_end);
      llvm::Function* coro_free_decl = llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::coro_free);
      llvm::FunctionCallee malloc_fn = module->getOrInsertFunction(
          "malloc", llvm::FunctionType::get(ptr_ty, {builder.getInt64Ty()}, false));

      async_ctx.coro_id =
          builder.CreateCall(coro_id_decl, {builder.getInt32(0), null_ptr, null_ptr, null_ptr}, "coro.id");
      llvm::Value* coro_size = builder.CreateCall(coro_size_decl, {}, "coro.size");
      llvm::Value* coro_frame = builder.CreateCall(malloc_fn, {coro_size}, "coro.frame");
      async_ctx.coro_handle = builder.CreateCall(coro_begin_decl, {async_ctx.coro_id, coro_frame}, "coro.begin");
      async_ctx.coro_save = coro_save_decl;
      async_ctx.coro_suspend = coro_suspend_decl;
      async_ctx.coro_end = coro_end_decl;
      async_ctx.coro_free = coro_free_decl;
      async_ctx.free_fn = module->getOrInsertFunction(
          "free", llvm::FunctionType::get(builder.getVoidTy(), {builder.getPtrTy()}, false));
      async_ctx.finalize_block = llvm::BasicBlock::Create(context, fn_def.name + ".async.finalize", fn);
      if (async_ctx.expected_return_type != ValueType::Void) {
        async_ctx.return_slot = create_entry_alloca(
            fn, llvm_type_from_value_type(async_ctx.expected_return_type, builder), fn_def.name + ".async.ret");
        llvm::Value* init_ret = default_value_for_type(async_ctx.expected_return_type, builder);
        if (init_ret != nullptr) {
          builder.CreateStore(init_ret, async_ctx.return_slot);
        }
      }
    }

    std::unordered_map<std::string, VariableSlot> variables;
    std::unordered_map<std::string, StructInstance> struct_instances;
    std::unordered_map<std::string, TupleInstance> tuple_instances;
    std::unordered_map<std::string, ArrayInstance> array_instances;
    std::vector<LoopControlTarget> loop_targets;
    std::unordered_map<std::string, ClosureDef> closures;

    std::size_t arg_index = 0;
    std::size_t param_index = 0;
    for (llvm::Argument& arg : fn->args()) {
      if (fn_def.is_method && arg_index == 0) {
        auto owner_it = llvm_struct_types.find(fn_def.owner_type);
        if (owner_it == llvm_struct_types.end() || owner_it->second == nullptr) {
          diag.error("E2026", "missing struct layout for method owner '" + fn_def.owner_type + "'");
          return nullptr;
        }
        struct_instances["self"] = StructInstance{&arg, owner_it->second, fn_def.owner_type};
        ++arg_index;
        continue;
      }
      if (param_index >= fn_def.params.size()) {
        break;
      }
      const std::string& param_name = fn_def.params[param_index];
      ValueType param_type = ValueType::I32;
      if (param_index < fn_def.param_types.size()) {
        const ValueType annotated = value_type_from_return_type(fn_def.param_types[param_index]);
        if (annotated != ValueType::Invalid) {
          param_type = annotated;
        }
      }
      llvm::Type* param_llvm_type = llvm_type_from_value_type(param_type, builder);
      llvm::AllocaInst* alloca = create_entry_alloca(fn, param_llvm_type, param_name);
      builder.CreateStore(&arg, alloca);
      variables[param_name] = VariableSlot{alloca, param_type};
      ++param_index;
      ++arg_index;
    }

    std::size_t index = 0;
    if (!emit_block(fn_def.statements, core.enum_variant_tags, core.struct_fields, core.struct_field_types,
                    llvm_struct_types, llvm_functions, function_returns, function_async_flags,
                    function_returns[fn_def.name], index, -1, fn,
                    module.get(), builder, variables, struct_instances, tuple_instances, array_instances, loop_targets,
                    closures, &async_ctx,
                    diag)) {
      return nullptr;
    }

    auto drop_owned_variables = [&]() {
      for (auto& [_, slot] : variables) {
        if (slot.type != ValueType::I8Ptr || slot.alloca == nullptr) {
          continue;
        }
        if (slot.ownership != VariableSlot::Ownership::Rc && slot.ownership != VariableSlot::Ownership::Arc) {
          continue;
        }
        llvm::Value* handle = builder.CreateLoad(slot.alloca->getAllocatedType(), slot.alloca);
        emit_ref_drop_call(slot.ownership, handle, fn, builder);
      }
    };

    const ValueType expected = function_returns[fn_def.name];
    if (async_ctx.enabled) {
      if (!builder.GetInsertBlock()->getTerminator()) {
        drop_owned_variables();
        if (expected != ValueType::Void && async_ctx.return_slot != nullptr) {
          llvm::Value* fallback = nullptr;
          if (!trim(fn_def.return_expression).empty()) {
            ExprValue value = evaluate_expression(fn_def.return_expression, builder, variables, core.enum_variant_tags,
                                                  core.struct_fields, core.struct_field_types, struct_instances,
                                                  tuple_instances, array_instances, llvm_functions, function_returns,
                                                  &closures, fn, diag);
            fallback = cast_value_to_type(value, expected, builder);
          }
          if (fallback == nullptr) {
            if (expected == ValueType::I32) {
              fallback = builder.getInt32(fn_def.return_literal);
            } else if (expected == ValueType::I64) {
              fallback = builder.getInt64(fn_def.return_literal);
            } else {
              fallback = default_value_for_type(expected, builder);
            }
          }
          if (fallback != nullptr) {
            builder.CreateStore(fallback, async_ctx.return_slot);
          }
        }
        builder.CreateBr(async_ctx.finalize_block);
      }

      builder.SetInsertPoint(async_ctx.finalize_block);
      if (!builder.GetInsertBlock()->getTerminator()) {
        llvm::Value* save = builder.CreateCall(async_ctx.coro_save, {async_ctx.coro_handle}, "coro.final.save");
        llvm::Value* suspend = builder.CreateCall(async_ctx.coro_suspend, {save, builder.getTrue()}, "coro.final.suspend");

        auto* final_suspend_ret_bb =
            llvm::BasicBlock::Create(context, fn_def.name + ".async.final.suspend.ret", fn);
        auto* final_cleanup_bb = llvm::BasicBlock::Create(context, fn_def.name + ".async.final.cleanup", fn);
        auto* final_resume_bb = llvm::BasicBlock::Create(context, fn_def.name + ".async.final.resume", fn);
        llvm::SwitchInst* final_sw = builder.CreateSwitch(suspend, final_resume_bb, 2);
        final_sw->addCase(builder.getInt8(1), final_cleanup_bb);
        final_sw->addCase(builder.getInt8(2), final_cleanup_bb);

        builder.SetInsertPoint(final_resume_bb);
        builder.CreateBr(final_suspend_ret_bb);

        builder.SetInsertPoint(final_cleanup_bb);
        llvm::Value* frame = builder.CreateCall(async_ctx.coro_free, {async_ctx.coro_id, async_ctx.coro_handle},
                                                "coro.final.free.frame");
        llvm::Value* has_frame = builder.CreateIsNotNull(frame, "coro.final.has.frame");
        auto* final_free_bb = llvm::BasicBlock::Create(context, fn_def.name + ".async.final.free", fn);
        auto* final_no_free_bb = llvm::BasicBlock::Create(context, fn_def.name + ".async.final.no_free", fn);
        builder.CreateCondBr(has_frame, final_free_bb, final_no_free_bb);

        builder.SetInsertPoint(final_free_bb);
        builder.CreateCall(async_ctx.free_fn, {frame});
        builder.CreateBr(final_suspend_ret_bb);

        builder.SetInsertPoint(final_no_free_bb);
        builder.CreateBr(final_suspend_ret_bb);

        builder.SetInsertPoint(final_suspend_ret_bb);
        builder.CreateCall(async_ctx.coro_end, {async_ctx.coro_handle, builder.getFalse(), save});
        if (expected == ValueType::Void) {
          builder.CreateRetVoid();
        } else {
          llvm::Value* ret_value = async_ctx.return_slot == nullptr
                                       ? default_value_for_type(expected, builder)
                                       : builder.CreateLoad(
                                             llvm_type_from_value_type(expected, builder), async_ctx.return_slot);
          if (ret_value == nullptr) {
            ret_value = default_value_for_type(expected, builder);
          }
          builder.CreateRet(ret_value);
        }
      }
    } else if (!builder.GetInsertBlock()->getTerminator()) {
      drop_owned_variables();
      if (expected == ValueType::Void) {
        builder.CreateRetVoid();
      } else {
        llvm::Value* fallback = nullptr;
        if (!trim(fn_def.return_expression).empty()) {
          ExprValue value = evaluate_expression(fn_def.return_expression, builder, variables, core.enum_variant_tags,
                                                core.struct_fields, core.struct_field_types, struct_instances,
                                                tuple_instances, array_instances, llvm_functions, function_returns,
                                                &closures, fn, diag);
          fallback = cast_value_to_type(value, expected, builder);
        }
        if (fallback == nullptr) {
          if (expected == ValueType::I1) {
            fallback = builder.getInt1(false);
          } else if (expected == ValueType::I64) {
            fallback = builder.getInt64(fn_def.return_literal);
          } else if (expected == ValueType::F32) {
            fallback = llvm::ConstantFP::get(builder.getFloatTy(), 0.0);
          } else if (expected == ValueType::F64) {
            fallback = llvm::ConstantFP::get(builder.getDoubleTy(), 0.0);
          } else {
            fallback = builder.getInt32(fn_def.return_literal);
          }
        }
        builder.CreateRet(fallback);
      }
    }
  }

  if (llvm_functions.find("main") == llvm_functions.end()) {
    llvm::FunctionType* fn_type = llvm::FunctionType::get(builder.getInt32Ty(), false);
    llvm::Function* fn = llvm::Function::Create(fn_type, llvm::GlobalValue::ExternalLinkage, "main", module.get());
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "main.entry", fn);
    builder.SetInsertPoint(entry);
    builder.CreateRet(builder.getInt32(core.main_return_literal));
  }

  return module;
}

static void run_coroutine_passes(llvm::Module& module) {
  // LLVM 21 no longer exposes legacy addCoroutinePassesToExtensionPoints; run
  // the coroutine lowering pipeline explicitly with the new PM.
  llvm::LoopAnalysisManager lam;
  llvm::FunctionAnalysisManager fam;
  llvm::CGSCCAnalysisManager cgam;
  llvm::ModuleAnalysisManager mam;
  llvm::PassBuilder pb;
  pb.registerModuleAnalyses(mam);
  pb.registerCGSCCAnalyses(cgam);
  pb.registerFunctionAnalyses(fam);
  pb.registerLoopAnalyses(lam);
  pb.crossRegisterProxies(lam, fam, cgam, mam);

  llvm::ModulePassManager mpm;
  mpm.addPass(llvm::CoroEarlyPass());
  mpm.addPass(llvm::createModuleToPostOrderCGSCCPassAdaptor(llvm::CoroSplitPass()));
  llvm::FunctionPassManager fpm;
  fpm.addPass(llvm::CoroElidePass());
  mpm.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(fpm)));
  mpm.addPass(llvm::CoroCleanupPass());
  mpm.run(module, mam);
}

static void set_module_target_triple(llvm::Module& module, const llvm::Triple& triple) {
#if LLVM_VERSION_MAJOR >= 21
  module.setTargetTriple(triple);
#else
  module.setTargetTriple(triple.getTriple());
#endif
}

static std::unique_ptr<llvm::TargetMachine> create_target_machine_compat(const llvm::Target* target,
                                                                          const llvm::Triple& triple,
                                                                          const llvm::TargetOptions& options) {
#if LLVM_VERSION_MAJOR >= 21
  return std::unique_ptr<llvm::TargetMachine>(
      target->createTargetMachine(triple, "generic", "", options, std::nullopt));
#else
  return std::unique_ptr<llvm::TargetMachine>(
      target->createTargetMachine(triple.getTriple(), "generic", "", options, std::nullopt));
#endif
}

bool LlvmEmitter::emit_llvm_ir(const lowering::CoreProgram& core, const std::string& module_name,
                               const std::string& llvm_ir_path, const std::string& target_triple,
                               support::DiagnosticSink& diag) const {
  llvm::LLVMContext context;
  auto module = build_module(context, module_name, core, diag);
  if (!module) {
    return false;
  }
  if (!target_triple.empty()) {
    set_module_target_triple(*module, llvm::Triple(target_triple));
  }
  run_coroutine_passes(*module);
  std::string verify_error;
  llvm::raw_string_ostream verify_stream(verify_error);
  if (llvm::verifyModule(*module, &verify_stream)) {
    verify_stream.flush();
    diag.error("E2001", "LLVM module verification failed: " + trim(verify_error));
    return false;
  }
  std::error_code ec;
  llvm::raw_fd_ostream out(llvm_ir_path, ec, llvm::sys::fs::OF_Text);
  if (ec) {
    diag.error("E2002", "cannot open LLVM IR output file: " + llvm_ir_path);
    return false;
  }
  module->print(out, nullptr);
  (void)core;
  return true;
}

bool LlvmEmitter::emit_object(const lowering::CoreProgram& core, const std::string& module_name,
                              const std::string& object_path, const std::string& target_triple,
                              support::DiagnosticSink& diag) const {
  if (llvm::InitializeNativeTarget()) {
    diag.error("E2003", "cannot initialize native LLVM target");
    return false;
  }
  if (llvm::InitializeNativeTargetAsmParser()) {
    diag.error("E2003", "cannot initialize native LLVM asm parser");
    return false;
  }
  if (llvm::InitializeNativeTargetAsmPrinter()) {
    diag.error("E2003", "cannot initialize native LLVM asm printer");
    return false;
  }

  // Keep one-command cross-compile available for primary Linux targets.
  LLVMInitializeAArch64TargetInfo();
  LLVMInitializeAArch64Target();
  LLVMInitializeAArch64TargetMC();
  LLVMInitializeAArch64AsmParser();
  LLVMInitializeAArch64AsmPrinter();

  LLVMInitializeARMTargetInfo();
  LLVMInitializeARMTarget();
  LLVMInitializeARMTargetMC();
  LLVMInitializeARMAsmParser();
  LLVMInitializeARMAsmPrinter();

  LLVMInitializeX86TargetInfo();
  LLVMInitializeX86Target();
  LLVMInitializeX86TargetMC();
  LLVMInitializeX86AsmParser();
  LLVMInitializeX86AsmPrinter();

  llvm::LLVMContext context;
  auto module = build_module(context, module_name, core, diag);
  if (!module) {
    return false;
  }

  std::string error;
  const std::string triple_value = target_triple.empty() ? llvm::sys::getDefaultTargetTriple() : target_triple;
  const llvm::Triple triple(triple_value);
  set_module_target_triple(*module, triple);

  const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple.getTriple(), error);
  if (!target) {
    diag.error("E2003", "cannot resolve LLVM target: " + error);
    return false;
  }

  llvm::TargetOptions options;
  std::unique_ptr<llvm::TargetMachine> target_machine(create_target_machine_compat(target, triple, options));
  if (!target_machine) {
    diag.error("E2004", "cannot create LLVM target machine");
    return false;
  }
  module->setDataLayout(target_machine->createDataLayout());
  run_coroutine_passes(*module);

  std::string verify_error;
  llvm::raw_string_ostream verify_stream(verify_error);
  if (llvm::verifyModule(*module, &verify_stream)) {
    verify_stream.flush();
    diag.error("E2001", "LLVM module verification failed: " + trim(verify_error));
    return false;
  }

  std::error_code ec;
  llvm::raw_fd_ostream obj_file(object_path, ec, llvm::sys::fs::OF_None);
  if (ec) {
    diag.error("E2005", "cannot open object output file: " + object_path);
    return false;
  }

  llvm::legacy::PassManager pass;
  if (target_machine->addPassesToEmitFile(pass, obj_file, nullptr, llvm::CodeGenFileType::ObjectFile)) {
    diag.error("E2006", "target does not support object emission");
    return false;
  }
  pass.run(*module);
  obj_file.flush();
  (void)core;
  return true;
}

}  // namespace thagc::codegen
