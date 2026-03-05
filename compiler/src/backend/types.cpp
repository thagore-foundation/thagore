#include "internal.hpp"

namespace thagc::codegen {

bool is_numeric_type(ValueType type) {
  return type == ValueType::I32 || type == ValueType::I64 || type == ValueType::F32 || type == ValueType::F64;
}

bool is_float_type(ValueType type) {
  return type == ValueType::F32 || type == ValueType::F64;
}

bool is_integer_numeric_type(ValueType type) {
  return type == ValueType::I32 || type == ValueType::I64;
}

ValueType promoted_integer_type(ValueType lhs, ValueType rhs) {
  if (lhs == ValueType::I64 || rhs == ValueType::I64) {
    return ValueType::I64;
  }
  return ValueType::I32;
}

llvm::Value* to_integer_numeric_value(ExprValue value, ValueType target, llvm::IRBuilder<>& builder) {
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

ValueType promoted_float_type(ValueType lhs, ValueType rhs) {
  if (lhs == ValueType::F64 || rhs == ValueType::F64) {
    return ValueType::F64;
  }
  if (lhs == ValueType::F32 || rhs == ValueType::F32) {
    return ValueType::F32;
  }
  return ValueType::F64;
}

llvm::Value* to_float_value(ExprValue value, ValueType target, llvm::IRBuilder<>& builder) {
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

llvm::Value* to_f64_value(ExprValue value, llvm::IRBuilder<>& builder) {
  return to_float_value(value, ValueType::F64, builder);
}

llvm::Value* to_i32(ExprValue value, llvm::IRBuilder<>& builder) {
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

llvm::Value* to_i64(ExprValue value, llvm::IRBuilder<>& builder) {
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

llvm::Value* to_i1(ExprValue value, llvm::IRBuilder<>& builder) {
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


ValueType value_type_from_return_type(const std::string& type_name) {
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

llvm::Type* llvm_type_from_value_type(ValueType type, llvm::IRBuilder<>& builder) {
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

llvm::Value* cast_value_to_type(ExprValue value, ValueType target, llvm::IRBuilder<>& builder) {
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

llvm::Value* default_value_for_type(ValueType type, llvm::IRBuilder<>& builder) {
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
    return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(pointer_type(builder)));
  }
  if (type == ValueType::Void) {
    return nullptr;
  }
  return builder.getInt32(0);
}

ValueType value_type_from_field_annotation(const std::string& type_name) {
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

ValueType field_value_type_for_struct(const std::string& struct_name, const std::string& field_name,
                                             const std::unordered_map<std::string, std::string>& struct_field_types) {
  auto type_it = struct_field_types.find(struct_name + "." + field_name);
  if (type_it == struct_field_types.end()) {
    return ValueType::I32;
  }
  return value_type_from_field_annotation(type_it->second);
}


}  // namespace thagc::codegen
