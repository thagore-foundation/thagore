#include "thagc/backend/llvm_emitter.hpp"

#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/TargetParser/Host.h>

namespace thagc::codegen {

enum class ExprTokKind {
  Atom,
  Op,
  LParen,
  RParen,
  End,
};

struct ExprTok {
  ExprTokKind kind = ExprTokKind::End;
  std::string text;
};

enum class ValueType {
  I32,
  I1,
  I8Ptr,
  Invalid,
};

struct ExprValue {
  llvm::Value* value = nullptr;
  ValueType type = ValueType::Invalid;
};

struct VariableSlot {
  llvm::AllocaInst* alloca = nullptr;
  ValueType type = ValueType::Invalid;
};

struct ExprCursor {
  std::vector<ExprTok> tokens;
  std::size_t index = 0;
  std::string error;
  llvm::IRBuilder<>* builder = nullptr;
  std::unordered_map<std::string, VariableSlot>* variables = nullptr;
  const std::unordered_map<std::string, int>* enum_variant_tags = nullptr;
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

static llvm::AllocaInst* create_entry_alloca(llvm::Function* fn, llvm::Type* type, const std::string& name) {
  llvm::IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().begin());
  return tmp.CreateAlloca(type, nullptr, name);
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

static bool is_string_atom(const std::string& text) {
  return text.size() >= 2 && text.front() == '"' && text.back() == '"';
}

static std::vector<ExprTok> tokenize_expression(const std::string& text, std::string& error) {
  std::vector<ExprTok> out;
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
      while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
        ++i;
      }
      out.push_back(ExprTok{ExprTokKind::Atom, text.substr(start, i - start)});
      continue;
    }
    if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
      const std::size_t start = i;
      while (i < text.size() &&
             (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_')) {
        ++i;
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

static ExprValue parse_expression_equality(ExprCursor& cursor);

static ExprValue parse_primary(ExprCursor& cursor) {
  const ExprTok& tok = cur(cursor);
  if (tok.kind == ExprTokKind::Op && tok.text == "-") {
    ++cursor.index;
    ExprValue rhs = parse_primary(cursor);
    if (rhs.type != ValueType::I32 || rhs.value == nullptr) {
      cursor.error = cursor.error.empty() ? "unary '-' requires i32 operand" : cursor.error;
      return {};
    }
    return ExprValue{cursor.builder->CreateNeg(rhs.value), ValueType::I32};
  }

  if (tok.kind == ExprTokKind::Atom) {
    ++cursor.index;
    if (tok.text == "true") {
      return ExprValue{cursor.builder->getInt1(true), ValueType::I1};
    }
    if (tok.text == "false") {
      return ExprValue{cursor.builder->getInt1(false), ValueType::I1};
    }
    if (is_integer_atom(tok.text)) {
      return ExprValue{cursor.builder->getInt32(std::stoi(tok.text)), ValueType::I32};
    }
    if (is_string_atom(tok.text)) {
      const std::string text = tok.text.substr(1, tok.text.size() - 2);
      return ExprValue{cursor.builder->CreateGlobalStringPtr(text), ValueType::I8Ptr};
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
        return ExprValue{cursor.builder->getInt32(variant_it->second), ValueType::I32};
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
    if (lhs.type != ValueType::I32 || rhs.type != ValueType::I32 || rhs.value == nullptr) {
      cursor.error = "operator '" + op + "' requires i32 operands";
      return {};
    }
    lhs.value = (op == "*") ? cursor.builder->CreateMul(lhs.value, rhs.value)
                            : cursor.builder->CreateSDiv(lhs.value, rhs.value);
    lhs.type = ValueType::I32;
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
    if (lhs.type != ValueType::I32 || rhs.type != ValueType::I32 || rhs.value == nullptr) {
      cursor.error = "operator '" + op + "' requires i32 operands";
      return {};
    }
    lhs.value = (op == "+") ? cursor.builder->CreateAdd(lhs.value, rhs.value)
                            : cursor.builder->CreateSub(lhs.value, rhs.value);
    lhs.type = ValueType::I32;
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
    if (lhs.type != ValueType::I32 || rhs.type != ValueType::I32 || rhs.value == nullptr) {
      cursor.error = "comparison operator '" + op + "' requires i32 operands";
      return {};
    }
    if (op == "<") {
      lhs.value = cursor.builder->CreateICmpSLT(lhs.value, rhs.value);
    } else if (op == "<=") {
      lhs.value = cursor.builder->CreateICmpSLE(lhs.value, rhs.value);
    } else if (op == ">") {
      lhs.value = cursor.builder->CreateICmpSGT(lhs.value, rhs.value);
    } else {
      lhs.value = cursor.builder->CreateICmpSGE(lhs.value, rhs.value);
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
  while (cur(cursor).kind == ExprTokKind::Op && (cur(cursor).text == "==" || cur(cursor).text == "!=")) {
    const std::string op = cur(cursor).text;
    ++cursor.index;
    ExprValue rhs = parse_comparison(cursor);
    if (rhs.type != lhs.type || rhs.value == nullptr) {
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
                                     support::DiagnosticSink& diag) {
  const std::string clean = trim(expr_text);
  if (clean.empty()) {
    return ExprValue{builder.getInt32(0), ValueType::I32};
  }
  if (expr_text.empty()) {
    return ExprValue{builder.getInt32(0), ValueType::I32};
  }
  std::string tok_error;
  ExprCursor cursor;
  cursor.tokens = tokenize_expression(clean, tok_error);
  cursor.builder = &builder;
  cursor.variables = &variables;
  cursor.enum_variant_tags = &enum_variant_tags;
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

static llvm::Value* to_i32(ExprValue value, llvm::IRBuilder<>& builder) {
  if (value.value == nullptr) {
    return nullptr;
  }
  if (value.type == ValueType::I32) {
    return value.value;
  }
  if (value.type == ValueType::I1) {
    return builder.CreateZExt(value.value, builder.getInt32Ty());
  }
  return nullptr;
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
  return nullptr;
}

static std::size_t find_block_end(const std::vector<lowering::CoreStmt>& stmts, std::size_t start_index,
                                  int parent_indent) {
  std::size_t i = start_index;
  while (i < stmts.size() && stmts[i].indent > parent_indent) {
    ++i;
  }
  return i;
}

struct ForHeader {
  bool ok = false;
  std::string loop_var;
  std::string start_expr;
  std::string end_expr;
};

static ForHeader parse_for_header(const lowering::CoreStmt& st) {
  ForHeader out;
  std::string line = trim(st.text);
  if (!starts_with(line, "for ") || !ends_with(line, ":")) {
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
  const auto variant_it = enum_variant_tags.find(text);
  if (variant_it != enum_variant_tags.end()) {
    out.valid = true;
    out.value = variant_it->second;
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
  out.valid = true;
  const int parsed = std::stoi(text.substr(i));
  out.value = neg ? -parsed : parsed;
  return out;
}

static bool emit_block(const lowering::CoreProgram& core, std::size_t& index, int parent_indent, llvm::Function* fn,
                       llvm::Module* module, llvm::IRBuilder<>& builder,
                       std::unordered_map<std::string, VariableSlot>& variables, support::DiagnosticSink& diag) {
  llvm::FunctionCallee printf_fn = module->getOrInsertFunction(
      "printf",
      llvm::FunctionType::get(builder.getInt32Ty(), llvm::PointerType::getUnqual(builder.getInt8Ty()), true));
  llvm::Value* printf_i32_fmt = builder.CreateGlobalStringPtr("%d\n");
  llvm::Value* printf_str_fmt = builder.CreateGlobalStringPtr("%s\n");

  const auto& stmts = core.main_statements;
  while (index < stmts.size() && stmts[index].indent > parent_indent) {
    const lowering::CoreStmt& st = stmts[index];
    if (st.kind == lowering::CoreStmtKind::Let) {
      const std::string name = parse_let_name(st.text);
      if (name.empty() || !st.has_expression) {
        diag.error("E2010", "invalid let statement in backend: '" + st.text + "'");
        return false;
      }
      ExprValue value = evaluate_expression(st.expression, builder, variables, core.enum_variant_tags, diag);
      if (value.value == nullptr || value.type == ValueType::Invalid) {
        return false;
      }
      auto it = variables.find(name);
      if (it == variables.end()) {
        llvm::Type* type = (value.type == ValueType::I1) ? builder.getInt1Ty() : builder.getInt32Ty();
        llvm::AllocaInst* alloca = create_entry_alloca(fn, type, name);
        variables[name] = VariableSlot{alloca, value.type};
        it = variables.find(name);
      }
      if (it->second.type != value.type) {
        diag.error("E2011", "type change for variable '" + name + "' is not supported in backend");
        return false;
      }
      builder.CreateStore(value.value, it->second.alloca);
      ++index;
      continue;
    }

    if (st.kind == lowering::CoreStmtKind::Expr) {
      const std::string line = trim(st.text);
      if (starts_with(line, "print(") && ends_with(line, ")")) {
        std::string inner = st.has_expression ? st.expression : trim(line.substr(6, line.size() - 7));
        ExprValue value = evaluate_expression(inner, builder, variables, core.enum_variant_tags, diag);
        if (value.type == ValueType::I8Ptr) {
          builder.CreateCall(printf_fn, {printf_str_fmt, value.value});
        } else {
          llvm::Value* as_i32 = to_i32(value, builder);
          if (as_i32 == nullptr) {
            diag.error("E2012", "print() expects i32/bool/string-compatible expression");
            return false;
          }
          builder.CreateCall(printf_fn, {printf_i32_fmt, as_i32});
        }
      } else if (st.has_expression) {
        ExprValue value = evaluate_expression(st.expression, builder, variables, core.enum_variant_tags, diag);
        if (value.value == nullptr) {
          return false;
        }
      }
      ++index;
      continue;
    }

    if (st.kind == lowering::CoreStmtKind::Return) {
      llvm::Value* ret = builder.getInt32(0);
      if (st.has_expression) {
        ExprValue value = evaluate_expression(st.expression, builder, variables, core.enum_variant_tags, diag);
        ret = to_i32(value, builder);
        if (ret == nullptr) {
          diag.error("E2013", "return expects i32/bool-compatible expression");
          return false;
        }
      }
      builder.CreateRet(ret);
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

      ExprValue cond_value = evaluate_expression(st.expression, builder, variables, core.enum_variant_tags, diag);
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
      if (!emit_block(core, index, st.indent, fn, module, builder, variables, diag)) {
        return false;
      }
      if (!builder.GetInsertBlock()->getTerminator()) {
        builder.CreateBr(merge_bb);
      }

      if (has_else) {
        builder.SetInsertPoint(else_bb);
        index = then_end + 1;
        if (!emit_block(core, index, st.indent, fn, module, builder, variables, diag)) {
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
      auto* cond_bb = llvm::BasicBlock::Create(builder.getContext(), "while.cond", fn);
      auto* body_bb = llvm::BasicBlock::Create(builder.getContext(), "while.body", fn);
      auto* after_bb = llvm::BasicBlock::Create(builder.getContext(), "while.end", fn);
      builder.CreateBr(cond_bb);

      builder.SetInsertPoint(cond_bb);
      ExprValue cond_value = evaluate_expression(st.expression, builder, variables, core.enum_variant_tags, diag);
      llvm::Value* cond = to_i1(cond_value, builder);
      if (cond == nullptr) {
        diag.error("E2017", "while condition must be bool/i32-compatible");
        return false;
      }
      builder.CreateCondBr(cond, body_bb, after_bb);

      builder.SetInsertPoint(body_bb);
      ++index;
      if (!emit_block(core, index, st.indent, fn, module, builder, variables, diag)) {
        return false;
      }
      if (!builder.GetInsertBlock()->getTerminator()) {
        builder.CreateBr(cond_bb);
      }
      builder.SetInsertPoint(after_bb);
      continue;
    }

    if (st.kind == lowering::CoreStmtKind::Else) {
      return true;
    }

    if (st.kind == lowering::CoreStmtKind::For) {
      const ForHeader header = parse_for_header(st);
      if (!header.ok) {
        diag.error("E2018", "invalid for header in backend: '" + st.text + "'");
        return false;
      }
      ExprValue start_value =
          evaluate_expression(header.start_expr, builder, variables, core.enum_variant_tags, diag);
      ExprValue end_value = evaluate_expression(header.end_expr, builder, variables, core.enum_variant_tags, diag);
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
      ++index;
      if (!emit_block(core, index, st.indent, fn, module, builder, variables, diag)) {
        return false;
      }
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
      ExprValue match_expr = evaluate_expression(st.expression, builder, variables, core.enum_variant_tags, diag);
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
        const MatchArmLabel label = parse_match_arm_label(arm_label_stmt.text, core.enum_variant_tags);
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
          llvm::Value* cmp = builder.CreateICmpEQ(match_i32, builder.getInt32(label.value));
          builder.CreateCondBr(cmp, arm_bb, next_cmp_bb);
        }

        builder.SetInsertPoint(arm_bb);
        std::size_t nested_index = arm_body_start;
        if (!emit_block(core, nested_index, arm_label_stmt.indent, fn, module, builder, variables, diag)) {
          return false;
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
  return true;
}

static std::unique_ptr<llvm::Module> build_module(llvm::LLVMContext& context, const std::string& module_name,
                                                  const lowering::CoreProgram& core,
                                                  support::DiagnosticSink& diag) {
  auto module = std::make_unique<llvm::Module>(module_name, context);
  llvm::IRBuilder<> builder(context);

  auto* fn_type = llvm::FunctionType::get(builder.getInt32Ty(), false);
  auto* fn = llvm::Function::Create(fn_type, llvm::GlobalValue::ExternalLinkage, "main", module.get());
  auto* entry = llvm::BasicBlock::Create(context, "entry", fn);
  builder.SetInsertPoint(entry);
  std::unordered_map<std::string, VariableSlot> variables;

  if (!core.main_statements.empty()) {
    std::size_t index = 0;
    if (!emit_block(core, index, -1, fn, module.get(), builder, variables, diag)) {
      return nullptr;
    }
    if (!builder.GetInsertBlock()->getTerminator()) {
      llvm::Value* fallback = builder.getInt32(core.main_return_literal);
      if (!trim(core.main_return_expression).empty()) {
        ExprValue value =
            evaluate_expression(core.main_return_expression, builder, variables, core.enum_variant_tags, diag);
        llvm::Value* as_i32 = to_i32(value, builder);
        if (as_i32 != nullptr) {
          fallback = as_i32;
        }
      }
      builder.CreateRet(fallback);
    }
  } else {
    ExprValue value =
        evaluate_expression(core.main_return_expression, builder, variables, core.enum_variant_tags, diag);
    llvm::Value* ret_value = to_i32(value, builder);
    if (ret_value == nullptr) {
      ret_value = builder.getInt32(core.main_return_literal);
    }
    builder.CreateRet(ret_value);
  }

  return module;
}

bool LlvmEmitter::emit_llvm_ir(const lowering::CoreProgram& core, const std::string& module_name,
                               const std::string& llvm_ir_path, support::DiagnosticSink& diag) const {
  llvm::LLVMContext context;
  auto module = build_module(context, module_name, core, diag);
  if (!module) {
    return false;
  }
  if (llvm::verifyModule(*module, &llvm::errs())) {
    diag.error("E2001", "LLVM module verification failed");
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
                              const std::string& object_path, support::DiagnosticSink& diag) const {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  llvm::LLVMContext context;
  auto module = build_module(context, module_name, core, diag);
  if (!module) {
    return false;
  }

  std::string error;
  const llvm::Triple target_triple(llvm::sys::getDefaultTargetTriple());
  module->setTargetTriple(target_triple);

  const llvm::Target* target = llvm::TargetRegistry::lookupTarget(target_triple.getTriple(), error);
  if (!target) {
    diag.error("E2003", "cannot resolve LLVM target: " + error);
    return false;
  }

  llvm::TargetOptions options;
  std::unique_ptr<llvm::TargetMachine> target_machine(
      target->createTargetMachine(target_triple, "generic", "", options, std::nullopt));
  if (!target_machine) {
    diag.error("E2004", "cannot create LLVM target machine");
    return false;
  }
  module->setDataLayout(target_machine->createDataLayout());

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
