#pragma once

namespace thagc::semantics {

enum class TypeKind {
  I32,
  F32,
  F64,
  Bool,
  String,
  Ptr,
  Void,
  Unknown,
};

}  // namespace thagc::semantics

