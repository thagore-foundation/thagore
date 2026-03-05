#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "thagc/frontend/span.hpp"
#include "thagc/ty/ty.hpp"

namespace thagc::mir {

enum class MirProjectionKind {
  Field,
  Index,
};

struct MirProjectionElem {
  MirProjectionKind kind = MirProjectionKind::Field;
  std::string value;
};

struct MirPlace {
  std::uint32_t local = 0;
  std::vector<MirProjectionElem> projection;
};

struct MirLocal {
  std::uint32_t id = 0;
  std::string name;
  ty::Ty ty;
  bool is_mut = false;
  bool is_owned = false;
};

enum class MirOperandKind {
  Copy,
  Move,
  Ref,
  MutRef,
  Constant,
};

struct MirOperand {
  MirOperandKind kind = MirOperandKind::Constant;
  std::optional<std::uint32_t> local;
  std::string text;
};

enum class MirRvalueKind {
  Use,
  Aggregate,
};

struct MirRvalue {
  MirRvalueKind kind = MirRvalueKind::Use;
  MirOperand operand;
  std::vector<MirOperand> aggregate_items;
};

enum class MirStatementKind {
  Assign,
  Eval,
};

struct MirStatement {
  MirStatementKind kind = MirStatementKind::Assign;
  std::optional<MirPlace> lhs;
  MirRvalue rhs;
  int line = 0;
  std::optional<syntax::Span> span;
  std::string text;
};

enum class MirTerminatorKind {
  Return,
  Drop,
  Unreachable,
};

struct MirTerminator {
  MirTerminatorKind kind = MirTerminatorKind::Unreachable;
  std::optional<MirOperand> value;
  int line = 0;
  std::optional<syntax::Span> span;
};

struct MirBasicBlock {
  std::vector<MirStatement> statements;
  MirTerminator term;
};

struct MirBody {
  std::string function_name;
  std::vector<MirLocal> locals;
  std::vector<MirBasicBlock> blocks;
};

}  // namespace thagc::mir
