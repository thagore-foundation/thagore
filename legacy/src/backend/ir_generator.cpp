#include "thagore/backend/ir_generator.hpp"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/IR/LegacyPassManager.h"

#include <format>
#include <charconv>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace thagore {
namespace {

auto overloadedMethodFor(BinaryOp op) -> std::string_view {
  switch (op) {
    case BinaryOp::Add: return "__add__";
    case BinaryOp::Sub: return "__sub__";
    case BinaryOp::Mul: return "__mul__";
    case BinaryOp::Div: return "__div__";
    case BinaryOp::Eq: return "__eq__";
    default: return "";
  }
}

auto getStringStructType(llvm::LLVMContext &context) -> llvm::StructType * {
  if (auto *existing = llvm::StructType::getTypeByName(context, "thg.string")) {
    return existing;
  }
  auto *ptrTy = llvm::PointerType::get(context, 0);
  auto *i32Ty = llvm::Type::getInt32Ty(context);
  return llvm::StructType::create(context, {ptrTy, i32Ty}, "thg.string");
}

struct LocalValue {
  llvm::AllocaInst *slot {nullptr};
  BaseType type {BaseType::Unknown};
  TypePtr declaredType {};
  std::string structName {};
  bool ownedRef {false};
};

struct StructLayout {
  llvm::StructType *llvmType {nullptr};
  std::vector<StructDecl::Field> fields {};
  std::unordered_map<std::string, std::size_t> fieldIndices {};
};

auto mapDeclaredType(
  llvm::LLVMContext &context,
  const TypePtr &type,
  const std::unordered_map<std::string, StructLayout> &structLayouts
) -> llvm::Type * {
  if (!type) {
    return llvm::PointerType::get(context, 0);
  }
  switch (type->base) {
    case BaseType::Void: return llvm::Type::getVoidTy(context);
    case BaseType::I32: return llvm::Type::getInt32Ty(context);
    case BaseType::F32: return llvm::Type::getFloatTy(context);
    case BaseType::F64: return llvm::Type::getDoubleTy(context);
    case BaseType::Pointer: return llvm::PointerType::get(context, 0);
    case BaseType::Bool: return llvm::Type::getInt1Ty(context);
    case BaseType::String: return getStringStructType(context);
    case BaseType::Array: {
      if (!type->elementType || type->arraySize == 0) {
        return llvm::Type::getInt32Ty(context);
      }
      auto *elemTy = mapDeclaredType(context, type->elementType, structLayouts);
      return llvm::ArrayType::get(elemTy, static_cast<std::uint64_t>(type->arraySize));
    }
    case BaseType::Struct: {
      auto it = structLayouts.find(type->name);
      if (it != structLayouts.end()) {
        return it->second.llvmType;
      }
      return llvm::Type::getInt32Ty(context);
    }
    case BaseType::Unknown: break;
  }
  return llvm::PointerType::get(context, 0);
}

class FunctionLowering {
public:
  FunctionLowering(
    llvm::LLVMContext &ctx,
    llvm::Module &mod,
    llvm::Function &fn,
    const std::unordered_map<std::string, StructLayout> &structLayouts_,
    const std::unordered_map<std::string, BaseType> &functionReturnKinds_
  )
    : context(ctx), module(mod), function(fn), builder(ctx), structLayouts(structLayouts_),
      functionReturnKinds(functionReturnKinds_) {
    declareRuntimeHooks();
  }

  auto lower(const FunctionDecl &decl) -> Result<void, Diagnostic> {
    const BaseType returnType = decl.returnType ? decl.returnType->base : BaseType::Void;
    return lowerStatements(decl.body->statements, returnType, &decl);
  }

  auto lowerStatements(
    const std::vector<std::unique_ptr<Stmt>> &statements,
    BaseType returnType,
    const FunctionDecl *decl = nullptr
  ) -> Result<void, Diagnostic> {
    auto *entry = llvm::BasicBlock::Create(context, "entry", &function);
    builder.SetInsertPoint(entry);
    pushScope();
    if (decl != nullptr) {
      auto paramResult = bindFunctionParams(*decl);
      if (!paramResult) {
        return std::unexpected(paramResult.error());
      }
    }
    if (function.getName() == "main" && function.arg_size() >= 2) {
      auto it = function.arg_begin();
      llvm::Value *argc = &*it;
      ++it;
      llvm::Value *argv = &*it;
      builder.CreateCall(initEnvFn, {argc, argv});
    }

    for (const auto &stmt : statements) {
      auto result = lowerStmt(*stmt);
      if (!result) {
        return std::unexpected(result.error());
      }
      if (terminated()) {
        break;
      }
    }

    if (!terminated()) {
      popScope();
      if (returnType == BaseType::Void || function.getReturnType()->isVoidTy()) {
        builder.CreateRetVoid();
      } else if (returnType == BaseType::I32 || function.getReturnType()->isIntegerTy(32)) {
        builder.CreateRet(llvm::ConstantInt::get(function.getReturnType(), 0));
      } else {
        builder.CreateRet(llvm::UndefValue::get(function.getReturnType()));
      }
    } else {
      scopeLocals.clear();
      locals.clear();
    }
    return {};
  }

private:
  llvm::LLVMContext &context;
  llvm::Module &module;
  llvm::Function &function;
  llvm::IRBuilder<> builder;
  std::unordered_map<std::string, LocalValue> locals {};
  std::vector<std::vector<std::string>> scopeLocals {};
  const std::unordered_map<std::string, StructLayout> &structLayouts;
  const std::unordered_map<std::string, BaseType> &functionReturnKinds;
  llvm::FunctionCallee retainFn {};
  llvm::FunctionCallee releaseFn {};
  llvm::FunctionCallee strAddFn {};
  llvm::FunctionCallee strEqFn {};
  llvm::FunctionCallee strDupFn {};
  llvm::FunctionCallee strFreeFn {};
  llvm::FunctionCallee cstrLenFn {};
  llvm::FunctionCallee strLenFn {};
  llvm::FunctionCallee strSubstrFn {};
  llvm::FunctionCallee initEnvFn {};

  auto terminated() const -> bool {
    auto *block = builder.GetInsertBlock();
    return block && block->getTerminator() != nullptr;
  }

  void declareRuntimeHooks() {
    auto *voidTy = llvm::Type::getVoidTy(context);
    auto *ptrTy = llvm::PointerType::get(context, 0);
    auto *i32Ty = llvm::Type::getInt32Ty(context);
    retainFn = module.getOrInsertFunction("__thg_retain", llvm::FunctionType::get(voidTy, {ptrTy}, false));
    releaseFn = module.getOrInsertFunction("__thg_release", llvm::FunctionType::get(voidTy, {ptrTy}, false));
    strAddFn = module.getOrInsertFunction("__thg_str_add", llvm::FunctionType::get(ptrTy, {ptrTy, ptrTy}, false));
    strEqFn = module.getOrInsertFunction("__thg_str_eq", llvm::FunctionType::get(i32Ty, {ptrTy, ptrTy}, false));
    strDupFn = module.getOrInsertFunction("__thg_str_dup", llvm::FunctionType::get(ptrTy, {ptrTy}, false));
    strFreeFn = module.getOrInsertFunction("__thg_str_free", llvm::FunctionType::get(voidTy, {ptrTy}, false));
    cstrLenFn = module.getOrInsertFunction("__thg_cstr_len", llvm::FunctionType::get(i32Ty, {ptrTy}, false));
    strLenFn = module.getOrInsertFunction("__thg_str_len", llvm::FunctionType::get(i32Ty, {ptrTy}, false));
    strSubstrFn = module.getOrInsertFunction("__thg_str_substr", llvm::FunctionType::get(ptrTy, {ptrTy, i32Ty, i32Ty}, false));
    initEnvFn = module.getOrInsertFunction("__thg_init_env", llvm::FunctionType::get(voidTy, {i32Ty, ptrTy}, false));
  }

  auto llvmType(BaseType type) -> llvm::Type * {
    switch (type) {
      case BaseType::Void: return llvm::Type::getVoidTy(context);
      case BaseType::I32: return llvm::Type::getInt32Ty(context);
      case BaseType::F32: return llvm::Type::getFloatTy(context);
      case BaseType::F64: return llvm::Type::getDoubleTy(context);
      case BaseType::Pointer: return llvm::PointerType::get(context, 0);
      case BaseType::Bool: return llvm::Type::getInt1Ty(context);
      case BaseType::String: return getStringStructType(context);
      case BaseType::Array: break;
      case BaseType::Struct: break;
      case BaseType::Unknown: break;
    }
    return llvm::Type::getInt32Ty(context);
  }

  auto findStructLayoutByType(llvm::Type *type) const -> const StructLayout * {
    for (const auto &[_, layout] : structLayouts) {
      if (layout.llvmType == type) {
        return &layout;
      }
    }
    return nullptr;
  }

  auto ensureBool(llvm::Value *value, const SourceSpan &span) -> Result<llvm::Value *, Diagnostic> {
    if (value->getType()->isIntegerTy(1)) {
      return value;
    }
    if (value->getType()->isPointerTy()) {
      return builder.CreateICmpNE(
        value,
        llvm::ConstantPointerNull::get(llvm::PointerType::get(context, 0)),
        "cond.ptr"
      );
    }
    if (value->getType()->isIntegerTy()) {
      return builder.CreateICmpNE(value, llvm::ConstantInt::get(value->getType(), 0), "cond");
    }
    return std::unexpected(Diagnostic {
      .code = ErrorCode::CodegenError,
      .message = "Condition cannot be lowered to bool.",
      .span = span,
    });
  }

  auto createAlloca(const std::string &name, llvm::Type *type) -> llvm::AllocaInst * {
    auto &entryBlock = function.getEntryBlock();
    llvm::IRBuilder<> entryBuilder {&entryBlock, entryBlock.begin()};
    return entryBuilder.CreateAlloca(type, nullptr, name);
  }

  auto bindFunctionParams(const FunctionDecl &decl) -> Result<void, Diagnostic> {
    std::size_t idx = 0;
    for (auto &arg : function.args()) {
      if (idx >= decl.params.size()) {
        break;
      }
      const auto &param = decl.params[idx++];
      auto *slot = createAlloca(param.name, arg.getType());
      const bool isString = arg.getType() == llvmType(BaseType::String);
      if (isString) {
        auto cloned = cloneStringValue(&arg, param.span);
        if (!cloned) {
          return std::unexpected(cloned.error());
        }
        builder.CreateStore(cloned.value(), slot);
      } else {
        builder.CreateStore(&arg, slot);
      }
      locals.emplace(
        param.name,
        LocalValue {
          .slot = slot,
          .type = isString ? BaseType::String : ((param.type && param.type->base == BaseType::Struct) ? BaseType::Struct : (param.type ? param.type->base : BaseType::Unknown)),
          .declaredType = param.type,
          .structName = (!decl.methodOwner.empty() && param.name == "self") ? decl.methodOwner : ((param.type && param.type->base == BaseType::Struct) ? param.type->name : ""),
          .ownedRef = isString
        }
      );
      if (!scopeLocals.empty()) {
        scopeLocals.back().push_back(param.name);
      }
    }
    return {};
  }

  void pushScope() {
    scopeLocals.emplace_back();
  }

  void retainStringValue(llvm::Value *stringValue) {
    auto *ptr = builder.CreateExtractValue(stringValue, {0}, "str.retain.ptr");
    builder.CreateCall(retainFn, {ptr});
  }

  void releaseStringValue(llvm::Value *stringValue) {
    auto *ptr = builder.CreateExtractValue(stringValue, {0}, "str.release.ptr");
    builder.CreateCall(releaseFn, {ptr});
  }

  auto packCStringValue(llvm::Value *ptrValue) -> llvm::Value * {
    auto *strLen = builder.CreateCall(cstrLenFn, {ptrValue}, "cstr.len");
    llvm::Value *stringValue = llvm::UndefValue::get(llvmType(BaseType::String));
    stringValue = builder.CreateInsertValue(stringValue, ptrValue, {0}, "cstr.v.ptr");
    stringValue = builder.CreateInsertValue(stringValue, strLen, {1}, "cstr.v.len");
    return stringValue;
  }

  auto cloneStringValue(llvm::Value *value, const SourceSpan &span) -> Result<llvm::Value *, Diagnostic> {
    auto ptr = extractStringPointer(value, span, "str.clone");
    if (!ptr) {
      return std::unexpected(ptr.error());
    }
    auto *dup = builder.CreateCall(strDupFn, {ptr.value()}, "str.dup.ptr");
    return packCStringValue(dup);
  }

  auto freeStringValue(llvm::Value *stringValue) -> Result<void, Diagnostic> {
    auto ptr = extractStringPointer(stringValue, {}, "str.free");
    if (!ptr) {
      return std::unexpected(ptr.error());
    }
    builder.CreateCall(strFreeFn, {ptr.value()});
    return {};
  }

  auto extractStringPointer(llvm::Value *value, const SourceSpan &span, std::string_view nameHint)
    -> Result<llvm::Value *, Diagnostic> {
    auto *stringTy = llvmType(BaseType::String);
    if (value->getType() == stringTy) {
      return builder.CreateExtractValue(value, {0}, std::format("{}.ptr", nameHint));
    }
    if (value->getType()->isPointerTy()) {
      return value;
    }
    return std::unexpected(Diagnostic {
      .code = ErrorCode::CodegenError,
      .message = "String expression did not lower to string-compatible value.",
      .span = span,
    });
  }

  auto shouldRetainStringExpr(const Expr &expr) -> bool {
    if (expr.kind == NodeKind::LiteralExpr) {
      const auto &lit = static_cast<const LiteralExpr &>(expr);
      return lit.literalKind != LiteralExpr::Kind::String;
    }
    if (expr.kind == NodeKind::BinaryExpr) {
      const auto &bin = static_cast<const BinaryExpr &>(expr);
      return bin.op == BinaryOp::Add;
    }
    if (expr.kind == NodeKind::IdentifierExpr) {
      return true;
    }
    if (expr.kind == NodeKind::CallExpr) {
      return true;
    }
    return false;
  }

  void popScope() {
    if (scopeLocals.empty()) {
      return;
    }
    auto names = std::move(scopeLocals.back());
    scopeLocals.pop_back();
    for (auto it = names.rbegin(); it != names.rend(); ++it) {
      auto localIt = locals.find(*it);
      if (localIt == locals.end()) {
        continue;
      }
      if (localIt->second.ownedRef && !terminated()) {
        auto *loaded = builder.CreateLoad(localIt->second.slot->getAllocatedType(), localIt->second.slot);
        if (localIt->second.type == BaseType::String) {
          auto freed = freeStringValue(loaded);
          if (!freed) {
            locals.erase(localIt);
            continue;
          }
        } else if (loaded->getType()->isPointerTy()) {
          builder.CreateCall(releaseFn, {loaded});
        }
      }
      locals.erase(localIt);
    }
  }

  void emitReturnCleanups() {
    for (auto scopeIt = scopeLocals.rbegin(); scopeIt != scopeLocals.rend(); ++scopeIt) {
      for (auto nameIt = scopeIt->rbegin(); nameIt != scopeIt->rend(); ++nameIt) {
        auto localIt = locals.find(*nameIt);
        if (localIt == locals.end() || !localIt->second.ownedRef) {
          continue;
        }
        auto *loaded = builder.CreateLoad(localIt->second.slot->getAllocatedType(), localIt->second.slot);
        if (localIt->second.type == BaseType::String) {
          auto freed = freeStringValue(loaded);
          if (!freed) {
            continue;
          }
        } else if (loaded->getType()->isPointerTy()) {
          builder.CreateCall(releaseFn, {loaded});
        }
      }
    }
  }

  auto lowerStmt(const Stmt &stmt) -> Result<void, Diagnostic> {
    switch (stmt.kind) {
      case NodeKind::LetStmt: return lowerLet(static_cast<const LetStmt &>(stmt));
      case NodeKind::AssignStmt: return lowerAssign(static_cast<const AssignStmt &>(stmt));
      case NodeKind::MemberAssignStmt: return lowerMemberAssign(static_cast<const MemberAssignStmt &>(stmt));
      case NodeKind::ArrayAssignStmt: return lowerArrayAssign(static_cast<const ArrayAssignStmt &>(stmt));
      case NodeKind::ReturnStmt: return lowerReturn(static_cast<const ReturnStmt &>(stmt));
      case NodeKind::IfStmt: return lowerIf(static_cast<const IfStmt &>(stmt));
      case NodeKind::LoopStmt: return lowerLoop(static_cast<const LoopStmt &>(stmt));
      case NodeKind::ExprStmt: {
        auto expr = lowerExpr(*static_cast<const ExprStmt &>(stmt).expr);
        if (!expr) {
          return std::unexpected(expr.error());
        }
        return {};
      }
      case NodeKind::BlockStmt: return lowerBlock(static_cast<const BlockStmt &>(stmt));
      default:
        return std::unexpected(Diagnostic {
          .code = ErrorCode::CodegenError,
          .message = "Unsupported statement kind in lowering.",
          .span = stmt.span,
        });
    }
  }

  auto lowerBlock(const BlockStmt &block) -> Result<void, Diagnostic> {
    pushScope();
    for (const auto &stmt : block.statements) {
      auto lowered = lowerStmt(*stmt);
      if (!lowered) {
        return std::unexpected(lowered.error());
      }
      if (terminated()) {
        break;
      }
    }
    popScope();
    return {};
  }

  auto lowerLet(const LetStmt &stmt) -> Result<void, Diagnostic> {
    auto init = lowerExpr(*stmt.init);
    if (!init) {
      return std::unexpected(init.error());
    }
    llvm::Value *initValue = init.value();
    auto *stringTy = llvmType(BaseType::String);
    BaseType exprType = inferExprType(*stmt.init);
    if (exprType == BaseType::String && initValue->getType()->isPointerTy()) {
      initValue = packCStringValue(initValue);
    }
    if (exprType == BaseType::Unknown && initValue->getType() == stringTy) {
      exprType = BaseType::String;
    }
    std::string structName {};
    if (exprType == BaseType::Struct) {
      if (stmt.init->inferredType && stmt.init->inferredType->base == BaseType::Struct) {
        structName = stmt.init->inferredType->name;
      }
      if (structName.empty() && stmt.init->kind == NodeKind::CallExpr) {
        const auto &call = static_cast<const CallExpr &>(*stmt.init);
        if (structLayouts.contains(call.callee)) {
          structName = call.callee;
        }
      }
    }
    if (exprType == BaseType::Unknown) {
      if (const auto *layout = findStructLayoutByType(initValue->getType()); layout != nullptr) {
        exprType = BaseType::Struct;
        for (const auto &[name, candidate] : structLayouts) {
          if (candidate.llvmType == initValue->getType()) {
            structName = name;
            break;
          }
        }
      }
    }
    auto *alloca = createAlloca(stmt.name, initValue->getType());
    bool shouldRetain = false;
    if (initValue->getType() == stringTy) {
      auto cloned = cloneStringValue(initValue, stmt.span);
      if (!cloned) {
        return std::unexpected(cloned.error());
      }
      initValue = cloned.value();
      shouldRetain = true;
    } else if (initValue->getType()->isPointerTy() && !isTemporaryExpr(*stmt.init)) {
      shouldRetain = true;
      builder.CreateCall(retainFn, {initValue});
    }
    builder.CreateStore(initValue, alloca);
    if (shouldRetain) {
      locals.emplace(
        stmt.name,
        LocalValue {
          .slot = alloca,
          .type = exprType,
          .declaredType = (exprType == BaseType::Struct && !structName.empty())
            ? makeStructType(structName)
            : stmt.init->inferredType,
          .structName = structName,
          .ownedRef = true
        }
      );
    } else {
      locals.emplace(
        stmt.name,
        LocalValue {
          .slot = alloca,
          .type = exprType,
          .declaredType = (exprType == BaseType::Struct && !structName.empty())
            ? makeStructType(structName)
            : stmt.init->inferredType,
          .structName = structName,
          .ownedRef = false
        }
      );
    }
    if (!scopeLocals.empty()) {
      scopeLocals.back().push_back(stmt.name);
    }
    return {};
  }

  auto lowerAssign(const AssignStmt &stmt) -> Result<void, Diagnostic> {
    auto found = locals.find(stmt.name);
    if (found == locals.end()) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = std::format("Unknown variable '{}' in assignment.", stmt.name),
        .span = stmt.span,
      });
    }

    auto rhs = lowerExpr(*stmt.value);
    if (!rhs) {
      return std::unexpected(rhs.error());
    }
    llvm::Value *rhsValue = rhs.value();
    if (found->second.ownedRef) {
      auto *oldValue = builder.CreateLoad(found->second.slot->getAllocatedType(), found->second.slot);
      if (found->second.type == BaseType::String) {
        auto freed = freeStringValue(oldValue);
        if (!freed) {
          return std::unexpected(freed.error());
        }
      } else {
        builder.CreateCall(releaseFn, {oldValue});
      }
    }
    if (found->second.slot->getAllocatedType() == llvmType(BaseType::String)) {
      if (rhsValue->getType()->isPointerTy()) {
        rhsValue = packCStringValue(rhsValue);
      }
      auto cloned = cloneStringValue(rhsValue, stmt.span);
      if (!cloned) {
        return std::unexpected(cloned.error());
      }
      rhsValue = cloned.value();
    }
    builder.CreateStore(rhsValue, found->second.slot);
    auto *stringTy = llvmType(BaseType::String);
    const bool isStringSlot = found->second.type == BaseType::String || found->second.slot->getAllocatedType() == stringTy;
    if (isStringSlot) {
      found->second.type = BaseType::String;
      found->second.declaredType = makeType(BaseType::String);
      found->second.structName.clear();
      found->second.ownedRef = true;
      return {};
    }
    if (const auto *layout = findStructLayoutByType(found->second.slot->getAllocatedType()); layout != nullptr) {
      found->second.type = BaseType::Struct;
      found->second.declaredType = nullptr;
      for (const auto &[name, candidate] : structLayouts) {
        if (candidate.llvmType == layout->llvmType) {
          found->second.structName = name;
          found->second.declaredType = makeStructType(name);
          break;
        }
      }
      found->second.ownedRef = false;
      return {};
    }
    if (rhsValue->getType()->isPointerTy() && !isTemporaryExpr(*stmt.value)) {
      builder.CreateCall(retainFn, {rhsValue});
      found->second.ownedRef = true;
      return {};
    }
    found->second.ownedRef = false;
    return {};
  }

  auto lowerMemberAssign(const MemberAssignStmt &stmt) -> Result<void, Diagnostic> {
    auto objectIt = locals.find(stmt.objectName);
    if (objectIt == locals.end()) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = std::format("Unknown variable '{}' in member assignment.", stmt.objectName),
        .span = stmt.span,
      });
    }
    if (objectIt->second.type != BaseType::Struct) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = std::format("'{}' is not a struct value.", stmt.objectName),
        .span = stmt.span,
      });
    }

    auto layoutIt = structLayouts.find(objectIt->second.structName);
    if (layoutIt == structLayouts.end()) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = std::format("Unknown struct layout for '{}'.", stmt.objectName),
        .span = stmt.span,
      });
    }
    const auto &layout = layoutIt->second;
    auto fieldIt = layout.fieldIndices.find(stmt.memberName);
    if (fieldIt == layout.fieldIndices.end()) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = std::format("Unknown field '{}.{}'.", objectIt->second.structName, stmt.memberName),
        .span = stmt.span,
      });
    }

    auto rhs = lowerExpr(*stmt.value);
    if (!rhs) {
      return std::unexpected(rhs.error());
    }
    llvm::Value *rhsValue = rhs.value();

    llvm::Value *basePtr = nullptr;
    if (objectIt->second.slot->getAllocatedType()->isPointerTy()) {
      basePtr = builder.CreateLoad(objectIt->second.slot->getAllocatedType(), objectIt->second.slot, "member.base.ptr");
    } else {
      basePtr = objectIt->second.slot;
    }

    auto *idx0 = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0);
    auto *idxN = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), static_cast<std::uint32_t>(fieldIt->second));
    auto *fieldPtr = builder.CreateInBoundsGEP(layout.llvmType, basePtr, {idx0, idxN}, "member.assign.ptr");
    auto *fieldTy = layout.llvmType->getElementType(static_cast<unsigned int>(fieldIt->second));

    if (fieldTy->isPointerTy() && rhsValue->getType() == llvmType(BaseType::String)) {
      auto ptr = extractStringPointer(rhsValue, stmt.span, "member.assign.str");
      if (!ptr) {
        return std::unexpected(ptr.error());
      }
      rhsValue = ptr.value();
    }

    builder.CreateStore(rhsValue, fieldPtr);
    return {};
  }

  auto lowerArrayAssign(const ArrayAssignStmt &stmt) -> Result<void, Diagnostic> {
    auto elemPtr = lowerArrayElementPtr(stmt.arrayName, *stmt.index, stmt.span);
    if (!elemPtr) {
      return std::unexpected(elemPtr.error());
    }
    auto rhs = lowerExpr(*stmt.value);
    if (!rhs) {
      return std::unexpected(rhs.error());
    }
    builder.CreateStore(rhs.value(), elemPtr.value());
    return {};
  }

  auto lowerReturn(const ReturnStmt &stmt) -> Result<void, Diagnostic> {
    if (!stmt.value) {
      emitReturnCleanups();
      builder.CreateRetVoid();
      return {};
    }

    auto ret = lowerExpr(*stmt.value);
    if (!ret) {
      return std::unexpected(ret.error());
    }
    if (function.getReturnType()->isVoidTy()) {
      emitReturnCleanups();
      builder.CreateRetVoid();
      return {};
    }
    llvm::Value *retValue = ret.value();
    if (function.getReturnType() == llvmType(BaseType::String) && retValue->getType()->isPointerTy()) {
      retValue = packCStringValue(retValue);
    }
    if (retValue->getType() == llvmType(BaseType::String)) {
      auto cloned = cloneStringValue(retValue, stmt.span);
      if (!cloned) {
        return std::unexpected(cloned.error());
      }
      retValue = cloned.value();
    }
    emitReturnCleanups();
    builder.CreateRet(retValue);
    return {};
  }

  auto lowerIf(const IfStmt &stmt) -> Result<void, Diagnostic> {
    auto condValue = lowerExpr(*stmt.condition);
    if (!condValue) {
      return std::unexpected(condValue.error());
    }
    auto boolCond = ensureBool(condValue.value(), stmt.condition->span);
    if (!boolCond) {
      return std::unexpected(boolCond.error());
    }

    auto *thenBB = llvm::BasicBlock::Create(context, "if.then", &function);
    llvm::BasicBlock *elseBB = nullptr;
    auto *mergeBB = llvm::BasicBlock::Create(context, "if.end", &function);
    if (stmt.elseBlock) {
      elseBB = llvm::BasicBlock::Create(context, "if.else", &function);
      builder.CreateCondBr(boolCond.value(), thenBB, elseBB);
    } else {
      builder.CreateCondBr(boolCond.value(), thenBB, mergeBB);
    }

    builder.SetInsertPoint(thenBB);
    auto thenResult = lowerBlock(*stmt.thenBlock);
    if (!thenResult) {
      return std::unexpected(thenResult.error());
    }
    if (!terminated()) {
      builder.CreateBr(mergeBB);
    }

    if (stmt.elseBlock) {
      builder.SetInsertPoint(elseBB);
      auto elseResult = lowerBlock(*stmt.elseBlock);
      if (!elseResult) {
        return std::unexpected(elseResult.error());
      }
      if (!terminated()) {
        builder.CreateBr(mergeBB);
      }
    }

    builder.SetInsertPoint(mergeBB);
    return {};
  }

  auto lowerLoop(const LoopStmt &stmt) -> Result<void, Diagnostic> {
    auto *condBB = llvm::BasicBlock::Create(context, "loop.cond", &function);
    auto *bodyBB = llvm::BasicBlock::Create(context, "loop.body", &function);
    auto *afterBB = llvm::BasicBlock::Create(context, "loop.after", &function);

    builder.CreateBr(condBB);
    builder.SetInsertPoint(condBB);
    if (stmt.condition) {
      auto condValue = lowerExpr(*stmt.condition);
      if (!condValue) {
        return std::unexpected(condValue.error());
      }
      auto boolCond = ensureBool(condValue.value(), stmt.condition->span);
      if (!boolCond) {
        return std::unexpected(boolCond.error());
      }
      builder.CreateCondBr(boolCond.value(), bodyBB, afterBB);
    } else {
      auto *always = llvm::ConstantInt::getTrue(context);
      builder.CreateCondBr(always, bodyBB, afterBB);
    }

    builder.SetInsertPoint(bodyBB);
    auto bodyResult = lowerBlock(*stmt.body);
    if (!bodyResult) {
      return std::unexpected(bodyResult.error());
    }
    if (!terminated()) {
      builder.CreateBr(condBB);
    }

    builder.SetInsertPoint(afterBB);
    return {};
  }

  auto lowerExpr(const Expr &expr) -> Result<llvm::Value *, Diagnostic> {
    switch (expr.kind) {
      case NodeKind::LiteralExpr: return lowerLiteral(static_cast<const LiteralExpr &>(expr));
      case NodeKind::ArrayLiteralExpr: return lowerArrayLiteral(static_cast<const ArrayLiteralExpr &>(expr));
      case NodeKind::IdentifierExpr: return lowerIdentifier(static_cast<const IdentifierExpr &>(expr));
      case NodeKind::BinaryExpr: return lowerBinary(static_cast<const BinaryExpr &>(expr));
      case NodeKind::CallExpr: return lowerCall(static_cast<const CallExpr &>(expr));
      case NodeKind::MemberExpr: return lowerMember(static_cast<const MemberExpr &>(expr));
      case NodeKind::MethodCallExpr: return lowerMethodCall(static_cast<const MethodCallExpr &>(expr));
      case NodeKind::IndexExpr: return lowerIndex(static_cast<const IndexExpr &>(expr));
      default:
        return std::unexpected(Diagnostic {
          .code = ErrorCode::CodegenError,
          .message = "Unsupported expression kind in lowering.",
          .span = expr.span,
        });
    }
  }

  auto lowerLiteral(const LiteralExpr &expr) -> Result<llvm::Value *, Diagnostic> {
    if (expr.literalKind == LiteralExpr::Kind::Int) {
      int parsed = 0;
      const auto *begin = expr.value.data();
      const auto *end = begin + expr.value.size();
      const auto parse = std::from_chars(begin, end, parsed);
      if (parse.ec != std::errc {}) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::CodegenError,
          .message = std::format("Invalid integer literal '{}'.", expr.value),
          .span = expr.span,
        });
      }
      return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), parsed);
    }
    if (expr.literalKind == LiteralExpr::Kind::Float) {
      char *endPtr = nullptr;
      const float parsed = std::strtof(expr.value.c_str(), &endPtr);
      if (endPtr == nullptr || *endPtr != '\0') {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::CodegenError,
          .message = std::format("Invalid float literal '{}'.", expr.value),
          .span = expr.span,
        });
      }
      return llvm::ConstantFP::get(llvm::Type::getFloatTy(context), parsed);
    }
    if (expr.literalKind == LiteralExpr::Kind::Bool) {
      const bool value = expr.value == "true";
      return llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), value ? 1 : 0);
    }
    if (expr.literalKind == LiteralExpr::Kind::Null) {
      return llvm::ConstantPointerNull::get(llvm::PointerType::get(context, 0));
    }

    if (expr.value.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = "String literal is too large for i32 length.",
        .span = expr.span,
      });
    }

    auto *i8Ty = llvm::Type::getInt8Ty(context);
    auto *i32Ty = llvm::Type::getInt32Ty(context);
    auto *arrayTy = llvm::ArrayType::get(i8Ty, static_cast<std::uint64_t>(expr.value.size()) + 1);
    auto *arrayConst = llvm::ConstantDataArray::getString(context, expr.value, true);
    auto *global = new llvm::GlobalVariable(
      module,
      arrayTy,
      true,
      llvm::GlobalValue::PrivateLinkage,
      arrayConst,
      "str.data"
    );
    global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    global->setAlignment(llvm::Align(1));

    auto *zero = llvm::ConstantInt::get(i32Ty, 0);
    auto *ptr = builder.CreateInBoundsGEP(arrayTy, global, {zero, zero}, "str.ptr");
    auto *len = llvm::ConstantInt::get(i32Ty, static_cast<std::int32_t>(expr.value.size()));

    auto *stringTy = llvmType(BaseType::String);
    llvm::Value *stringValue = llvm::UndefValue::get(stringTy);
    stringValue = builder.CreateInsertValue(stringValue, ptr, {0}, "str.v.ptr");
    stringValue = builder.CreateInsertValue(stringValue, len, {1}, "str.v.len");
    return stringValue;
  }

  auto lowerArrayLiteral(const ArrayLiteralExpr &expr) -> Result<llvm::Value *, Diagnostic> {
    if (expr.elements.empty()) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = "Array literal cannot be empty.",
        .span = expr.span,
      });
    }
    auto first = lowerExpr(*expr.elements.front());
    if (!first) {
      return std::unexpected(first.error());
    }
    auto *arrayTy = llvm::ArrayType::get(first.value()->getType(), static_cast<std::uint64_t>(expr.elements.size()));
    llvm::Value *arrayValue = llvm::UndefValue::get(arrayTy);
    arrayValue = builder.CreateInsertValue(arrayValue, first.value(), {0}, "arr.init");
    for (std::size_t i = 1; i < expr.elements.size(); ++i) {
      auto elem = lowerExpr(*expr.elements[i]);
      if (!elem) {
        return std::unexpected(elem.error());
      }
      arrayValue = builder.CreateInsertValue(arrayValue, elem.value(), {static_cast<unsigned int>(i)}, "arr.init");
    }
    return arrayValue;
  }

  auto lowerIdentifier(const IdentifierExpr &expr) -> Result<llvm::Value *, Diagnostic> {
    auto found = locals.find(expr.name);
    if (found == locals.end()) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = std::format("Unknown variable '{}'.", expr.name),
        .span = expr.span,
      });
    }
    return builder.CreateLoad(found->second.slot->getAllocatedType(), found->second.slot, expr.name);
  }

  auto lowerArrayElementPtr(const std::string &arrayName, const Expr &indexExpr, const SourceSpan &span)
    -> Result<llvm::Value *, Diagnostic> {
    auto found = locals.find(arrayName);
    if (found == locals.end()) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = std::format("Unknown array variable '{}'.", arrayName),
        .span = span,
      });
    }
    auto *slotTy = found->second.slot->getAllocatedType();
    auto *arrayTy = llvm::dyn_cast<llvm::ArrayType>(slotTy);
    if (!arrayTy) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = std::format("'{}' is not an array.", arrayName),
        .span = span,
      });
    }
    auto indexValue = lowerExpr(indexExpr);
    if (!indexValue) {
      return std::unexpected(indexValue.error());
    }
    auto *i32Ty = llvm::Type::getInt32Ty(context);
    llvm::Value *normalizedIndex = indexValue.value();
    if (!normalizedIndex->getType()->isIntegerTy(32)) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = "Array index must lower to i32.",
        .span = indexExpr.span,
      });
    }
    auto *zero = llvm::ConstantInt::get(i32Ty, 0);
    return builder.CreateInBoundsGEP(arrayTy, found->second.slot, {zero, normalizedIndex}, "arr.elem.ptr");
  }

  auto lowerIndex(const IndexExpr &expr) -> Result<llvm::Value *, Diagnostic> {
    if (expr.array->kind == NodeKind::IdentifierExpr) {
      const auto &id = static_cast<const IdentifierExpr &>(*expr.array);
      auto found = locals.find(id.name);
      if (found == locals.end()) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::CodegenError,
          .message = std::format("Unknown array variable '{}'.", id.name),
          .span = expr.span,
        });
      }
      auto *arrayTy = llvm::dyn_cast<llvm::ArrayType>(found->second.slot->getAllocatedType());
      if (!arrayTy) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::CodegenError,
          .message = std::format("'{}' is not an array.", id.name),
          .span = expr.span,
        });
      }
      auto elemPtr = lowerArrayElementPtr(id.name, *expr.index, expr.span);
      if (!elemPtr) {
        return std::unexpected(elemPtr.error());
      }
      return builder.CreateLoad(arrayTy->getElementType(), elemPtr.value(), "arr.idx.load");
    }

    auto arrayValue = lowerExpr(*expr.array);
    if (!arrayValue) {
      return std::unexpected(arrayValue.error());
    }
    auto *arrayTy = llvm::dyn_cast<llvm::ArrayType>(arrayValue.value()->getType());
    if (!arrayTy) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = "Index access requires an array value.",
        .span = expr.span,
      });
    }
    auto *tmp = builder.CreateAlloca(arrayTy, nullptr, "arr.idx.tmp");
    builder.CreateStore(arrayValue.value(), tmp);
    auto indexValue = lowerExpr(*expr.index);
    if (!indexValue) {
      return std::unexpected(indexValue.error());
    }
    if (!indexValue.value()->getType()->isIntegerTy(32)) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = "Array index must lower to i32.",
        .span = expr.index->span,
      });
    }
    auto *zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0);
    auto *elemPtr = builder.CreateInBoundsGEP(arrayTy, tmp, {zero, indexValue.value()}, "arr.elem.ptr");
    return builder.CreateLoad(arrayTy->getElementType(), elemPtr, "arr.idx.load");
  }

  auto lowerMember(const MemberExpr &expr) -> Result<llvm::Value *, Diagnostic> {
    llvm::Value *basePtr = nullptr;
    const StructLayout *layout = nullptr;
    if (expr.object->kind == NodeKind::IdentifierExpr) {
      const auto &id = static_cast<const IdentifierExpr &>(*expr.object);
      auto found = locals.find(id.name);
      if (found != locals.end() && found->second.type == BaseType::Struct) {
        auto layoutIt = structLayouts.find(found->second.structName);
        if (layoutIt != structLayouts.end()) {
          layout = &layoutIt->second;
          if (found->second.slot->getAllocatedType()->isPointerTy()) {
            basePtr = builder.CreateLoad(found->second.slot->getAllocatedType(), found->second.slot, "self.ptr");
          } else {
            basePtr = found->second.slot;
          }
        }
      }
    }
    if (layout == nullptr || basePtr == nullptr) {
      auto object = lowerExpr(*expr.object);
      if (!object) {
        return std::unexpected(object.error());
      }
      layout = findStructLayoutByType(object.value()->getType());
      if (layout == nullptr) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::CodegenError,
          .message = "Member access requires a struct value.",
          .span = expr.span,
        });
      }
      auto *tmp = builder.CreateAlloca(layout->llvmType, nullptr, "member.base");
      builder.CreateStore(object.value(), tmp);
      basePtr = tmp;
    }

    auto fieldIt = layout->fieldIndices.find(expr.member);
    if (fieldIt == layout->fieldIndices.end()) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = std::format("Unknown field '{}'.", expr.member),
        .span = expr.span,
      });
    }
    auto *idx0 = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0);
    auto *idxN = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), static_cast<std::uint32_t>(fieldIt->second));
    auto *fieldPtr = builder.CreateInBoundsGEP(layout->llvmType, basePtr, {idx0, idxN}, "member.ptr");
    auto *fieldTy = layout->llvmType->getElementType(static_cast<unsigned int>(fieldIt->second));
    return builder.CreateLoad(fieldTy, fieldPtr, "member.load");
  }

  auto lowerMethodCall(const MethodCallExpr &expr) -> Result<llvm::Value *, Diagnostic> {
    auto objectValue = lowerExpr(*expr.object);
    if (!objectValue) {
      return std::unexpected(objectValue.error());
    }
    const bool isStringMethod =
      objectValue.value()->getType() == llvmType(BaseType::String) ||
      (expr.object->inferredType && expr.object->inferredType->base == BaseType::String);
    if (isStringMethod) {
      auto ptrValue = extractStringPointer(objectValue.value(), expr.span, "str.method");
      if (!ptrValue) {
        return std::unexpected(ptrValue.error());
      }

      if (expr.method == "length") {
        if (!expr.args.empty()) {
          return std::unexpected(Diagnostic {
            .code = ErrorCode::CodegenError,
            .message = "String.length() does not take arguments.",
            .span = expr.span,
          });
        }
        return builder.CreateCall(strLenFn, {ptrValue.value()}, "str.length");
      }

      if (expr.method == "substr") {
        if (expr.args.size() != 2) {
          return std::unexpected(Diagnostic {
            .code = ErrorCode::CodegenError,
            .message = "String.substr(start, len) expects two arguments.",
            .span = expr.span,
          });
        }
        auto startValue = lowerExpr(*expr.args[0]);
        if (!startValue) {
          return std::unexpected(startValue.error());
        }
        auto lenValue = lowerExpr(*expr.args[1]);
        if (!lenValue) {
          return std::unexpected(lenValue.error());
        }
        auto *newPtr = builder.CreateCall(strSubstrFn, {ptrValue.value(), startValue.value(), lenValue.value()}, "str.substr.ptr");
        return packCStringValue(newPtr);
      }

      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = std::format("Unknown string method '{}'.", expr.method),
        .span = expr.span,
      });
    }

    std::string structName {};
    llvm::Value *selfPtr = nullptr;

    if (expr.object->kind == NodeKind::IdentifierExpr) {
      const auto &id = static_cast<const IdentifierExpr &>(*expr.object);
      auto found = locals.find(id.name);
      if (found != locals.end() && found->second.type == BaseType::Struct) {
        structName = found->second.structName;
        if (found->second.slot->getAllocatedType()->isPointerTy()) {
          selfPtr = builder.CreateLoad(found->second.slot->getAllocatedType(), found->second.slot, "self.ptr");
        } else {
          selfPtr = found->second.slot;
        }
      }
    }

    if (selfPtr == nullptr || structName.empty()) {
      const auto *layout = findStructLayoutByType(objectValue.value()->getType());
      if (layout == nullptr) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::CodegenError,
          .message = "Method call requires a struct receiver.",
          .span = expr.span,
        });
      }
      for (const auto &[name, candidate] : structLayouts) {
        if (candidate.llvmType == layout->llvmType) {
          structName = name;
          break;
        }
      }
      auto *tmp = builder.CreateAlloca(layout->llvmType, nullptr, "method.self.tmp");
      builder.CreateStore(objectValue.value(), tmp);
      selfPtr = tmp;
    }

    const auto mangled = std::format("{}_{}", structName, expr.method);
    auto *callee = module.getFunction(mangled);
    if (!callee) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = std::format("Unknown method '{}.{}'.", structName, expr.method),
        .span = expr.span,
      });
    }
    llvm::SmallVector<llvm::Value *> args {};
    args.push_back(selfPtr);
    auto *fnType = callee->getFunctionType();
    for (std::size_t i = 0; i < expr.args.size(); ++i) {
      const auto &argExpr = expr.args[i];
      auto lowered = lowerExpr(*argExpr);
      if (!lowered) {
        return std::unexpected(lowered.error());
      }
      auto *argValue = lowered.value();
      const auto paramIndex = static_cast<unsigned int>(i + 1);
      if (paramIndex < fnType->getNumParams()) {
        auto *expectedTy = fnType->getParamType(paramIndex);
        if (expectedTy->isPointerTy() && argValue->getType() == llvmType(BaseType::String)) {
          argValue = builder.CreateExtractValue(argValue, {0}, "mcall.str.arg.ptr");
        }
      }
      args.push_back(argValue);
    }
    while (args.size() < fnType->getNumParams()) {
      auto *expectedTy = fnType->getParamType(static_cast<unsigned int>(args.size()));
      if (expectedTy->isPointerTy()) {
        args.push_back(llvm::ConstantPointerNull::get(llvm::PointerType::get(context, 0)));
      } else if (expectedTy->isIntegerTy()) {
        args.push_back(llvm::ConstantInt::get(expectedTy, 0));
      } else if (expectedTy->isFloatTy() || expectedTy->isDoubleTy()) {
        args.push_back(llvm::ConstantFP::get(expectedTy, 0.0));
      } else {
        args.push_back(llvm::UndefValue::get(expectedTy));
      }
    }
    if (callee->getReturnType()->isVoidTy()) {
      builder.CreateCall(callee, args);
      return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0);
    }
    return builder.CreateCall(callee, args, "mcalltmp");
  }

  auto lowerBinary(const BinaryExpr &expr) -> Result<llvm::Value *, Diagnostic> {
    const auto overloadMethod = overloadedMethodFor(expr.op);
    if (!overloadMethod.empty()) {
      std::string structName {};
      llvm::Value *selfPtr = nullptr;

      if (expr.left->kind == NodeKind::IdentifierExpr) {
        const auto &id = static_cast<const IdentifierExpr &>(*expr.left);
        auto found = locals.find(id.name);
      if (found != locals.end() && found->second.type == BaseType::Struct) {
          structName = found->second.structName;
          if (structName.empty() && found->second.declaredType && found->second.declaredType->base == BaseType::Struct) {
            structName = found->second.declaredType->name;
          }
          if (found->second.slot->getAllocatedType()->isPointerTy()) {
            selfPtr = builder.CreateLoad(found->second.slot->getAllocatedType(), found->second.slot, "op.self.ptr");
          } else {
            selfPtr = found->second.slot;
          }
        }
      } else if (expr.left->kind == NodeKind::CallExpr) {
        const auto &call = static_cast<const CallExpr &>(*expr.left);
        if (structLayouts.contains(call.callee)) {
          structName = call.callee;
        }
      }

      if (!structName.empty()) {
        const auto mangled = std::format("{}_{}", structName, overloadMethod);
        auto *callee = module.getFunction(mangled);
        if (callee != nullptr) {
          if (selfPtr == nullptr) {
            auto leftValue = lowerExpr(*expr.left);
            if (!leftValue) {
              return std::unexpected(leftValue.error());
            }
            auto *leftLayout = findStructLayoutByType(leftValue.value()->getType());
            if (leftLayout == nullptr) {
              return std::unexpected(Diagnostic {
                .code = ErrorCode::CodegenError,
                .message = "Invalid struct receiver in overloaded operator.",
                .span = expr.left->span,
              });
            }
            auto *tmp = builder.CreateAlloca(leftLayout->llvmType, nullptr, "op.self.tmp");
            builder.CreateStore(leftValue.value(), tmp);
            selfPtr = tmp;
          }

          auto rightValue = lowerExpr(*expr.right);
          if (!rightValue) {
            return std::unexpected(rightValue.error());
          }
          llvm::SmallVector<llvm::Value *> args {};
          args.push_back(selfPtr);
          args.push_back(rightValue.value());
          return builder.CreateCall(callee, args, "op.calltmp");
        }
      }
    }

    auto lhs = lowerExpr(*expr.left);
    if (!lhs) {
      return std::unexpected(lhs.error());
    }
    auto rhs = lowerExpr(*expr.right);
    if (!rhs) {
      return std::unexpected(rhs.error());
    }

    llvm::Value *lhsValue = lhs.value();
    llvm::Value *rhsValue = rhs.value();

    auto *stringTy = llvmType(BaseType::String);
    const bool lhsIsString = inferExprType(*expr.left) == BaseType::String || lhsValue->getType() == stringTy;
    const bool rhsIsString = inferExprType(*expr.right) == BaseType::String || rhsValue->getType() == stringTy;
    if (expr.op == BinaryOp::Add && lhsIsString && rhsIsString) {
      auto leftPtr = extractStringPointer(lhsValue, expr.left->span, "str.left");
      if (!leftPtr) {
        return std::unexpected(leftPtr.error());
      }
      auto rightPtr = extractStringPointer(rhsValue, expr.right->span, "str.right");
      if (!rightPtr) {
        return std::unexpected(rightPtr.error());
      }
      auto *added = builder.CreateCall(strAddFn, {leftPtr.value(), rightPtr.value()}, "str.add.ptr");
      return packCStringValue(added);
    }
    if (expr.op == BinaryOp::Eq && lhsIsString && rhsIsString) {
      auto leftPtr = extractStringPointer(lhsValue, expr.left->span, "str.left");
      if (!leftPtr) {
        return std::unexpected(leftPtr.error());
      }
      auto rightPtr = extractStringPointer(rhsValue, expr.right->span, "str.right");
      if (!rightPtr) {
        return std::unexpected(rightPtr.error());
      }
      auto *eqValue = builder.CreateCall(strEqFn, {leftPtr.value(), rightPtr.value()}, "str.eq");
      return builder.CreateICmpNE(eqValue, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0), "str.eq.bool");
    }
    if (expr.op == BinaryOp::Ne && lhsIsString && rhsIsString) {
      auto leftPtr = extractStringPointer(lhsValue, expr.left->span, "str.left");
      if (!leftPtr) {
        return std::unexpected(leftPtr.error());
      }
      auto rightPtr = extractStringPointer(rhsValue, expr.right->span, "str.right");
      if (!rightPtr) {
        return std::unexpected(rightPtr.error());
      }
      auto *eqValue = builder.CreateCall(strEqFn, {leftPtr.value(), rightPtr.value()}, "str.eq");
      return builder.CreateICmpEQ(eqValue, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0), "str.ne.bool");
    }

    const bool isComparison =
      expr.op == BinaryOp::Eq ||
      expr.op == BinaryOp::Ne ||
      expr.op == BinaryOp::Lt ||
      expr.op == BinaryOp::Le ||
      expr.op == BinaryOp::Gt ||
      expr.op == BinaryOp::Ge;
    if (isComparison && lhsValue->getType() != rhsValue->getType()) {
      if (lhsValue->getType() == stringTy && rhsValue->getType()->isPointerTy()) {
        auto ptr = extractStringPointer(lhsValue, expr.left->span, "cmp.lhs");
        if (!ptr) {
          return std::unexpected(ptr.error());
        }
        lhsValue = ptr.value();
      } else if (rhsValue->getType() == stringTy && lhsValue->getType()->isPointerTy()) {
        auto ptr = extractStringPointer(rhsValue, expr.right->span, "cmp.rhs");
        if (!ptr) {
          return std::unexpected(ptr.error());
        }
        rhsValue = ptr.value();
      }
    }
    if (isComparison && lhsValue->getType() != rhsValue->getType()) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = "Comparison operands lowered to incompatible types.",
        .span = expr.span,
      });
    }
    if (isComparison) {
      const bool scalarComparable =
        lhsValue->getType()->isIntegerTy() ||
        lhsValue->getType()->isFloatingPointTy() ||
        lhsValue->getType()->isPointerTy();
      if (!scalarComparable) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::CodegenError,
          .message = "Comparison operands must be numeric, pointer, or string.",
          .span = expr.span,
        });
      }
    }

    switch (expr.op) {
      case BinaryOp::Add:
        if (lhsValue->getType()->isFloatingPointTy() && rhsValue->getType()->isFloatingPointTy()) {
          return builder.CreateFAdd(lhsValue, rhsValue, "faddtmp");
        }
        return builder.CreateAdd(lhsValue, rhsValue, "addtmp");
      case BinaryOp::Sub:
        if (lhsValue->getType()->isFloatingPointTy() && rhsValue->getType()->isFloatingPointTy()) {
          return builder.CreateFSub(lhsValue, rhsValue, "fsubtmp");
        }
        return builder.CreateSub(lhsValue, rhsValue, "subtmp");
      case BinaryOp::Mul:
        if (lhsValue->getType()->isFloatingPointTy() && rhsValue->getType()->isFloatingPointTy()) {
          return builder.CreateFMul(lhsValue, rhsValue, "fmultmp");
        }
        return builder.CreateMul(lhsValue, rhsValue, "multmp");
      case BinaryOp::Div:
        if (lhsValue->getType()->isFloatingPointTy() && rhsValue->getType()->isFloatingPointTy()) {
          return builder.CreateFDiv(lhsValue, rhsValue, "fdivtmp");
        }
        return builder.CreateSDiv(lhsValue, rhsValue, "divtmp");
      case BinaryOp::And: {
        auto lhsBool = ensureBool(lhsValue, expr.left->span);
        if (!lhsBool) {
          return std::unexpected(lhsBool.error());
        }
        auto rhsBool = ensureBool(rhsValue, expr.right->span);
        if (!rhsBool) {
          return std::unexpected(rhsBool.error());
        }
        return builder.CreateAnd(lhsBool.value(), rhsBool.value(), "andtmp");
      }
      case BinaryOp::Or: {
        auto lhsBool = ensureBool(lhsValue, expr.left->span);
        if (!lhsBool) {
          return std::unexpected(lhsBool.error());
        }
        auto rhsBool = ensureBool(rhsValue, expr.right->span);
        if (!rhsBool) {
          return std::unexpected(rhsBool.error());
        }
        return builder.CreateOr(lhsBool.value(), rhsBool.value(), "ortmp");
      }
      case BinaryOp::Eq:
        if (lhsValue->getType()->isFloatingPointTy() && rhsValue->getType()->isFloatingPointTy()) {
          return builder.CreateFCmpOEQ(lhsValue, rhsValue, "feqtmp");
        }
        return builder.CreateICmpEQ(lhsValue, rhsValue, "eqtmp");
      case BinaryOp::Ne:
        if (lhsValue->getType()->isFloatingPointTy() && rhsValue->getType()->isFloatingPointTy()) {
          return builder.CreateFCmpONE(lhsValue, rhsValue, "fnetmp");
        }
        return builder.CreateICmpNE(lhsValue, rhsValue, "netmp");
      case BinaryOp::Lt:
        if (lhsValue->getType()->isFloatingPointTy() && rhsValue->getType()->isFloatingPointTy()) {
          return builder.CreateFCmpOLT(lhsValue, rhsValue, "flttmp");
        }
        return builder.CreateICmpSLT(lhsValue, rhsValue, "lttmp");
      case BinaryOp::Le:
        if (lhsValue->getType()->isFloatingPointTy() && rhsValue->getType()->isFloatingPointTy()) {
          return builder.CreateFCmpOLE(lhsValue, rhsValue, "fletmp");
        }
        return builder.CreateICmpSLE(lhsValue, rhsValue, "letmp");
      case BinaryOp::Gt:
        if (lhsValue->getType()->isFloatingPointTy() && rhsValue->getType()->isFloatingPointTy()) {
          return builder.CreateFCmpOGT(lhsValue, rhsValue, "fgttmp");
        }
        return builder.CreateICmpSGT(lhsValue, rhsValue, "gttmp");
      case BinaryOp::Ge:
        if (lhsValue->getType()->isFloatingPointTy() && rhsValue->getType()->isFloatingPointTy()) {
          return builder.CreateFCmpOGE(lhsValue, rhsValue, "fgetmp");
        }
        return builder.CreateICmpSGE(lhsValue, rhsValue, "getmp");
    }
    return std::unexpected(Diagnostic {
      .code = ErrorCode::CodegenError,
      .message = "Unknown binary operator.",
      .span = expr.span,
    });
  }

  auto lowerCall(const CallExpr &expr) -> Result<llvm::Value *, Diagnostic> {
    if (expr.callee == "__thg_throw") {
      if (expr.args.size() != 1) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::CodegenError,
          .message = "Builtin throw expects exactly one argument.",
          .span = expr.span,
        });
      }
      auto arg = lowerExpr(*expr.args[0]);
      if (!arg) {
        return std::unexpected(arg.error());
      }
      auto *voidTy = llvm::Type::getVoidTy(context);
      auto *i32Ty = llvm::Type::getInt32Ty(context);
      auto throwFn = module.getOrInsertFunction("__thg_throw", llvm::FunctionType::get(voidTy, {llvm::PointerType::get(context, 0)}, false));
      llvm::Value *msgPtr = llvm::ConstantPointerNull::get(llvm::PointerType::get(context, 0));
      if (arg.value()->getType() == llvmType(BaseType::String)) {
        msgPtr = builder.CreateExtractValue(arg.value(), {0}, "throw.msg.ptr");
      } else if (arg.value()->getType()->isPointerTy()) {
        msgPtr = arg.value();
      }
      builder.CreateCall(throwFn, {msgPtr});
      return llvm::ConstantInt::get(i32Ty, 0);
    }
    if (expr.callee == "print") {
      if (expr.args.size() != 1) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::CodegenError,
          .message = "Builtin print expects exactly one argument.",
          .span = expr.span,
        });
      }
      auto arg = lowerExpr(*expr.args[0]);
      if (!arg) {
        return std::unexpected(arg.error());
      }
      auto *voidTy = llvm::Type::getVoidTy(context);
      auto *i32Ty = llvm::Type::getInt32Ty(context);

      if (arg.value()->getType()->isIntegerTy(32)) {
        auto printFn = module.getOrInsertFunction("__thg_print_i32", llvm::FunctionType::get(voidTy, {i32Ty}, false));
        builder.CreateCall(printFn, {arg.value()});
        return llvm::ConstantInt::get(i32Ty, 0);
      }
      if (arg.value()->getType()->isFloatTy()) {
        auto *f32Ty = llvm::Type::getFloatTy(context);
        auto printFn = module.getOrInsertFunction("__thg_print_f32", llvm::FunctionType::get(voidTy, {f32Ty}, false));
        builder.CreateCall(printFn, {arg.value()});
        return llvm::ConstantInt::get(i32Ty, 0);
      }

      auto *stringTy = llvmType(BaseType::String);
      if (arg.value()->getType() == stringTy) {
        auto *ptr = builder.CreateExtractValue(arg.value(), {0}, "str.ptr");
        auto *len = builder.CreateExtractValue(arg.value(), {1}, "str.len");
        auto printFn = module.getOrInsertFunction(
          "__thg_print_str",
          llvm::FunctionType::get(voidTy, {llvm::PointerType::get(context, 0), i32Ty}, false)
        );
        builder.CreateCall(printFn, {ptr, len});
        return llvm::ConstantInt::get(i32Ty, 0);
      }
      if (arg.value()->getType()->isPointerTy()) {
        auto printFn = module.getOrInsertFunction(
          "__thg_print_ptr",
          llvm::FunctionType::get(voidTy, {llvm::PointerType::get(context, 0)}, false)
        );
        builder.CreateCall(printFn, {arg.value()});
        return llvm::ConstantInt::get(i32Ty, 0);
      }

      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = "Builtin print supports only i32, f32, string or ptr.",
        .span = expr.args[0]->span,
      });
    }

    if (auto st = structLayouts.find(expr.callee); st != structLayouts.end()) {
      if (expr.args.size() != st->second.fields.size()) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::CodegenError,
          .message = std::format("Struct '{}' constructor expects {} argument(s), got {}.", expr.callee, st->second.fields.size(), expr.args.size()),
          .span = expr.span,
        });
      }
      llvm::Value *value = llvm::UndefValue::get(st->second.llvmType);
      for (std::size_t i = 0; i < expr.args.size(); ++i) {
        auto lowered = lowerExpr(*expr.args[i]);
        if (!lowered) {
          return std::unexpected(lowered.error());
        }
        value = builder.CreateInsertValue(value, lowered.value(), {static_cast<unsigned int>(i)}, "struct.init");
      }
      return value;
    }

    auto *callee = module.getFunction(expr.callee);
    if (!callee) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = std::format("Unknown function '{}'.", expr.callee),
        .span = expr.span,
      });
    }
    llvm::SmallVector<llvm::Value *> args {};
    auto *fnType = callee->getFunctionType();
    for (std::size_t i = 0; i < expr.args.size(); ++i) {
      const auto &arg = expr.args[i];
      auto lowered = lowerExpr(*arg);
      if (!lowered) {
        return std::unexpected(lowered.error());
      }
      auto *argValue = lowered.value();
      if (i < fnType->getNumParams()) {
        auto *expectedTy = fnType->getParamType(static_cast<unsigned int>(i));
        if (expectedTy->isPointerTy() && argValue->getType() == llvmType(BaseType::String)) {
          argValue = builder.CreateExtractValue(argValue, {0}, "str.arg.ptr");
        }
      }
      args.push_back(argValue);
    }
    while (args.size() < fnType->getNumParams()) {
      auto *expectedTy = fnType->getParamType(static_cast<unsigned int>(args.size()));
      if (expectedTy->isPointerTy()) {
        args.push_back(llvm::ConstantPointerNull::get(llvm::PointerType::get(context, 0)));
      } else if (expectedTy->isIntegerTy()) {
        args.push_back(llvm::ConstantInt::get(expectedTy, 0));
      } else if (expectedTy->isFloatTy() || expectedTy->isDoubleTy()) {
        args.push_back(llvm::ConstantFP::get(expectedTy, 0.0));
      } else {
        args.push_back(llvm::UndefValue::get(expectedTy));
      }
    }
    if (callee->getReturnType()->isVoidTy()) {
      builder.CreateCall(callee, args);
      return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0);
    }
    auto *callValue = builder.CreateCall(callee, args, "calltmp");
    BaseType declaredReturn = BaseType::Unknown;
    if (auto it = functionReturnKinds.find(expr.callee); it != functionReturnKinds.end()) {
      declaredReturn = it->second;
    } else if (expr.inferredType) {
      declaredReturn = expr.inferredType->base;
    }
    if (callee->getReturnType()->isPointerTy() && declaredReturn == BaseType::String) {
      return packCStringValue(callValue);
    }
    return callValue;
  }

  auto inferExprType(const Expr &expr) -> BaseType {
    if (expr.inferredType) {
      return expr.inferredType->base;
    }
    if (expr.kind == NodeKind::LiteralExpr) {
      const auto &lit = static_cast<const LiteralExpr &>(expr);
      if (lit.literalKind == LiteralExpr::Kind::String) {
        return BaseType::String;
      }
      if (lit.literalKind == LiteralExpr::Kind::Bool) {
        return BaseType::Bool;
      }
      if (lit.literalKind == LiteralExpr::Kind::Null) {
        return BaseType::Pointer;
      }
      if (lit.literalKind == LiteralExpr::Kind::Float) {
        return BaseType::F32;
      }
      return BaseType::I32;
    }
    if (expr.kind == NodeKind::ArrayLiteralExpr) {
      return BaseType::Array;
    }
    if (expr.kind == NodeKind::IdentifierExpr) {
      const auto &id = static_cast<const IdentifierExpr &>(expr);
      auto it = locals.find(id.name);
      if (it != locals.end()) {
        return it->second.type;
      }
      return BaseType::Unknown;
    }
    if (expr.kind == NodeKind::IndexExpr) {
      const auto &idx = static_cast<const IndexExpr &>(expr);
      if (idx.array->inferredType && idx.array->inferredType->base == BaseType::Array && idx.array->inferredType->elementType) {
        return idx.array->inferredType->elementType->base;
      }
      return BaseType::Unknown;
    }
    if (expr.kind == NodeKind::BinaryExpr) {
      const auto &bin = static_cast<const BinaryExpr &>(expr);
      const auto overloadMethod = overloadedMethodFor(bin.op);
      if (!overloadMethod.empty()) {
        std::string ownerName {};
        if (bin.left->kind == NodeKind::IdentifierExpr) {
          const auto &id = static_cast<const IdentifierExpr &>(*bin.left);
          auto it = locals.find(id.name);
          if (it != locals.end() && it->second.type == BaseType::Struct) {
            ownerName = it->second.structName;
            if (ownerName.empty() && it->second.declaredType && it->second.declaredType->base == BaseType::Struct) {
              ownerName = it->second.declaredType->name;
            }
          }
        } else if (bin.left->kind == NodeKind::CallExpr) {
          const auto &call = static_cast<const CallExpr &>(*bin.left);
          if (structLayouts.contains(call.callee)) {
            ownerName = call.callee;
          }
        }
        if (!ownerName.empty()) {
          const auto mangled = std::format("{}_{}", ownerName, overloadMethod);
          auto *fn = module.getFunction(mangled);
          if (fn != nullptr) {
            if (fn->getReturnType()->isIntegerTy(32)) {
              return BaseType::I32;
            }
            if (fn->getReturnType()->isFloatTy()) {
              return BaseType::F32;
            }
            if (fn->getReturnType()->isDoubleTy()) {
              return BaseType::F64;
            }
            if (fn->getReturnType()->isIntegerTy(1)) {
              return BaseType::Bool;
            }
            if (fn->getReturnType() == llvmType(BaseType::String)) {
              return BaseType::String;
            }
            if (findStructLayoutByType(fn->getReturnType()) != nullptr) {
              return BaseType::Struct;
            }
          }
        }
      }
      const auto lhs = inferExprType(*bin.left);
      const auto rhs = inferExprType(*bin.right);
      switch (bin.op) {
        case BinaryOp::Add:
          if (lhs == BaseType::String && rhs == BaseType::String) {
            return BaseType::String;
          }
          if (lhs == BaseType::F32 && rhs == BaseType::F32) {
            return BaseType::F32;
          }
          if (lhs == BaseType::F64 && rhs == BaseType::F64) {
            return BaseType::F64;
          }
          return BaseType::I32;
        case BinaryOp::Sub:
        case BinaryOp::Mul:
        case BinaryOp::Div:
          if (lhs == BaseType::F32 && rhs == BaseType::F32) {
            return BaseType::F32;
          }
          if (lhs == BaseType::F64 && rhs == BaseType::F64) {
            return BaseType::F64;
          }
          return BaseType::I32;
        case BinaryOp::Eq:
        case BinaryOp::Ne:
        case BinaryOp::Lt:
        case BinaryOp::Le:
        case BinaryOp::Gt:
        case BinaryOp::Ge:
        case BinaryOp::And:
        case BinaryOp::Or:
          return BaseType::Bool;
      }
    }
    if (expr.kind == NodeKind::CallExpr) {
      const auto &call = static_cast<const CallExpr &>(expr);
      if (call.callee == "print") {
        return BaseType::Void;
      }
      if (structLayouts.contains(call.callee)) {
        return BaseType::Struct;
      }
      auto *fn = module.getFunction(call.callee);
      if (fn != nullptr) {
        if (fn->getReturnType()->isVoidTy()) {
          return BaseType::Void;
        }
        if (fn->getReturnType()->isIntegerTy(32)) {
          return BaseType::I32;
        }
        if (fn->getReturnType()->isFloatTy()) {
          return BaseType::F32;
        }
        if (fn->getReturnType()->isDoubleTy()) {
          return BaseType::F64;
        }
        if (fn->getReturnType()->isIntegerTy(1)) {
          return BaseType::Bool;
        }
        if (fn->getReturnType() == llvmType(BaseType::String)) {
          return BaseType::String;
        }
        if (auto it = functionReturnKinds.find(call.callee); it != functionReturnKinds.end() && it->second == BaseType::String) {
          return BaseType::String;
        }
        if (fn->getReturnType()->isPointerTy()) {
          return BaseType::Pointer;
        }
      }
    }
    if (expr.kind == NodeKind::MethodCallExpr) {
      const auto &call = static_cast<const MethodCallExpr &>(expr);
      if (call.object->inferredType && call.object->inferredType->base == BaseType::String) {
        if (call.method == "length") {
          return BaseType::I32;
        }
        if (call.method == "substr") {
          return BaseType::String;
        }
      }
      BaseType ownerBase = BaseType::Unknown;
      std::string ownerName {};
      if (call.object->inferredType) {
        ownerBase = call.object->inferredType->base;
        ownerName = call.object->inferredType->name;
      }
      if (ownerBase == BaseType::Struct && !ownerName.empty()) {
        const auto mangled = std::format("{}_{}", ownerName, call.method);
        auto *fn = module.getFunction(mangled);
        if (fn != nullptr) {
          if (fn->getReturnType()->isIntegerTy(32)) {
            return BaseType::I32;
          }
          if (fn->getReturnType()->isFloatTy()) {
            return BaseType::F32;
          }
          if (fn->getReturnType()->isDoubleTy()) {
            return BaseType::F64;
          }
          if (fn->getReturnType()->isPointerTy()) {
            return BaseType::Pointer;
          }
          if (fn->getReturnType() == llvmType(BaseType::String)) {
            return BaseType::String;
          }
        }
      }
      return BaseType::Unknown;
    }
    if (expr.kind == NodeKind::MemberExpr) {
      const auto &member = static_cast<const MemberExpr &>(expr);
      const auto ownerType = inferExprType(*member.object);
      if (ownerType == BaseType::Struct) {
        return BaseType::I32;
      }
    }
    return BaseType::Unknown;
  }

  auto isTemporaryExpr(const Expr &expr) -> bool {
    return expr.kind == NodeKind::LiteralExpr || expr.kind == NodeKind::BinaryExpr || expr.kind == NodeKind::CallExpr;
  }
};

auto mapType(llvm::LLVMContext &context, BaseType type) -> llvm::Type * {
  switch (type) {
    case BaseType::Void: return llvm::Type::getVoidTy(context);
    case BaseType::I32: return llvm::Type::getInt32Ty(context);
    case BaseType::F32: return llvm::Type::getFloatTy(context);
    case BaseType::F64: return llvm::Type::getDoubleTy(context);
    case BaseType::Pointer: return llvm::PointerType::get(context, 0);
    case BaseType::Bool: return llvm::Type::getInt1Ty(context);
    case BaseType::String: return getStringStructType(context);
    case BaseType::Array: break;
    case BaseType::Struct: break;
    case BaseType::Unknown: return llvm::PointerType::get(context, 0);
  }
  return llvm::PointerType::get(context, 0);
}

} // namespace

IRGenerator::IRGenerator(llvm::LLVMContext &context_) : context(context_) {}

auto IRGenerator::lower(const TypedModule &typed, const std::string &moduleName)
  -> Result<std::unique_ptr<llvm::Module>, Diagnostic> {
  auto module = std::make_unique<llvm::Module>(moduleName, context);
  bool hasExplicitMain = false;
  bool hasTopLevelStatements = !typed.module->topLevelStatements.empty();
  std::unordered_map<std::string, StructLayout> structLayouts {};
  std::unordered_map<std::string, BaseType> functionReturnKinds {};

  for (const auto &st : typed.module->structs) {
    auto llvmName = std::format("thg.struct.{}", st->name);
    auto *ty = llvm::StructType::create(context, llvmName);
    StructLayout layout {};
    layout.llvmType = ty;
    layout.fields = st->fields;
    for (std::size_t i = 0; i < st->fields.size(); ++i) {
      layout.fieldIndices.emplace(st->fields[i].name, i);
    }
    structLayouts.emplace(st->name, std::move(layout));
  }

  for (const auto &st : typed.module->structs) {
    auto layoutIt = structLayouts.find(st->name);
    if (layoutIt == structLayouts.end()) {
      continue;
    }
    std::vector<llvm::Type *> body {};
    body.reserve(st->fields.size());
    for (const auto &field : st->fields) {
      body.push_back(mapDeclaredType(context, field.type, structLayouts));
    }
    layoutIt->second.llvmType->setBody(body, false);
  }

  for (const auto &decl : typed.module->functions) {
    const bool isUserMain = !decl->isExtern && decl->name == "main";
    if (isUserMain) {
      hasExplicitMain = true;
      if (!decl->params.empty()) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::CodegenError,
          .message = "func main() cannot declare parameters; use env module to read CLI args.",
          .span = decl->span,
        });
      }
      if (!decl->returnType || decl->returnType->base != BaseType::I32) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::CodegenError,
          .message = "func main() must return i32.",
          .span = decl->span,
        });
      }
    }
    std::vector<llvm::Type *> params {};
    if (isUserMain) {
      params.push_back(llvm::Type::getInt32Ty(context));
      params.push_back(llvm::PointerType::get(context, 0));
    } else {
      params.reserve(decl->params.size());
      for (std::size_t i = 0; i < decl->params.size(); ++i) {
        const auto &param = decl->params[i];
        auto *paramTy = mapDeclaredType(context, param.type, structLayouts);
        if (decl->isExtern && param.type && param.type->base == BaseType::String) {
          paramTy = llvm::PointerType::get(context, 0);
        }
        if (!decl->methodOwner.empty() && i == 0 && param.name == "self" && param.type && param.type->base == BaseType::Struct) {
          paramTy = llvm::PointerType::get(context, 0);
        }
        params.push_back(paramTy);
      }
    }

    auto *retTy = mapDeclaredType(context, decl->returnType, structLayouts);
    if (isUserMain) {
      retTy = llvm::Type::getInt32Ty(context);
    }
    if (decl->isExtern && decl->returnType && decl->returnType->base == BaseType::String) {
      retTy = llvm::PointerType::get(context, 0);
    }
    auto *fnType = llvm::FunctionType::get(retTy, params, false);
    auto *fn = llvm::Function::Create(fnType, llvm::Function::ExternalLinkage, decl->name, *module);
    functionReturnKinds.emplace(decl->name, decl->returnType ? decl->returnType->base : BaseType::Void);
    if (isUserMain) {
      auto it = fn->arg_begin();
      if (it != fn->arg_end()) {
        it->setName("argc");
        ++it;
      }
      if (it != fn->arg_end()) {
        it->setName("argv");
      }
    } else {
      std::size_t idx = 0;
      for (auto &arg : fn->args()) {
        arg.setName(decl->params[idx++].name);
      }
    }
  }

  if (hasTopLevelStatements && hasExplicitMain) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::CodegenError,
      .message = "Cannot mix top-level executable statements with explicit 'func main()'.",
      .span = typed.module->span,
    });
  }

  if (hasTopLevelStatements) {
    auto *fnType = llvm::FunctionType::get(llvm::Type::getInt32Ty(context), {}, false);
    llvm::Function::Create(fnType, llvm::Function::ExternalLinkage, "__thg_script_main", *module);
  }

  const bool needsEntryMain = hasTopLevelStatements;
  if (needsEntryMain) {
    if (module->getFunction("main") != nullptr) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = "Conflicting symbol 'main' already exists.",
        .span = typed.module->span,
      });
    }
    auto *fnType = llvm::FunctionType::get(
      llvm::Type::getInt32Ty(context),
      {llvm::Type::getInt32Ty(context), llvm::PointerType::get(context, 0)},
      false
    );
    llvm::Function::Create(fnType, llvm::Function::ExternalLinkage, "main", *module);
  }

  for (const auto &decl : typed.module->functions) {
    if (decl->isExtern) {
      continue;
    }
    auto *fn = module->getFunction(decl->name);
    FunctionLowering lowering {context, *module, *fn, structLayouts, functionReturnKinds};
    auto lowered = lowering.lower(*decl);
    if (!lowered) {
      return std::unexpected(lowered.error());
    }
  }

  if (hasTopLevelStatements) {
    auto *scriptMain = module->getFunction("__thg_script_main");
    if (scriptMain == nullptr) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = "Failed to create implicit main function.",
        .span = typed.module->span,
      });
    }
    FunctionLowering lowering {context, *module, *scriptMain, structLayouts, functionReturnKinds};
    auto lowered = lowering.lowerStatements(typed.module->topLevelStatements, BaseType::I32);
    if (!lowered) {
      return std::unexpected(lowered.error());
    }
  }

  if (needsEntryMain) {
    auto *entryMain = module->getFunction("main");
    if (entryMain == nullptr) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = "Failed to create entry main function.",
        .span = typed.module->span,
      });
    }

    auto *entryBlock = llvm::BasicBlock::Create(context, "entry", entryMain);
    llvm::IRBuilder<> builder {entryBlock};
    auto argcIt = entryMain->arg_begin();
    llvm::Value *argc = &*argcIt;
    argc->setName("argc");
    ++argcIt;
    llvm::Value *argv = &*argcIt;
    argv->setName("argv");

    auto initEnv = module->getOrInsertFunction(
      "__thg_init_env",
      llvm::FunctionType::get(llvm::Type::getVoidTy(context), {llvm::Type::getInt32Ty(context), llvm::PointerType::get(context, 0)}, false)
    );
    builder.CreateCall(initEnv, {argc, argv});

    auto *scriptMain = module->getFunction("__thg_script_main");
    if (scriptMain == nullptr) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = "Missing internal script entry function.",
        .span = typed.module->span,
      });
    }
    auto *result = builder.CreateCall(scriptMain, {}, "script.ret");
    builder.CreateRet(result);
  }

  if (llvm::verifyModule(*module, &llvm::errs())) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::CodegenError,
      .message = "LLVM module verification failed.",
      .span = {},
    });
  }

  return module;
}

auto BackendPipeline::optimizeModule(llvm::Module &module, int optLevel) -> Result<void, Diagnostic> {
  llvm::LoopAnalysisManager loopAM;
  llvm::FunctionAnalysisManager functionAM;
  llvm::CGSCCAnalysisManager cgsccAM;
  llvm::ModuleAnalysisManager moduleAM;
  llvm::PassBuilder passBuilder;

  passBuilder.registerModuleAnalyses(moduleAM);
  passBuilder.registerCGSCCAnalyses(cgsccAM);
  passBuilder.registerFunctionAnalyses(functionAM);
  passBuilder.registerLoopAnalyses(loopAM);
  passBuilder.crossRegisterProxies(loopAM, functionAM, cgsccAM, moduleAM);

  llvm::OptimizationLevel level = llvm::OptimizationLevel::O2;
  if (optLevel <= 0) {
    level = llvm::OptimizationLevel::O0;
  } else if (optLevel == 1) {
    level = llvm::OptimizationLevel::O1;
  } else if (optLevel == 3) {
    level = llvm::OptimizationLevel::O3;
  }

  auto modulePassManager = passBuilder.buildPerModuleDefaultPipeline(level);
  modulePassManager.run(module, moduleAM);
  return {};
}

auto BackendPipeline::emitIR(const llvm::Module &module, const std::string &outPath) -> Result<void, Diagnostic> {
  std::error_code ec;
  llvm::raw_fd_ostream out(outPath, ec, llvm::sys::fs::OF_Text);
  if (ec) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::CodegenError,
      .message = std::format("Unable to open output IR file '{}': {}", outPath, ec.message()),
      .span = {},
    });
  }
  module.print(out, nullptr);
  return {};
}

auto BackendPipeline::emitObject(llvm::Module &module, llvm::TargetMachine &targetMachine, const std::string &outPath)
  -> Result<void, Diagnostic> {
  std::error_code ec;
  llvm::raw_fd_ostream out(outPath, ec, llvm::sys::fs::OF_None);
  if (ec) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::CodegenError,
      .message = std::format("Unable to open output object file '{}': {}", outPath, ec.message()),
      .span = {},
    });
  }

  llvm::legacy::PassManager passManager;
  auto fileType = llvm::CGFT_ObjectFile;
  if (targetMachine.addPassesToEmitFile(passManager, out, nullptr, fileType)) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::CodegenError,
      .message = "Target machine cannot emit object file for this target.",
      .span = {},
    });
  }
  passManager.run(module);
  out.flush();
  return {};
}

} // namespace thagore
