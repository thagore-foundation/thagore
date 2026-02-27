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
  FunctionType,
  Ptr,
  StructType,
  EnumType,
  Void,
  Unknown,
  Fn = FunctionType,
  Struct = StructType,
};

}  // namespace thagc::semantics
