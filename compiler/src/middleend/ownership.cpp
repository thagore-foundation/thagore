#include "thagc/middleend/ownership.hpp"

#include <cctype>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace thagc::middleend {

namespace {

enum class OwnershipQualifier {
  None,
  Own,
  Ref,
  Mut,
};

enum class BorrowKind {
  None,
  Immutable,
  Mutable,
};

struct LocalState {
  bool owned = false;
  bool moved = false;
  int immut_borrows = 0;
  bool mut_borrowed = false;
  std::optional<std::string> borrow_from;
  BorrowKind borrow_kind = BorrowKind::None;
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

static OwnershipQualifier parse_ownership_qualifier(const std::string& type_text, std::string& stripped) {
  const std::string clean = trim_copy(type_text);
  if (clean.size() > 4 && clean.compare(0, 4, "own ") == 0) {
    stripped = trim_copy(clean.substr(4));
    return OwnershipQualifier::Own;
  }
  if (clean.size() > 4 && clean.compare(0, 4, "ref ") == 0) {
    stripped = trim_copy(clean.substr(4));
    return OwnershipQualifier::Ref;
  }
  if (clean.size() > 4 && clean.compare(0, 4, "mut ") == 0) {
    stripped = trim_copy(clean.substr(4));
    return OwnershipQualifier::Mut;
  }
  stripped = clean;
  return OwnershipQualifier::None;
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

static std::string parse_let_rhs(const std::string& line) {
  const std::size_t eq = line.find('=');
  if (eq == std::string::npos || eq + 1 >= line.size()) {
    return "";
  }
  return trim_copy(line.substr(eq + 1));
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

static std::string parse_assignment_rhs(const std::string& line) {
  const std::size_t eq = line.find('=');
  if (eq == std::string::npos || eq + 1 >= line.size()) {
    return "";
  }
  return trim_copy(line.substr(eq + 1));
}

static std::string parse_return_expr(const std::string& line) {
  const std::string clean = trim_copy(line);
  if (clean.rfind("return", 0) != 0) {
    return "";
  }
  if (clean.size() <= 6) {
    return "";
  }
  return trim_copy(clean.substr(6));
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

static bool emit_ownership_error(const std::string& code, int line, const std::string& message,
                                 support::DiagnosticSink& diag) {
  diag.error(code, "line " + std::to_string(line) + ": " + message);
  return false;
}

static void release_borrow_binding(const std::string& name, std::unordered_map<std::string, LocalState>& states) {
  auto it = states.find(name);
  if (it == states.end()) {
    return;
  }
  LocalState& local = it->second;
  if (!local.borrow_from.has_value()) {
    local.borrow_kind = BorrowKind::None;
    return;
  }
  auto src = states.find(*local.borrow_from);
  if (src != states.end()) {
    if (local.borrow_kind == BorrowKind::Immutable) {
      if (src->second.immut_borrows > 0) {
        --src->second.immut_borrows;
      }
    } else if (local.borrow_kind == BorrowKind::Mutable) {
      src->second.mut_borrowed = false;
    }
  }
  local.borrow_from.reset();
  local.borrow_kind = BorrowKind::None;
}

static bool ensure_not_moved(const std::string& name, int line, const std::unordered_map<std::string, LocalState>& states,
                             support::DiagnosticSink& diag) {
  auto it = states.find(name);
  if (it == states.end()) {
    return true;
  }
  if (it->second.owned && it->second.moved) {
    return emit_ownership_error("E_MOVE_USE_AFTER_MOVE", line,
                                "use of moved value '" + name + "' after ownership transfer", diag);
  }
  return true;
}

static bool consume_move(const std::string& source, int line, std::unordered_map<std::string, LocalState>& states,
                         support::DiagnosticSink& diag) {
  auto src = states.find(source);
  if (src == states.end() || !src->second.owned) {
    return true;
  }
  if (src->second.moved) {
    return emit_ownership_error("E_MOVE_USE_AFTER_MOVE", line,
                                "use of moved value '" + source + "' after ownership transfer", diag);
  }
  if (src->second.immut_borrows > 0 || src->second.mut_borrowed) {
    return emit_ownership_error("E_BORROW_MOVE_CONFLICT", line,
                                "cannot move '" + source + "' while it is borrowed", diag);
  }
  src->second.moved = true;
  return true;
}

static bool mark_immutable_borrow(const std::string& source, int line, std::unordered_map<std::string, LocalState>& states,
                                  support::DiagnosticSink& diag) {
  auto src = states.find(source);
  if (src == states.end()) {
    return true;
  }
  if (!ensure_not_moved(source, line, states, diag)) {
    return false;
  }
  if (src->second.mut_borrowed) {
    return emit_ownership_error("E_BORROW_CONFLICT", line,
                                "cannot take immutable borrow of '" + source + "' while mutable borrow is active", diag);
  }
  ++src->second.immut_borrows;
  return true;
}

static bool mark_mutable_borrow(const std::string& source, int line, std::unordered_map<std::string, LocalState>& states,
                                support::DiagnosticSink& diag) {
  auto src = states.find(source);
  if (src == states.end()) {
    return true;
  }
  if (!ensure_not_moved(source, line, states, diag)) {
    return false;
  }
  if (src->second.mut_borrowed || src->second.immut_borrows > 0) {
    return emit_ownership_error("E_BORROW_CONFLICT", line,
                                "cannot take mutable borrow of '" + source + "' while another borrow is active", diag);
  }
  src->second.mut_borrowed = true;
  return true;
}

static bool check_function_ownership(const syntax::AstFunction& fn, support::DiagnosticSink& diag) {
  std::unordered_map<std::string, LocalState> states;

  for (std::size_t i = 0; i < fn.params.size(); ++i) {
    LocalState state;
    std::string stripped;
    const std::string annotation = i < fn.param_types.size() ? fn.param_types[i] : "";
    const OwnershipQualifier qual = parse_ownership_qualifier(annotation, stripped);
    state.owned = qual == OwnershipQualifier::Own;
    states[fn.params[i]] = state;
  }

  for (const auto& st : fn.body) {
    if (st.kind == syntax::StatementKind::Let) {
      const std::string name = parse_let_name(st.text);
      if (name.empty()) {
        continue;
      }
      std::string stripped_ann;
      const OwnershipQualifier qual = parse_ownership_qualifier(parse_let_annotation(st.text), stripped_ann);
      const std::string rhs = parse_let_rhs(st.text);
      const std::string rhs_ident = parse_simple_identifier(rhs);

      release_borrow_binding(name, states);
      LocalState next_state;
      next_state.owned = qual == OwnershipQualifier::Own;
      states[name] = next_state;

      if (rhs_ident.empty()) {
        for (const std::string& ident : extract_identifiers(rhs)) {
          if (!ensure_not_moved(ident, st.line, states, diag)) {
            return false;
          }
        }
        continue;
      }

      if (qual == OwnershipQualifier::Ref) {
        if (!mark_immutable_borrow(rhs_ident, st.line, states, diag)) {
          return false;
        }
        states[name].borrow_from = rhs_ident;
        states[name].borrow_kind = BorrowKind::Immutable;
        states[name].owned = false;
        continue;
      }

      if (qual == OwnershipQualifier::Mut) {
        if (!mark_mutable_borrow(rhs_ident, st.line, states, diag)) {
          return false;
        }
        states[name].borrow_from = rhs_ident;
        states[name].borrow_kind = BorrowKind::Mutable;
        states[name].owned = false;
        continue;
      }

      if (!consume_move(rhs_ident, st.line, states, diag)) {
        return false;
      }
      auto src = states.find(rhs_ident);
      if (src != states.end()) {
        states[name].owned = src->second.owned;
      }
      continue;
    }

    if (st.kind == syntax::StatementKind::Assign) {
      const std::string target = parse_assignment_target(st.text);
      if (target.empty()) {
        continue;
      }
      auto dst = states.find(target);
      if (dst != states.end() && (dst->second.immut_borrows > 0 || dst->second.mut_borrowed)) {
        return emit_ownership_error("E_BORROW_MUTATE_CONFLICT", st.line,
                                    "cannot assign to '" + target + "' while it is borrowed", diag);
      }
      const std::string rhs = parse_assignment_rhs(st.text);
      const std::string rhs_ident = parse_simple_identifier(rhs);
      if (rhs_ident.empty()) {
        for (const std::string& ident : extract_identifiers(rhs)) {
          if (!ensure_not_moved(ident, st.line, states, diag)) {
            return false;
          }
        }
      } else {
        if (!consume_move(rhs_ident, st.line, states, diag)) {
          return false;
        }
      }
      if (dst != states.end()) {
        release_borrow_binding(target, states);
        dst->second.moved = false;
      }
      continue;
    }

    if (st.kind == syntax::StatementKind::Return) {
      const std::string ret = parse_return_expr(st.text);
      const std::string ident = parse_simple_identifier(ret);
      if (!ident.empty()) {
        if (!consume_move(ident, st.line, states, diag)) {
          return false;
        }
      } else {
        for (const std::string& used : extract_identifiers(ret)) {
          if (!ensure_not_moved(used, st.line, states, diag)) {
            return false;
          }
        }
      }
      continue;
    }

    if (st.has_expression && st.expression_valid) {
      std::unordered_set<std::string> seen;
      for (const std::string& used : extract_identifiers(st.expression_normalized)) {
        if (!seen.insert(used).second) {
          continue;
        }
        if (!ensure_not_moved(used, st.line, states, diag)) {
          return false;
        }
      }
    }
  }

  for (auto& entry : states) {
    release_borrow_binding(entry.first, states);
  }
  return true;
}

}  // namespace

bool check_program_ownership(const syntax::AstProgram& program, support::DiagnosticSink& diag) {
  for (const auto& fn : program.functions) {
    if (!check_function_ownership(fn, diag)) {
      return false;
    }
  }
  return true;
}

}  // namespace thagc::middleend
