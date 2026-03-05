#include "thagc/middleend/mir_lowering.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "thagc/frontend/types.hpp"

namespace thagc::middleend {

namespace {

enum class OwnershipQualifier {
  None,
  Own,
  Ref,
  Mut,
};

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

static OwnershipQualifier parse_ownership_qualifier(std::string text, std::string& stripped) {
  text = trim_copy(text);
  if (text.size() > 4 && text.compare(0, 4, "own ") == 0) {
    stripped = trim_copy(text.substr(4));
    return OwnershipQualifier::Own;
  }
  if (text.size() > 4 && text.compare(0, 4, "ref ") == 0) {
    stripped = trim_copy(text.substr(4));
    return OwnershipQualifier::Ref;
  }
  if (text.size() > 4 && text.compare(0, 4, "mut ") == 0) {
    stripped = trim_copy(text.substr(4));
    return OwnershipQualifier::Mut;
  }
  stripped = text;
  return OwnershipQualifier::None;
}

static semantics::TypeKind parse_coarse_type(const std::string& type_text) {
  std::string stripped;
  parse_ownership_qualifier(type_text, stripped);
  const std::string clean = trim_copy(stripped);
  if (clean == "i64") {
    return semantics::TypeKind::I64;
  }
  if (clean == "f32") {
    return semantics::TypeKind::F32;
  }
  if (clean == "f64") {
    return semantics::TypeKind::F64;
  }
  if (clean == "bool") {
    return semantics::TypeKind::Bool;
  }
  if (clean == "string" || clean == "String") {
    return semantics::TypeKind::String;
  }
  if (clean == "ptr") {
    return semantics::TypeKind::Ptr;
  }
  if (clean == "Option" || clean.rfind("Option<", 0) == 0) {
    return semantics::TypeKind::Option;
  }
  if (clean == "Result" || clean.rfind("Result<", 0) == 0) {
    return semantics::TypeKind::Result;
  }
  if (clean == "Rc" || clean.rfind("Rc<", 0) == 0) {
    return semantics::TypeKind::Rc;
  }
  if (clean == "Arc" || clean.rfind("Arc<", 0) == 0) {
    return semantics::TypeKind::Arc;
  }
  if (clean == "void") {
    return semantics::TypeKind::Void;
  }
  return semantics::TypeKind::I32;
}

static std::string parse_simple_identifier(const std::string& text) {
  const std::string clean = trim_copy(text);
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

static std::vector<std::string> extract_identifiers(const std::string& text) {
  std::vector<std::string> out;
  bool in_string = false;
  bool escaping = false;
  for (std::size_t i = 0; i < text.size();) {
    const char ch = text[i];
    if (in_string) {
      if (escaping) {
        escaping = false;
      } else if (ch == '\\') {
        escaping = true;
      } else if (ch == '"') {
        in_string = false;
      }
      ++i;
      continue;
    }
    if (ch == '"') {
      in_string = true;
      ++i;
      continue;
    }
    if (!is_ident_start(ch)) {
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

static std::string parse_let_name(const std::string& line) {
  const std::string clean = trim_copy(line);
  if (clean.rfind("let ", 0) != 0) {
    return "";
  }
  std::size_t i = 4;
  while (i < clean.size() && std::isspace(static_cast<unsigned char>(clean[i]))) {
    ++i;
  }
  if (i >= clean.size() || !is_ident_start(clean[i])) {
    return "";
  }
  const std::size_t start = i;
  while (i < clean.size() && is_ident_body(clean[i])) {
    ++i;
  }
  return clean.substr(start, i - start);
}

static std::string parse_let_annotation(const std::string& line) {
  const std::string clean = trim_copy(line);
  if (clean.rfind("let ", 0) != 0) {
    return "";
  }
  const std::size_t eq = clean.find('=');
  const std::size_t colon = clean.find(':');
  if (colon == std::string::npos || eq == std::string::npos || colon > eq) {
    return "";
  }
  return trim_copy(clean.substr(colon + 1, eq - colon - 1));
}

static std::string parse_assignment_target(const std::string& line) {
  const std::size_t eq = line.find('=');
  if (eq == std::string::npos) {
    return "";
  }
  const std::string lhs = trim_copy(line.substr(0, eq));
  if (lhs.empty() || !is_ident_start(lhs[0])) {
    return "";
  }
  for (std::size_t i = 1; i < lhs.size(); ++i) {
    if (!is_ident_body(lhs[i])) {
      return "";
    }
  }
  return lhs;
}

static mir::MirRvalue make_constant_rvalue(const std::string& text) {
  mir::MirRvalue out;
  out.kind = mir::MirRvalueKind::Use;
  out.operand.kind = mir::MirOperandKind::Constant;
  out.operand.text = text;
  return out;
}

static void emit_eval_identifier_use(const std::string& expr, int line, const std::optional<syntax::Span>& span,
                                     const std::unordered_map<std::string, std::uint32_t>& locals,
                                     std::vector<mir::MirStatement>& out) {
  const std::vector<std::string> idents = extract_identifiers(expr);
  std::unordered_set<std::string> seen;
  for (const std::string& ident : idents) {
    if (ident == "let" || ident == "return" || ident == "if" || ident == "else" || ident == "while" || ident == "for" ||
        ident == "match" || ident == "true" || ident == "false" || ident == "spawn" || ident == "open" || ident == "read" ||
        ident == "write" || ident == "close") {
      continue;
    }
    if (!seen.insert(ident).second) {
      continue;
    }
    auto it = locals.find(ident);
    if (it == locals.end()) {
      continue;
    }
    mir::MirStatement eval;
    eval.kind = mir::MirStatementKind::Eval;
    eval.line = line;
    eval.span = span;
    eval.text = ident;
    eval.rhs.kind = mir::MirRvalueKind::Use;
    eval.rhs.operand.kind = mir::MirOperandKind::Copy;
    eval.rhs.operand.local = it->second;
    eval.rhs.operand.text = ident;
    out.push_back(std::move(eval));
  }
}

}  // namespace

mir::MirBody lower_function_to_mir(const syntax::AstFunction& fn) {
  mir::MirBody body;
  body.function_name = fn.name;
  body.blocks.push_back(mir::MirBasicBlock{});
  auto& block = body.blocks.back();

  std::unordered_map<std::string, std::uint32_t> locals;

  auto add_local = [&](const std::string& name, const std::string& annotation) -> std::uint32_t {
    std::string stripped;
    const OwnershipQualifier qual = parse_ownership_qualifier(annotation, stripped);
    mir::MirLocal local;
    local.id = static_cast<std::uint32_t>(body.locals.size());
    local.name = name;
    local.ty = ty::from_type_kind(parse_coarse_type(stripped));
    local.is_mut = qual == OwnershipQualifier::Mut;
    local.is_owned = qual == OwnershipQualifier::Own;
    body.locals.push_back(local);
    locals[name] = local.id;
    return local.id;
  };

  for (std::size_t i = 0; i < fn.params.size(); ++i) {
    const std::string annotation = i < fn.param_types.size() ? fn.param_types[i] : "";
    add_local(fn.params[i], annotation);
  }

  for (const auto& st : fn.body) {
    if (st.kind == syntax::StatementKind::Let) {
      const std::string name = parse_let_name(st.text);
      if (name.empty()) {
        continue;
      }
      const std::string annotation = parse_let_annotation(st.text);
      std::string stripped_ann;
      const OwnershipQualifier qual = parse_ownership_qualifier(annotation, stripped_ann);
      const std::uint32_t lhs_id = add_local(name, annotation);
      if (!st.has_expression || !st.expression_valid) {
        continue;
      }
      const std::string rhs_ident = parse_simple_identifier(st.expression_normalized);
      mir::MirStatement assign;
      assign.kind = mir::MirStatementKind::Assign;
      assign.line = st.line;
      assign.span = st.span;
      assign.text = st.text;
      assign.lhs = mir::MirPlace{lhs_id, {}};
      assign.rhs.kind = mir::MirRvalueKind::Use;
      if (!rhs_ident.empty()) {
        auto src = locals.find(rhs_ident);
        if (src != locals.end()) {
          assign.rhs.operand.local = src->second;
          assign.rhs.operand.text = rhs_ident;
          if (qual == OwnershipQualifier::Ref) {
            assign.rhs.operand.kind = mir::MirOperandKind::Ref;
          } else if (qual == OwnershipQualifier::Mut) {
            assign.rhs.operand.kind = mir::MirOperandKind::MutRef;
          } else {
            assign.rhs.operand.kind = mir::MirOperandKind::Move;
          }
        } else {
          assign.rhs = make_constant_rvalue(st.expression_normalized);
        }
      } else {
        emit_eval_identifier_use(st.expression_normalized, st.line, st.span, locals, block.statements);
        assign.rhs = make_constant_rvalue(st.expression_normalized);
      }
      block.statements.push_back(std::move(assign));
      continue;
    }

    if (st.kind == syntax::StatementKind::Assign) {
      const std::string target = parse_assignment_target(st.text);
      auto dst = locals.find(target);
      if (dst == locals.end() || !st.has_expression || !st.expression_valid) {
        continue;
      }
      const std::string rhs_ident = parse_simple_identifier(st.expression_normalized);
      mir::MirStatement assign;
      assign.kind = mir::MirStatementKind::Assign;
      assign.line = st.line;
      assign.span = st.span;
      assign.text = st.text;
      assign.lhs = mir::MirPlace{dst->second, {}};
      assign.rhs.kind = mir::MirRvalueKind::Use;
      if (!rhs_ident.empty()) {
        auto src = locals.find(rhs_ident);
        if (src != locals.end()) {
          assign.rhs.operand.local = src->second;
          assign.rhs.operand.text = rhs_ident;
          assign.rhs.operand.kind = mir::MirOperandKind::Move;
        } else {
          assign.rhs = make_constant_rvalue(st.expression_normalized);
        }
      } else {
        emit_eval_identifier_use(st.expression_normalized, st.line, st.span, locals, block.statements);
        assign.rhs = make_constant_rvalue(st.expression_normalized);
      }
      block.statements.push_back(std::move(assign));
      continue;
    }

    if (st.kind == syntax::StatementKind::Return) {
      if (!st.has_expression || !st.expression_valid) {
        continue;
      }
      const std::string rhs_ident = parse_simple_identifier(st.expression_normalized);
      mir::MirStatement eval;
      eval.kind = mir::MirStatementKind::Eval;
      eval.line = st.line;
      eval.span = st.span;
      eval.text = st.text;
      eval.rhs.kind = mir::MirRvalueKind::Use;
      if (!rhs_ident.empty()) {
        auto src = locals.find(rhs_ident);
        if (src != locals.end()) {
          eval.rhs.operand.local = src->second;
          eval.rhs.operand.text = rhs_ident;
          eval.rhs.operand.kind = mir::MirOperandKind::Move;
        } else {
          eval.rhs = make_constant_rvalue(st.expression_normalized);
        }
      } else {
        emit_eval_identifier_use(st.expression_normalized, st.line, st.span, locals, block.statements);
        eval.rhs = make_constant_rvalue(st.expression_normalized);
      }
      block.statements.push_back(std::move(eval));
      continue;
    }

    if (st.has_expression && st.expression_valid) {
      emit_eval_identifier_use(st.expression_normalized, st.line, st.span, locals, block.statements);
    }
  }

  block.term.kind = mir::MirTerminatorKind::Unreachable;
  return body;
}

}  // namespace thagc::middleend
