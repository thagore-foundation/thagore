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
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace thagore {
namespace {

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
    return llvm::Type::getInt32Ty(context);
  }
  switch (type->base) {
    case BaseType::Void: return llvm::Type::getVoidTy(context);
    case BaseType::I32: return llvm::Type::getInt32Ty(context);
    case BaseType::Bool: return llvm::Type::getInt1Ty(context);
    case BaseType::String: return getStringStructType(context);
    case BaseType::Struct: {
      auto it = structLayouts.find(type->name);
      if (it != structLayouts.end()) {
        return it->second.llvmType;
      }
      return llvm::Type::getInt32Ty(context);
    }
    case BaseType::Unknown: break;
  }
  return llvm::Type::getInt32Ty(context);
}

class FunctionLowering {
public:
  FunctionLowering(
    llvm::LLVMContext &ctx,
    llvm::Module &mod,
    llvm::Function &fn,
    const std::unordered_map<std::string, StructLayout> &structLayouts_
  )
    : context(ctx), module(mod), function(fn), builder(ctx), structLayouts(structLayouts_) {
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
    entryBuilder.SetInsertPoint(entry);
    pushScope();
    if (decl != nullptr) {
      auto paramResult = bindFunctionParams(*decl);
      if (!paramResult) {
        return std::unexpected(paramResult.error());
      }
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
  llvm::IRBuilder<> entryBuilder {context};
  std::unordered_map<std::string, LocalValue> locals {};
  std::vector<std::vector<std::string>> scopeLocals {};
  const std::unordered_map<std::string, StructLayout> &structLayouts;
  llvm::FunctionCallee retainFn {};
  llvm::FunctionCallee releaseFn {};
  llvm::FunctionCallee concatFn {};

  auto terminated() const -> bool {
    auto *block = builder.GetInsertBlock();
    return block && block->getTerminator() != nullptr;
  }

  void declareRuntimeHooks() {
    auto *voidTy = llvm::Type::getVoidTy(context);
    auto *ptrTy = llvm::PointerType::get(context, 0);
    auto *i32Ty = llvm::Type::getInt32Ty(context);
    auto *stringTy = getStringStructType(context);
    retainFn = module.getOrInsertFunction("__thg_retain", llvm::FunctionType::get(voidTy, {ptrTy}, false));
    releaseFn = module.getOrInsertFunction("__thg_release", llvm::FunctionType::get(voidTy, {ptrTy}, false));
    concatFn =
      module.getOrInsertFunction("__thg_str_concat", llvm::FunctionType::get(ptrTy, {ptrTy, i32Ty, ptrTy, i32Ty, ptrTy}, false));
  }

  auto llvmType(BaseType type) -> llvm::Type * {
    switch (type) {
      case BaseType::Void: return llvm::Type::getVoidTy(context);
      case BaseType::I32: return llvm::Type::getInt32Ty(context);
      case BaseType::Bool: return llvm::Type::getInt1Ty(context);
      case BaseType::String: return getStringStructType(context);
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
      builder.CreateStore(&arg, slot);

      const bool isString = arg.getType() == llvmType(BaseType::String);
      if (isString) {
        retainStringValue(&arg);
      }
      locals.emplace(
        param.name,
        LocalValue {
          .slot = slot,
          .type = isString ? BaseType::String : (param.type ? param.type->base : BaseType::Unknown),
          .structName = (param.type && param.type->base == BaseType::Struct) ? param.type->name : "",
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
          releaseStringValue(loaded);
        } else if (loaded->getType()->isPointerTy()) {
          builder.CreateCall(releaseFn, {loaded});
        }
      }
      locals.erase(localIt);
    }
  }

  auto lowerStmt(const Stmt &stmt) -> Result<void, Diagnostic> {
    switch (stmt.kind) {
      case NodeKind::LetStmt: return lowerLet(static_cast<const LetStmt &>(stmt));
      case NodeKind::AssignStmt: return lowerAssign(static_cast<const AssignStmt &>(stmt));
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
    auto *stringTy = llvmType(BaseType::String);
    BaseType exprType = inferExprType(*stmt.init);
    if (exprType == BaseType::Unknown && init.value()->getType() == stringTy) {
      exprType = BaseType::String;
    }
    std::string structName {};
    if (exprType == BaseType::Unknown) {
      if (const auto *layout = findStructLayoutByType(init.value()->getType()); layout != nullptr) {
        exprType = BaseType::Struct;
        for (const auto &[name, candidate] : structLayouts) {
          if (candidate.llvmType == init.value()->getType()) {
            structName = name;
            break;
          }
        }
      }
    }
    auto *alloca = createAlloca(stmt.name, init.value()->getType());
    builder.CreateStore(init.value(), alloca);

    bool shouldRetain = false;
    if (init.value()->getType() == stringTy) {
      shouldRetain = true;
      retainStringValue(init.value());
    } else if (init.value()->getType()->isPointerTy() && !isTemporaryExpr(*stmt.init)) {
      shouldRetain = true;
      builder.CreateCall(retainFn, {init.value()});
    }
    if (shouldRetain) {
      locals.emplace(stmt.name, LocalValue {.slot = alloca, .type = exprType, .structName = structName, .ownedRef = true});
    } else {
      locals.emplace(stmt.name, LocalValue {.slot = alloca, .type = exprType, .structName = structName, .ownedRef = false});
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
    if (found->second.ownedRef) {
      auto *oldValue = builder.CreateLoad(found->second.slot->getAllocatedType(), found->second.slot);
      if (found->second.type == BaseType::String) {
        releaseStringValue(oldValue);
      } else {
        builder.CreateCall(releaseFn, {oldValue});
      }
    }
    builder.CreateStore(rhs.value(), found->second.slot);
    auto *stringTy = llvmType(BaseType::String);
    const bool isStringSlot = found->second.type == BaseType::String || found->second.slot->getAllocatedType() == stringTy;
    if (isStringSlot) {
      found->second.type = BaseType::String;
      found->second.structName.clear();
      found->second.ownedRef = true;
      retainStringValue(rhs.value());
      return {};
    }
    if (const auto *layout = findStructLayoutByType(found->second.slot->getAllocatedType()); layout != nullptr) {
      found->second.type = BaseType::Struct;
      for (const auto &[name, candidate] : structLayouts) {
        if (candidate.llvmType == layout->llvmType) {
          found->second.structName = name;
          break;
        }
      }
      found->second.ownedRef = false;
      return {};
    }
    if (rhs.value()->getType()->isPointerTy() && !isTemporaryExpr(*stmt.value)) {
      builder.CreateCall(retainFn, {rhs.value()});
      found->second.ownedRef = true;
      return {};
    }
    found->second.ownedRef = false;
    return {};
  }

  auto lowerReturn(const ReturnStmt &stmt) -> Result<void, Diagnostic> {
    if (!stmt.value) {
      while (!scopeLocals.empty()) {
        popScope();
      }
      builder.CreateRetVoid();
      return {};
    }

    auto ret = lowerExpr(*stmt.value);
    if (!ret) {
      return std::unexpected(ret.error());
    }
    if (ret.value()->getType() == llvmType(BaseType::String)) {
      retainStringValue(ret.value());
    }
    while (!scopeLocals.empty()) {
      popScope();
    }
    builder.CreateRet(ret.value());
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
      case NodeKind::IdentifierExpr: return lowerIdentifier(static_cast<const IdentifierExpr &>(expr));
      case NodeKind::BinaryExpr: return lowerBinary(static_cast<const BinaryExpr &>(expr));
      case NodeKind::CallExpr: return lowerCall(static_cast<const CallExpr &>(expr));
      case NodeKind::MemberExpr: return lowerMember(static_cast<const MemberExpr &>(expr));
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

  auto lowerMember(const MemberExpr &expr) -> Result<llvm::Value *, Diagnostic> {
    auto object = lowerExpr(*expr.object);
    if (!object) {
      return std::unexpected(object.error());
    }
    const auto *layout = findStructLayoutByType(object.value()->getType());
    if (layout == nullptr) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = "Member access requires a struct value.",
        .span = expr.span,
      });
    }
    auto fieldIt = layout->fieldIndices.find(expr.member);
    if (fieldIt == layout->fieldIndices.end()) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = std::format("Unknown field '{}'.", expr.member),
        .span = expr.span,
      });
    }
    auto *tmp = builder.CreateAlloca(layout->llvmType, nullptr, "member.base");
    builder.CreateStore(object.value(), tmp);
    auto *idx0 = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0);
    auto *idxN = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), static_cast<std::uint32_t>(fieldIt->second));
    auto *fieldPtr = builder.CreateInBoundsGEP(layout->llvmType, tmp, {idx0, idxN}, "member.ptr");
    auto *fieldTy = layout->llvmType->getElementType(static_cast<unsigned int>(fieldIt->second));
    return builder.CreateLoad(fieldTy, fieldPtr, "member.load");
  }

  auto lowerBinary(const BinaryExpr &expr) -> Result<llvm::Value *, Diagnostic> {
    auto lhs = lowerExpr(*expr.left);
    if (!lhs) {
      return std::unexpected(lhs.error());
    }
    auto rhs = lowerExpr(*expr.right);
    if (!rhs) {
      return std::unexpected(rhs.error());
    }

    auto *stringTy = llvmType(BaseType::String);
    if (expr.op == BinaryOp::Add && lhs.value()->getType() == stringTy && rhs.value()->getType() == stringTy) {
      auto *i32Ty = llvm::Type::getInt32Ty(context);
      auto *leftPtr = builder.CreateExtractValue(lhs.value(), {0}, "str.left.ptr");
      auto *leftLen = builder.CreateExtractValue(lhs.value(), {1}, "str.left.len");
      auto *rightPtr = builder.CreateExtractValue(rhs.value(), {0}, "str.right.ptr");
      auto *rightLen = builder.CreateExtractValue(rhs.value(), {1}, "str.right.len");
      auto *lenOut = builder.CreateAlloca(i32Ty, nullptr, "str.concat.len");
      auto *concatPtr = builder.CreateCall(concatFn, {leftPtr, leftLen, rightPtr, rightLen, lenOut}, "str.concat.ptr");
      auto *concatLen = builder.CreateLoad(i32Ty, lenOut, "str.concat.len.val");

      llvm::Value *stringValue = llvm::UndefValue::get(stringTy);
      stringValue = builder.CreateInsertValue(stringValue, concatPtr, {0}, "str.concat.v.ptr");
      stringValue = builder.CreateInsertValue(stringValue, concatLen, {1}, "str.concat.v.len");
      return stringValue;
    }

    switch (expr.op) {
      case BinaryOp::Add: return builder.CreateAdd(lhs.value(), rhs.value(), "addtmp");
      case BinaryOp::Sub: return builder.CreateSub(lhs.value(), rhs.value(), "subtmp");
      case BinaryOp::Mul: return builder.CreateMul(lhs.value(), rhs.value(), "multmp");
      case BinaryOp::Div: return builder.CreateSDiv(lhs.value(), rhs.value(), "divtmp");
      case BinaryOp::Eq: return builder.CreateICmpEQ(lhs.value(), rhs.value(), "eqtmp");
      case BinaryOp::Ne: return builder.CreateICmpNE(lhs.value(), rhs.value(), "netmp");
      case BinaryOp::Lt: return builder.CreateICmpSLT(lhs.value(), rhs.value(), "lttmp");
      case BinaryOp::Le: return builder.CreateICmpSLE(lhs.value(), rhs.value(), "letmp");
      case BinaryOp::Gt: return builder.CreateICmpSGT(lhs.value(), rhs.value(), "gttmp");
      case BinaryOp::Ge: return builder.CreateICmpSGE(lhs.value(), rhs.value(), "getmp");
    }
    return std::unexpected(Diagnostic {
      .code = ErrorCode::CodegenError,
      .message = "Unknown binary operator.",
      .span = expr.span,
    });
  }

  auto lowerCall(const CallExpr &expr) -> Result<llvm::Value *, Diagnostic> {
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

      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = "Builtin print supports only i32 or string.",
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
    for (const auto &arg : expr.args) {
      auto lowered = lowerExpr(*arg);
      if (!lowered) {
        return std::unexpected(lowered.error());
      }
      args.push_back(lowered.value());
    }
    return builder.CreateCall(callee, args, "calltmp");
  }

  auto inferExprType(const Expr &expr) -> BaseType {
    if (expr.inferredType) {
      return expr.inferredType->base;
    }
    if (expr.kind == NodeKind::LiteralExpr) {
      const auto &lit = static_cast<const LiteralExpr &>(expr);
      return lit.literalKind == LiteralExpr::Kind::String ? BaseType::String : BaseType::I32;
    }
    if (expr.kind == NodeKind::IdentifierExpr) {
      const auto &id = static_cast<const IdentifierExpr &>(expr);
      auto it = locals.find(id.name);
      if (it != locals.end()) {
        return it->second.type;
      }
      return BaseType::Unknown;
    }
    if (expr.kind == NodeKind::BinaryExpr) {
      const auto &bin = static_cast<const BinaryExpr &>(expr);
      const auto lhs = inferExprType(*bin.left);
      const auto rhs = inferExprType(*bin.right);
      switch (bin.op) {
        case BinaryOp::Add:
          if (lhs == BaseType::String && rhs == BaseType::String) {
            return BaseType::String;
          }
          return BaseType::I32;
        case BinaryOp::Sub:
        case BinaryOp::Mul:
        case BinaryOp::Div:
          return BaseType::I32;
        case BinaryOp::Eq:
        case BinaryOp::Ne:
        case BinaryOp::Lt:
        case BinaryOp::Le:
        case BinaryOp::Gt:
        case BinaryOp::Ge:
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
    case BaseType::Bool: return llvm::Type::getInt1Ty(context);
    case BaseType::String: return getStringStructType(context);
    case BaseType::Struct: break;
    case BaseType::Unknown: break;
  }
  return llvm::Type::getInt32Ty(context);
}

} // namespace

IRGenerator::IRGenerator(llvm::LLVMContext &context_) : context(context_) {}

auto IRGenerator::lower(const TypedModule &typed, const std::string &moduleName)
  -> Result<std::unique_ptr<llvm::Module>, Diagnostic> {
  auto module = std::make_unique<llvm::Module>(moduleName, context);
  bool hasExplicitMain = false;
  std::unordered_map<std::string, StructLayout> structLayouts {};

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
    if (decl->name == "main") {
      hasExplicitMain = true;
    }
    std::vector<llvm::Type *> params {};
    params.reserve(decl->params.size());
    for (const auto &param : decl->params) {
      params.push_back(mapDeclaredType(context, param.type, structLayouts));
    }

    auto *retTy = mapDeclaredType(context, decl->returnType, structLayouts);
    auto *fnType = llvm::FunctionType::get(retTy, params, false);
    auto *fn = llvm::Function::Create(fnType, llvm::Function::ExternalLinkage, decl->name, *module);

    std::size_t idx = 0;
    for (auto &arg : fn->args()) {
      arg.setName(decl->params[idx++].name);
    }
  }

  if (!typed.module->topLevelStatements.empty() && hasExplicitMain) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::CodegenError,
      .message = "Cannot mix top-level executable statements with explicit 'func main()'.",
      .span = typed.module->span,
    });
  }

  if (!typed.module->topLevelStatements.empty()) {
    auto *fnType = llvm::FunctionType::get(llvm::Type::getInt32Ty(context), {}, false);
    llvm::Function::Create(fnType, llvm::Function::ExternalLinkage, "main", *module);
  }

  for (const auto &decl : typed.module->functions) {
    auto *fn = module->getFunction(decl->name);
    FunctionLowering lowering {context, *module, *fn, structLayouts};
    auto lowered = lowering.lower(*decl);
    if (!lowered) {
      return std::unexpected(lowered.error());
    }
  }

  if (!typed.module->topLevelStatements.empty()) {
    auto *mainFn = module->getFunction("main");
    if (mainFn == nullptr) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = "Failed to create implicit main function.",
        .span = typed.module->span,
      });
    }
    FunctionLowering lowering {context, *module, *mainFn, structLayouts};
    auto lowered = lowering.lowerStatements(typed.module->topLevelStatements, BaseType::I32);
    if (!lowered) {
      return std::unexpected(lowered.error());
    }
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
  auto fileType = llvm::CodeGenFileType::ObjectFile;
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
