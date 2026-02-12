#include "thagore/frontend/semantic.hpp"

#include <format>
#include <optional>
#include <unordered_map>
#include <vector>

namespace thagore {
namespace {

auto baseTypeName(BaseType type) -> const char * {
  switch (type) {
    case BaseType::Unknown: return "unknown";
    case BaseType::Void: return "void";
    case BaseType::I32: return "i32";
    case BaseType::Bool: return "bool";
    case BaseType::String: return "String";
    case BaseType::Struct: return "struct";
  }
  return "unknown";
}

auto sameType(const TypePtr &lhs, const TypePtr &rhs) -> bool {
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->base != rhs->base) {
    return false;
  }
  if (lhs->base == BaseType::Struct) {
    return lhs->name == rhs->name;
  }
  return true;
}

struct Scope {
  std::unordered_map<std::string, TypePtr> symbols {};
};

class SemanticPass : public ExprVisitor<TypePtr>, public StmtVisitor<void> {
public:
  explicit SemanticPass(TypedModule &typed_) : typed(typed_) {}

  auto run() -> Result<void, Diagnostic> {
    for (const auto &st : typed.module->structs) {
      if (typed.structTypes.contains(st->name)) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::SemanticError,
          .message = std::format("Struct '{}' already declared.", st->name),
          .span = st->span,
        });
      }
      typed.structTypes[st->name] = TypedModule::StructType {.fields = st->fields};
    }
    for (const auto &st : typed.module->structs) {
      for (const auto &field : st->fields) {
        if (field.type->base == BaseType::Struct && !typed.structTypes.contains(field.type->name)) {
          return std::unexpected(Diagnostic {
            .code = ErrorCode::SemanticError,
            .message = std::format("Unknown struct type '{}' in field '{}.{}'.", field.type->name, st->name, field.name),
            .span = field.span,
          });
        }
      }
    }

    bool hasExplicitMain = false;
    for (const auto &fn : typed.module->functions) {
      if (fn->name == "main") {
        hasExplicitMain = true;
        break;
      }
    }

    if (!typed.module->topLevelStatements.empty() && hasExplicitMain) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = "Cannot mix top-level executable statements with explicit 'func main()'.",
        .span = typed.module->span,
      });
    }

    scopes.clear();
    pushScope();
    inTopLevelContext = true;
    for (const auto &stmt : typed.module->topLevelStatements) {
      auto v = stmt->accept(*this);
      if (!v) {
        popScope();
        return std::unexpected(v.error());
      }
    }
    popScope();

    if (typed.module->topLevelStatements.empty() && !hasExplicitMain) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = "No entry point found. Define 'func main()' or add top-level statements.",
        .span = typed.module->span,
      });
    }

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
  bool inTopLevelContext {false};

  auto analyzeFunction(FunctionDecl &fn) -> Result<void, Diagnostic> {
    scopes.clear();
    pushScope();
    inTopLevelContext = false;
    currentFunctionReturnType = fn.returnType ? fn.returnType : makeType(BaseType::Void);
    for (const auto &param : fn.params) {
      if (param.type->base == BaseType::Struct && !typed.structTypes.contains(param.type->name)) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::SemanticError,
          .message = std::format("Unknown struct type '{}'.", param.type->name),
          .span = param.span,
        });
      }
    }
    if (fn.returnType && fn.returnType->base == BaseType::Struct && !typed.structTypes.contains(fn.returnType->name)) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = std::format("Unknown struct type '{}'.", fn.returnType->name),
        .span = fn.span,
      });
    }

    for (const auto &param : fn.params) {
      scopes.back().symbols[param.name] = param.type;
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

    const BaseType declared = fn.returnType ? fn.returnType->base : BaseType::Void;
    if (!sameType(fn.returnType ? fn.returnType : makeType(BaseType::Void), inferredReturn)) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = std::format(
          "Function '{}' returns incompatible type (declared {}{}, actual {}{}).",
          fn.name,
          baseTypeName(declared),
          declared == BaseType::Struct ? std::format(" {}", fn.returnType->name) : "",
          baseTypeName(inferredReturn->base)
          ,
          inferredReturn->base == BaseType::Struct ? std::format(" {}", inferredReturn->name) : ""
        ),
        .span = fn.span,
      });
    }

    typed.functionTypes[fn.name].returnType = fn.returnType;
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
        if (leftBase == BaseType::String && rightBase == BaseType::String) {
          return makeType(BaseType::String);
        }
        [[fallthrough]];
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
        if (leftBase == BaseType::Struct) {
          return std::unexpected(Diagnostic {
            .code = ErrorCode::SemanticError,
            .message = "Struct comparison is not supported yet.",
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
      if (argType.value()->base != BaseType::I32 && argType.value()->base != BaseType::String) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::SemanticError,
          .message = "Builtin print supports only i32 or string.",
          .span = expr.args[0]->span,
        });
      }
      return makeType(BaseType::Void);
    }

    if (auto st = typed.structTypes.find(expr.callee); st != typed.structTypes.end()) {
      if (expr.args.size() != st->second.fields.size()) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::SemanticError,
          .message = std::format("Struct '{}' constructor expects {} argument(s), got {}.", expr.callee, st->second.fields.size(), expr.args.size()),
          .span = expr.span,
        });
      }
      for (std::size_t i = 0; i < expr.args.size(); ++i) {
        auto t = expr.args[i]->accept(*this);
        if (!t) {
          return std::unexpected(t.error());
        }
        if (!sameType(t.value(), st->second.fields[i].type)) {
          return std::unexpected(Diagnostic {
            .code = ErrorCode::SemanticError,
            .message = std::format("Constructor argument type mismatch at field '{}'.", st->second.fields[i].name),
            .span = expr.args[i]->span,
          });
        }
      }
      return makeStructType(expr.callee);
    }

    auto found = typed.functionTypes.find(expr.callee);
    if (found == typed.functionTypes.end()) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = std::format("Unknown function '{}'.", expr.callee),
        .span = expr.span,
      });
    }
    if (expr.args.size() != found->second.params.size()) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = std::format("Function '{}' expects {} argument(s), got {}.", expr.callee, found->second.params.size(), expr.args.size()),
        .span = expr.span,
      });
    }
    for (std::size_t i = 0; i < expr.args.size(); ++i) {
      auto t = expr.args[i]->accept(*this);
      if (!t) {
        return std::unexpected(t.error());
      }
      if (!sameType(t.value(), found->second.params[i])) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::SemanticError,
          .message = std::format("Argument type mismatch at position {} in call to '{}'.", i + 1, expr.callee),
          .span = expr.args[i]->span,
        });
      }
    }
    return found->second.returnType;
  }

  auto visit(const MemberExpr &expr) -> Result<TypePtr, Diagnostic> override {
    auto objectType = expr.object->accept(*this);
    if (!objectType) {
      return std::unexpected(objectType.error());
    }
    if (objectType.value()->base != BaseType::Struct) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = "Member access is only valid on struct values.",
        .span = expr.span,
      });
    }
    auto st = typed.structTypes.find(objectType.value()->name);
    if (st == typed.structTypes.end()) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = std::format("Unknown struct type '{}'.", objectType.value()->name),
        .span = expr.span,
      });
    }
    for (const auto &field : st->second.fields) {
      if (field.name == expr.member) {
        return field.type;
      }
    }
    return std::unexpected(Diagnostic {
      .code = ErrorCode::SemanticError,
      .message = std::format("Struct '{}' has no field '{}'.", objectType.value()->name, expr.member),
      .span = expr.span,
    });
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
    if (!sameType(lhs.value(), rhs.value())) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = std::format("Type mismatch in assignment to '{}'.", stmt.name),
        .span = stmt.span,
      });
    }
    return {};
  }

  auto visit(const ReturnStmt &stmt) -> Result<void, Diagnostic> override {
    if (inTopLevelContext) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = "Return is not allowed at top-level scope.",
        .span = stmt.span,
      });
    }
    TypePtr actual = makeType(BaseType::Void);
    if (stmt.value) {
      auto t = stmt.value->accept(*this);
      if (!t) {
        return std::unexpected(t.error());
      }
      actual = t.value();
    }
    if (sameType(currentFunctionReturnType, actual)) {
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
    auto thenResult = stmt.thenBlock->accept(*this);
    if (!thenResult) {
      return std::unexpected(thenResult.error());
    }
    if (stmt.elseBlock) {
      auto elseResult = stmt.elseBlock->accept(*this);
      if (!elseResult) {
        return std::unexpected(elseResult.error());
      }
    }
    return {};
  }

  auto visit(const LoopStmt &stmt) -> Result<void, Diagnostic> override {
    if (stmt.condition) {
      auto cond = stmt.condition->accept(*this);
      if (!cond) {
        return std::unexpected(cond.error());
      }
      if (cond.value()->base != BaseType::Bool) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::SemanticError,
          .message = "While condition must be bool.",
          .span = stmt.condition->span,
        });
      }
    }
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
    TypedModule::FunctionSignature sig {};
    sig.returnType = fn->returnType ? fn->returnType : makeType(BaseType::Void);
    sig.params.reserve(fn->params.size());
    for (const auto &param : fn->params) {
      sig.params.push_back(param.type);
    }
    typed.functionTypes[fn->name] = std::move(sig);
  }

  SemanticPass pass {typed};
  auto result = pass.run();
  if (!result) {
    return std::unexpected(result.error());
  }
  return typed;
}

} // namespace thagore
