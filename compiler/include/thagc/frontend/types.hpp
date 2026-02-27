#pragma once

namespace thagc::semantics {

enum class TypeKind {
  I32,
  F32,
  F64,
  Bool,
  String,
  Option,
  Result,
  List,
  Rc,
  Arc,
  Fn,
  Ptr,
  Struct,
  Void,
  Unknown,
};

}  // namespace thagc::semantics
