#include "thagore/frontend/semantic.hpp"

#include <format>
#include <optional>
#include <unordered_map>
#include <vector>

namespace thagore {
namespace {

struct Scope {
  std::unordered_map<std::string, TypePtr> symbols {};
};

class SemanticPass : public ExprVisitor<TypePtr>, public StmtVisitor<void> {
public:
  explicit SemanticPass(TypedModule &typed_) : typed(typed_) {}

  auto run() -> Result<void, Diagnostic> {
    for (auto &fn : typed.module->functions) {
      auto ret = analyzeFunction(*fn);
      if (!ret) {
        return std::unexpected(ret.error());
      }
    }
    return {};
  }

private:
  TypedModule &typed;
  std::vector<Scope> scopes {};
  TypePtr currentFunctionReturnType {makeType(BaseType::Void)};

  auto analyzeFunction(FunctionDecl &fn) -> Result<void, Diagnostic> {
    scopes.clear();
    pushScope();
    for (const auto &param : fn.params) {
      scopes.back().symbols[param] = makeType(BaseType::I32);
    }

    bool sawReturn = false;
    TypePtr inferredReturn = makeType(BaseType::Void);
    for (const auto &stmt : fn.body->statements) {
      if (stmt->kind == NodeKind::ReturnStmt) {
        sawReturn = true;
        const auto *ret = static_cast<const ReturnStmt *>(stmt.get());
        if (ret->value) {
          auto retType = ret->value->accept(*this);
          if (!retType) {
            return std::unexpected(retType.error());
          }
          inferredReturn = retType.value();
          currentFunctionReturnType = inferredReturn;
        }
      }
      auto s = stmt->accept(*this);
      if (!s) {
        return std::unexpected(s.error());
      }
    }

    if (!sawReturn) {
      inferredReturn = makeType(BaseType::Void);
    }

    fn.returnType = inferredReturn;
    typed.functionTypes[fn.name] = inferredReturn;
    popScope();
    return {};
  }

  auto pushScope() -> void {
    scopes.push_back(Scope {});
  }

  auto popScope() -> void {
    if (!scopes.empty()) {
      scopes.pop_back();
    }
  }

  auto declareSymbol(const std::string &name, TypePtr type, const SourceSpan &span) -> Result<void, Diagnostic> {
    if (scopes.empty()) {
      pushScope();
    }
    auto &[symbols] = scopes.back();
    if (symbols.contains(name)) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = std::format("Variable '{}' already declared in this scope.", name),
        .span = span,
      });
    }
    symbols[name] = std::move(type);
    return {};
  }

  auto lookupSymbol(const std::string &name, const SourceSpan &span) -> Result<TypePtr, Diagnostic> {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
      if (auto found = it->symbols.find(name); found != it->symbols.end()) {
        return found->second;
      }
    }
    return std::unexpected(Diagnostic {
      .code = ErrorCode::SemanticError,
      .message = std::format("Unknown variable '{}'.", name),
      .span = span,
    });
  }

public:
  auto visit(const BinaryExpr &expr) -> Result<TypePtr, Diagnostic> override {
    auto left = expr.left->accept(*this);
    if (!left) {
      return std::unexpected(left.error());
    }
    auto right = expr.right->accept(*this);
    if (!right) {
      return std::unexpected(right.error());
    }

    auto leftBase = left.value()->base;
    auto rightBase = right.value()->base;

    switch (expr.op) {
      case BinaryOp::Add:
      case BinaryOp::Sub:
      case BinaryOp::Mul:
      case BinaryOp::Div:
        if (leftBase != BaseType::I32 || rightBase != BaseType::I32) {
          return std::unexpected(Diagnostic {
            .code = ErrorCode::SemanticError,
            .message = "Arithmetic operators require i32 operands.",
            .span = expr.span,
          });
        }
        return makeType(BaseType::I32);
      case BinaryOp::Eq:
      case BinaryOp::Ne:
      case BinaryOp::Lt:
      case BinaryOp::Le:
      case BinaryOp::Gt:
      case BinaryOp::Ge:
        if (leftBase != rightBase) {
          return std::unexpected(Diagnostic {
            .code = ErrorCode::SemanticError,
            .message = "Comparison operands must share the same type.",
            .span = expr.span,
          });
        }
        return makeType(BaseType::Bool);
    }
    return makeType(BaseType::Unknown);
  }

  auto visit(const LiteralExpr &expr) -> Result<TypePtr, Diagnostic> override {
    if (expr.literalKind == LiteralExpr::Kind::Int) {
      return makeType(BaseType::I32);
    }
    return makeType(BaseType::String);
  }

  auto visit(const IdentifierExpr &expr) -> Result<TypePtr, Diagnostic> override {
    return lookupSymbol(expr.name, expr.span);
  }

  auto visit(const CallExpr &expr) -> Result<TypePtr, Diagnostic> override {
    if (expr.callee == "print") {
      if (expr.args.size() != 1) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::SemanticError,
          .message = "Builtin print expects exactly one argument.",
          .span = expr.span,
        });
      }
      auto argType = expr.args[0]->accept(*this);
      if (!argType) {
        return std::unexpected(argType.error());
      }
      if (argType.value()->base != BaseType::I32) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::SemanticError,
          .message = "Builtin print currently supports only i32.",
          .span = expr.args[0]->span,
        });
      }
      return makeType(BaseType::Void);
    }

    auto found = typed.functionTypes.find(expr.callee);
    if (found == typed.functionTypes.end()) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = std::format("Unknown function '{}'.", expr.callee),
        .span = expr.span,
      });
    }
    for (const auto &arg : expr.args) {
      auto t = arg->accept(*this);
      if (!t) {
        return std::unexpected(t.error());
      }
    }
    return found->second;
  }

  auto visit(const BlockStmt &stmt) -> Result<void, Diagnostic> override {
    pushScope();
    for (const auto &nested : stmt.statements) {
      auto v = nested->accept(*this);
      if (!v) {
        popScope();
        return std::unexpected(v.error());
      }
    }
    popScope();
    return {};
  }

  auto visit(const LetStmt &stmt) -> Result<void, Diagnostic> override {
    auto t = stmt.init->accept(*this);
    if (!t) {
      return std::unexpected(t.error());
    }
    return declareSymbol(stmt.name, t.value(), stmt.span);
  }

  auto visit(const AssignStmt &stmt) -> Result<void, Diagnostic> override {
    auto lhs = lookupSymbol(stmt.name, stmt.span);
    if (!lhs) {
      return std::unexpected(lhs.error());
    }
    auto rhs = stmt.value->accept(*this);
    if (!rhs) {
      return std::unexpected(rhs.error());
    }
    if (lhs.value()->base != rhs.value()->base) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = std::format("Type mismatch in assignment to '{}'.", stmt.name),
        .span = stmt.span,
      });
    }
    return {};
  }

  auto visit(const ReturnStmt &stmt) -> Result<void, Diagnostic> override {
    TypePtr actual = makeType(BaseType::Void);
    if (stmt.value) {
      auto t = stmt.value->accept(*this);
      if (!t) {
        return std::unexpected(t.error());
      }
      actual = t.value();
    }
    if (currentFunctionReturnType->base == BaseType::Unknown) {
      currentFunctionReturnType = actual;
      return {};
    }
    if (currentFunctionReturnType->base == BaseType::Void || currentFunctionReturnType->base == actual->base) {
      currentFunctionReturnType = actual;
      return {};
    }
    return std::unexpected(Diagnostic {
      .code = ErrorCode::SemanticError,
      .message = "Inconsistent return type in function.",
      .span = stmt.span,
    });
  }

  auto visit(const IfStmt &stmt) -> Result<void, Diagnostic> override {
    auto cond = stmt.condition->accept(*this);
    if (!cond) {
      return std::unexpected(cond.error());
    }
    if (cond.value()->base != BaseType::Bool) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = "If condition must be bool.",
        .span = stmt.condition->span,
      });
    }
    return stmt.thenBlock->accept(*this);
  }

  auto visit(const LoopStmt &stmt) -> Result<void, Diagnostic> override {
    return stmt.body->accept(*this);
  }

  auto visit(const ExprStmt &stmt) -> Result<void, Diagnostic> override {
    auto t = stmt.expr->accept(*this);
    if (!t) {
      return std::unexpected(t.error());
    }
    return {};
  }
};

} // namespace

auto SemanticAnalyzer::analyze(std::unique_ptr<ModuleDecl> module) -> Result<TypedModule, Diagnostic> {
  TypedModule typed {
    .module = std::move(module),
    .functionTypes = {},
  };

  for (const auto &fn : typed.module->functions) {
    typed.functionTypes[fn->name] = makeType(BaseType::Unknown);
  }

  SemanticPass pass {typed};
  auto result = pass.run();
  if (!result) {
    return std::unexpected(result.error());
  }
  return typed;
}

} // namespace thagore
