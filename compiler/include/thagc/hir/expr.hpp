#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "thagc/frontend/ast.hpp"
#include "thagc/ty/ty.hpp"

namespace thagc::hir {

struct HirExpr;
using HirExprPtr = std::shared_ptr<HirExpr>;

enum class HirLitKind {
  Integer,
  Float,
  Bool,
  String,
};

struct HirRaw {
  std::string text;
};

struct HirLit {
  HirLitKind kind = HirLitKind::Integer;
  std::string value;
};

struct HirIdent {
  std::string name;
};

struct HirCall {
  HirExprPtr callee;
  std::vector<HirExprPtr> args;
};

struct HirBin {
  std::string op;
  HirExprPtr lhs;
  HirExprPtr rhs;
};

struct HirUnary {
  std::string op;
  HirExprPtr operand;
};

struct HirField {
  HirExprPtr base;
  std::string field;
};

struct HirIndex {
  HirExprPtr base;
  HirExprPtr index;
};

struct HirTuple {
  std::vector<HirExprPtr> items;
};

struct HirArray {
  std::vector<HirExprPtr> items;
};

using HirExprNode =
    std::variant<HirRaw, HirLit, HirIdent, HirCall, HirBin, HirUnary, HirField, HirIndex, HirTuple, HirArray>;

struct HirExpr {
  HirExprNode node = HirRaw{};
  std::optional<syntax::Span> span;
  std::optional<ty::Ty> inferred_type;
};

}  // namespace thagc::hir
