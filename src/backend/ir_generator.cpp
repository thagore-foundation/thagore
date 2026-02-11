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
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace thagore {
namespace {

struct LocalValue {
  llvm::AllocaInst *slot {nullptr};
  BaseType type {BaseType::Unknown};
  bool ownedRef {false};
};

class FunctionLowering {
public:
  FunctionLowering(llvm::LLVMContext &ctx, llvm::Module &mod, llvm::Function &fn)
    : context(ctx), module(mod), function(fn), builder(ctx) {
    declareRuntimeHooks();
  }

  auto lower(const FunctionDecl &decl) -> Result<void, Diagnostic> {
    const BaseType returnType = decl.returnType ? decl.returnType->base : BaseType::Void;
    return lowerStatements(decl.body->statements, returnType);
  }

  auto lowerStatements(const std::vector<std::unique_ptr<Stmt>> &statements, BaseType returnType) -> Result<void, Diagnostic> {
    auto *entry = llvm::BasicBlock::Create(context, "entry", &function);
    builder.SetInsertPoint(entry);
    entryBuilder.SetInsertPoint(entry);

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
      if (returnType == BaseType::Void || function.getReturnType()->isVoidTy()) {
        releaseOwnedLocals();
        builder.CreateRetVoid();
      } else if (returnType == BaseType::I32 || function.getReturnType()->isIntegerTy(32)) {
        releaseOwnedLocals();
        builder.CreateRet(llvm::ConstantInt::get(function.getReturnType(), 0));
      } else {
        releaseOwnedLocals();
        builder.CreateRet(llvm::UndefValue::get(function.getReturnType()));
      }
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
  llvm::FunctionCallee retainFn {};
  llvm::FunctionCallee releaseFn {};

  auto terminated() const -> bool {
    auto *block = builder.GetInsertBlock();
    return block && block->getTerminator() != nullptr;
  }

  void declareRuntimeHooks() {
    auto *voidTy = llvm::Type::getVoidTy(context);
    auto *ptrTy = llvm::PointerType::get(context, 0);
    retainFn = module.getOrInsertFunction("__thg_retain", llvm::FunctionType::get(voidTy, {ptrTy}, false));
    releaseFn = module.getOrInsertFunction("__thg_release", llvm::FunctionType::get(voidTy, {ptrTy}, false));
  }

  auto llvmType(BaseType type) -> llvm::Type * {
    switch (type) {
      case BaseType::Void: return llvm::Type::getVoidTy(context);
      case BaseType::I32: return llvm::Type::getInt32Ty(context);
      case BaseType::Bool: return llvm::Type::getInt1Ty(context);
      case BaseType::String: return llvm::PointerType::get(context, 0);
      case BaseType::Unknown: break;
    }
    return llvm::Type::getInt32Ty(context);
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
    for (const auto &stmt : block.statements) {
      auto lowered = lowerStmt(*stmt);
      if (!lowered) {
        return std::unexpected(lowered.error());
      }
      if (terminated()) {
        break;
      }
    }
    return {};
  }

  auto lowerLet(const LetStmt &stmt) -> Result<void, Diagnostic> {
    auto init = lowerExpr(*stmt.init);
    if (!init) {
      return std::unexpected(init.error());
    }
    auto *alloca = createAlloca(stmt.name, init.value()->getType());
    builder.CreateStore(init.value(), alloca);

    const bool isRef = init.value()->getType()->isPointerTy();
    const bool shouldRetain = isRef && !isTemporaryExpr(*stmt.init);
    if (shouldRetain) {
      builder.CreateCall(retainFn, {init.value()});
    }
    locals.emplace(stmt.name, LocalValue {.slot = alloca, .type = inferExprType(*stmt.init), .ownedRef = shouldRetain});
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
      builder.CreateCall(releaseFn, {oldValue});
    }
    builder.CreateStore(rhs.value(), found->second.slot);
    if (rhs.value()->getType()->isPointerTy() && !isTemporaryExpr(*stmt.value)) {
      builder.CreateCall(retainFn, {rhs.value()});
      found->second.ownedRef = true;
    } else {
      found->second.ownedRef = false;
    }
    return {};
  }

  auto lowerReturn(const ReturnStmt &stmt) -> Result<void, Diagnostic> {
    if (!stmt.value) {
      releaseOwnedLocals();
      builder.CreateRetVoid();
      return {};
    }

    auto ret = lowerExpr(*stmt.value);
    if (!ret) {
      return std::unexpected(ret.error());
    }
    releaseOwnedLocals();
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
    auto *mergeBB = llvm::BasicBlock::Create(context, "if.end", &function);
    builder.CreateCondBr(boolCond.value(), thenBB, mergeBB);

    builder.SetInsertPoint(thenBB);
    auto thenResult = lowerBlock(*stmt.thenBlock);
    if (!thenResult) {
      return std::unexpected(thenResult.error());
    }
    if (!terminated()) {
      builder.CreateBr(mergeBB);
    }

    builder.SetInsertPoint(mergeBB);
    return {};
  }

  auto lowerLoop(const LoopStmt &stmt) -> Result<void, Diagnostic> {
    auto *condBB = llvm::BasicBlock::Create(context, "loop.cond", &function);
    auto *bodyBB = llvm::BasicBlock::Create(context, "loop.body", &function);
    auto *exitBB = llvm::BasicBlock::Create(context, "loop.end", &function);

    builder.CreateBr(condBB);
    builder.SetInsertPoint(condBB);
    auto *always = llvm::ConstantInt::getTrue(context);
    builder.CreateCondBr(always, bodyBB, exitBB);

    builder.SetInsertPoint(bodyBB);
    auto bodyResult = lowerBlock(*stmt.body);
    if (!bodyResult) {
      return std::unexpected(bodyResult.error());
    }
    if (!terminated()) {
      builder.CreateBr(condBB);
    }

    builder.SetInsertPoint(exitBB);
    return {};
  }

  auto lowerExpr(const Expr &expr) -> Result<llvm::Value *, Diagnostic> {
    switch (expr.kind) {
      case NodeKind::LiteralExpr: return lowerLiteral(static_cast<const LiteralExpr &>(expr));
      case NodeKind::IdentifierExpr: return lowerIdentifier(static_cast<const IdentifierExpr &>(expr));
      case NodeKind::BinaryExpr: return lowerBinary(static_cast<const BinaryExpr &>(expr));
      case NodeKind::CallExpr: return lowerCall(static_cast<const CallExpr &>(expr));
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

    auto *value = builder.CreateGlobalStringPtr(expr.value, "str");
    return value;
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

  auto lowerBinary(const BinaryExpr &expr) -> Result<llvm::Value *, Diagnostic> {
    auto lhs = lowerExpr(*expr.left);
    if (!lhs) {
      return std::unexpected(lhs.error());
    }
    auto rhs = lowerExpr(*expr.right);
    if (!rhs) {
      return std::unexpected(rhs.error());
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
      if (!arg.value()->getType()->isIntegerTy(32)) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::CodegenError,
          .message = "Builtin print currently supports only i32.",
          .span = expr.args[0]->span,
        });
      }

      auto *voidTy = llvm::Type::getVoidTy(context);
      auto *i32Ty = llvm::Type::getInt32Ty(context);
      auto printFn = module.getOrInsertFunction("__thg_print_i32", llvm::FunctionType::get(voidTy, {i32Ty}, false));
      builder.CreateCall(printFn, {arg.value()});
      return llvm::ConstantInt::get(i32Ty, 0);
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

  void releaseOwnedLocals() {
    for (auto &[_, local] : locals) {
      if (!local.ownedRef) {
        continue;
      }
      auto *value = builder.CreateLoad(local.slot->getAllocatedType(), local.slot);
      builder.CreateCall(releaseFn, {value});
    }
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
      return BaseType::I32;
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
    case BaseType::String: return llvm::PointerType::get(context, 0);
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

  for (const auto &decl : typed.module->functions) {
    if (decl->name == "main") {
      hasExplicitMain = true;
    }
    std::vector<llvm::Type *> params {};
    params.reserve(decl->params.size());
    for (std::size_t i = 0; i < decl->params.size(); ++i) {
      params.push_back(llvm::Type::getInt32Ty(context));
    }

    auto *retTy = mapType(context, decl->returnType ? decl->returnType->base : BaseType::Void);
    auto *fnType = llvm::FunctionType::get(retTy, params, false);
    auto *fn = llvm::Function::Create(fnType, llvm::Function::ExternalLinkage, decl->name, *module);

    std::size_t idx = 0;
    for (auto &arg : fn->args()) {
      arg.setName(decl->params[idx++]);
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
    FunctionLowering lowering {context, *module, *fn};
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
    FunctionLowering lowering {context, *module, *mainFn};
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
