#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "thagc/frontend/types.hpp"

namespace thagc::ty {

struct Ty;
using TyPtr = std::shared_ptr<Ty>;

struct TyInt {
  int bits = 32;
  bool is_signed = true;
};

struct TyFloat {
  int bits = 32;
};

struct TyBool {};
struct TyUnit {};
struct TyStr {};

struct TyNamed {
  std::string name;
  std::vector<TyPtr> args;
};

struct TyFn {
  std::vector<TyPtr> params;
  TyPtr ret;
};

struct TyVar {
  std::uint32_t id = 0;
};

using TyData = std::variant<TyInt, TyFloat, TyBool, TyUnit, TyStr, TyNamed, TyFn, TyVar>;

struct Ty {
  TyData data;
};

inline Ty from_type_kind(semantics::TypeKind kind) {
  using semantics::TypeKind;
  switch (kind) {
    case TypeKind::I32:
      return Ty{TyInt{32, true}};
    case TypeKind::I64:
      return Ty{TyInt{64, true}};
    case TypeKind::F32:
      return Ty{TyFloat{32}};
    case TypeKind::F64:
      return Ty{TyFloat{64}};
    case TypeKind::Bool:
      return Ty{TyBool{}};
    case TypeKind::String:
      return Ty{TyStr{}};
    case TypeKind::Void:
      return Ty{TyUnit{}};
    case TypeKind::StructType:
      return Ty{TyNamed{"struct", {}}};
    case TypeKind::EnumType:
      return Ty{TyNamed{"enum", {}}};
    case TypeKind::TupleType:
      return Ty{TyNamed{"tuple", {}}};
    case TypeKind::ArrayType:
      return Ty{TyNamed{"array", {}}};
    case TypeKind::Option:
      return Ty{TyNamed{"Option", {}}};
    case TypeKind::Result:
      return Ty{TyNamed{"Result", {}}};
    case TypeKind::Rc:
      return Ty{TyNamed{"Rc", {}}};
    case TypeKind::Arc:
      return Ty{TyNamed{"Arc", {}}};
    case TypeKind::Ptr:
      return Ty{TyNamed{"ptr", {}}};
    case TypeKind::FunctionType:
      return Ty{TyNamed{"fn", {}}};
    case TypeKind::List:
      return Ty{TyNamed{"list", {}}};
    case TypeKind::Unknown:
    default:
      return Ty{TyVar{0}};
  }
}

inline semantics::TypeKind to_type_kind(const Ty& ty) {
  using semantics::TypeKind;
  if (std::holds_alternative<TyInt>(ty.data)) {
    const auto int_ty = std::get<TyInt>(ty.data);
    return int_ty.bits >= 64 ? TypeKind::I64 : TypeKind::I32;
  }
  if (std::holds_alternative<TyFloat>(ty.data)) {
    const auto float_ty = std::get<TyFloat>(ty.data);
    return float_ty.bits >= 64 ? TypeKind::F64 : TypeKind::F32;
  }
  if (std::holds_alternative<TyBool>(ty.data)) {
    return TypeKind::Bool;
  }
  if (std::holds_alternative<TyUnit>(ty.data)) {
    return TypeKind::Void;
  }
  if (std::holds_alternative<TyStr>(ty.data)) {
    return TypeKind::String;
  }
  if (std::holds_alternative<TyNamed>(ty.data)) {
    const auto& named = std::get<TyNamed>(ty.data);
    if (named.name == "Option") {
      return TypeKind::Option;
    }
    if (named.name == "Result") {
      return TypeKind::Result;
    }
    if (named.name == "Rc") {
      return TypeKind::Rc;
    }
    if (named.name == "Arc") {
      return TypeKind::Arc;
    }
    if (named.name == "ptr") {
      return TypeKind::Ptr;
    }
    if (named.name == "fn") {
      return TypeKind::FunctionType;
    }
    if (named.name == "list") {
      return TypeKind::List;
    }
    if (named.name == "tuple") {
      return TypeKind::TupleType;
    }
    if (named.name == "array") {
      return TypeKind::ArrayType;
    }
    if (named.name == "enum") {
      return TypeKind::EnumType;
    }
    if (named.name == "struct") {
      return TypeKind::StructType;
    }
  }
  return TypeKind::Unknown;
}

}  // namespace thagc::ty
