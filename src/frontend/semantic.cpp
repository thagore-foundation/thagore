#include "thagore/frontend/semantic.hpp"

#include <format>
#include <optional>
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

auto baseTypeName(BaseType type) -> const char * {
  switch (type) {
    case BaseType::Unknown: return "unknown";
    case BaseType::Void: return "void";
    case BaseType::I32: return "i32";
    case BaseType::F32: return "f32";
    case BaseType::Bool: return "bool";
    case BaseType::String: return "String";
    case BaseType::Struct: return "struct";
    case BaseType::Array: return "array";
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
  if (lhs->base == BaseType::Array) {
    if (lhs->arraySize != rhs->arraySize) {
      return false;
    }
    return sameType(lhs->elementType, rhs->elementType);
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
        auto known = ensureKnownType(field.type, field.span);
        if (!known) {
          return std::unexpected(known.error());
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

  auto ensureKnownType(const TypePtr &type, const SourceSpan &span) -> Result<void, Diagnostic> {
    if (!type) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = "Unknown type.",
        .span = span,
      });
    }
    if (type->base == BaseType::Struct) {
      if (!typed.structTypes.contains(type->name)) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::SemanticError,
          .message = std::format("Unknown struct type '{}'.", type->name),
          .span = span,
        });
      }
      return {};
    }
    if (type->base == BaseType::Array) {
      if (type->arraySize == 0) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::SemanticError,
          .message = "Array size must be greater than zero.",
          .span = span,
        });
      }
      return ensureKnownType(type->elementType, span);
    }
    return {};
  }

  auto containsReturn(const Stmt &stmt) const -> bool {
    if (stmt.kind == NodeKind::ReturnStmt) {
      return true;
    }
    if (stmt.kind == NodeKind::BlockStmt) {
      const auto &block = static_cast<const BlockStmt &>(stmt);
      for (const auto &nested : block.statements) {
        if (containsReturn(*nested)) {
          return true;
        }
      }
      return false;
    }
    if (stmt.kind == NodeKind::IfStmt) {
      const auto &ifStmt = static_cast<const IfStmt &>(stmt);
      if (containsReturn(*ifStmt.thenBlock)) {
        return true;
      }
      if (ifStmt.elseBlock && containsReturn(*ifStmt.elseBlock)) {
        return true;
      }
      return false;
    }
    if (stmt.kind == NodeKind::LoopStmt) {
      const auto &loop = static_cast<const LoopStmt &>(stmt);
      return containsReturn(*loop.body);
    }
    return false;
  }

  auto blockContainsReturn(const BlockStmt &block) const -> bool {
    for (const auto &stmt : block.statements) {
      if (containsReturn(*stmt)) {
        return true;
      }
    }
    return false;
  }

  auto analyzeFunction(FunctionDecl &fn) -> Result<void, Diagnostic> {
    if (fn.isExtern) {
      if (!fn.methodOwner.empty()) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::SemanticError,
          .message = "Extern function cannot be declared inside impl block.",
          .span = fn.span,
        });
      }
      for (const auto &param : fn.params) {
        auto known = ensureKnownType(param.type, param.span);
        if (!known) {
          return std::unexpected(known.error());
        }
      }
      if (fn.returnType) {
        auto known = ensureKnownType(fn.returnType, fn.span);
        if (!known) {
          return std::unexpected(known.error());
        }
      }
      typed.functionTypes[fn.name].returnType = fn.returnType;
      return {};
    }

    scopes.clear();
    pushScope();
    inTopLevelContext = false;
    currentFunctionReturnType = fn.returnType ? fn.returnType : makeType(BaseType::Void);
    for (const auto &param : fn.params) {
      auto known = ensureKnownType(param.type, param.span);
      if (!known) {
        return std::unexpected(known.error());
      }
    }
    if (fn.returnType) {
      auto known = ensureKnownType(fn.returnType, fn.span);
      if (!known) {
        return std::unexpected(known.error());
      }
    }

    for (const auto &param : fn.params) {
      scopes.back().symbols[param.name] = param.type;
    }

    for (const auto &stmt : fn.body->statements) {
      auto s = stmt->accept(*this);
      if (!s) {
        return std::unexpected(s.error());
      }
    }

    if (fn.returnType && fn.returnType->base != BaseType::Void && !blockContainsReturn(*fn.body)) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = std::format("Function '{}' is missing a return value.", fn.name),
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

    const auto overloadMethod = overloadedMethodFor(expr.op);
    if (leftBase == BaseType::Struct) {
      if (rightBase != BaseType::Struct || left.value()->name != right.value()->name) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::SemanticError,
          .message = "Operator overloading requires both operands to be the same struct type.",
          .span = expr.span,
        });
      }
      if (overloadMethod.empty()) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::SemanticError,
          .message = "Operator not supported for struct.",
          .span = expr.span,
        });
      }
      const auto mangled = std::format("{}_{}", left.value()->name, overloadMethod);
      auto found = typed.functionTypes.find(mangled);
      if (found == typed.functionTypes.end()) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::SemanticError,
          .message = std::format("Operator not supported: '{}'. Missing method '{}.{}'.", overloadMethod, left.value()->name, overloadMethod),
          .span = expr.span,
        });
      }
      if (found->second.params.size() != 2) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::SemanticError,
          .message = std::format("Method '{}.{}' must have signature (self, other).", left.value()->name, overloadMethod),
          .span = expr.span,
        });
      }
      if (!sameType(found->second.params[0], left.value()) || !sameType(found->second.params[1], right.value())) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::SemanticError,
          .message = std::format("Method '{}.{}' has incompatible operand types.", left.value()->name, overloadMethod),
          .span = expr.span,
        });
      }
      if (expr.op == BinaryOp::Eq && found->second.returnType->base != BaseType::Bool) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::SemanticError,
          .message = "Overloaded '__eq__' must return bool.",
          .span = expr.span,
        });
      }
      return found->second.returnType;
    }

    switch (expr.op) {
      case BinaryOp::Add:
        if (leftBase == BaseType::String && rightBase == BaseType::String) {
          return makeType(BaseType::String);
        }
        [[fallthrough]];
      case BinaryOp::Sub:
      case BinaryOp::Mul:
      case BinaryOp::Div:
        if (leftBase == BaseType::I32 && rightBase == BaseType::I32) {
          return makeType(BaseType::I32);
        }
        if (leftBase == BaseType::F32 && rightBase == BaseType::F32) {
          return makeType(BaseType::F32);
        }
        if ((leftBase == BaseType::I32 && rightBase == BaseType::F32) || (leftBase == BaseType::F32 && rightBase == BaseType::I32)) {
          return std::unexpected(Diagnostic {
            .code = ErrorCode::SemanticError,
            .message = "Cannot mix i32 and f32 without explicit cast.",
            .span = expr.span,
          });
        }
        return std::unexpected(Diagnostic {
          .code = ErrorCode::SemanticError,
          .message = "Arithmetic operators require matching numeric operands.",
          .span = expr.span,
        });
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
    if (expr.literalKind == LiteralExpr::Kind::Float) {
      return makeType(BaseType::F32);
    }
    return makeType(BaseType::String);
  }

  auto visit(const ArrayLiteralExpr &expr) -> Result<TypePtr, Diagnostic> override {
    if (expr.elements.empty()) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = "Array literal cannot be empty.",
        .span = expr.span,
      });
    }
    auto firstType = expr.elements.front()->accept(*this);
    if (!firstType) {
      return std::unexpected(firstType.error());
    }
    for (std::size_t i = 1; i < expr.elements.size(); ++i) {
      auto t = expr.elements[i]->accept(*this);
      if (!t) {
        return std::unexpected(t.error());
      }
      if (!sameType(firstType.value(), t.value())) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::SemanticError,
          .message = "All elements in array literal must have the same type.",
          .span = expr.elements[i]->span,
        });
      }
    }
    return makeArrayType(firstType.value(), expr.elements.size());
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
      if (
        argType.value()->base != BaseType::I32 &&
        argType.value()->base != BaseType::F32 &&
        argType.value()->base != BaseType::String
      ) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::SemanticError,
          .message = "Builtin print supports only i32, f32 or string.",
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

  auto visit(const MethodCallExpr &expr) -> Result<TypePtr, Diagnostic> override {
    auto objectType = expr.object->accept(*this);
    if (!objectType) {
      return std::unexpected(objectType.error());
    }
    if (objectType.value()->base != BaseType::Struct) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = "Method call requires a struct instance.",
        .span = expr.span,
      });
    }

    const auto mangled = std::format("{}_{}", objectType.value()->name, expr.method);
    auto found = typed.functionTypes.find(mangled);
    if (found == typed.functionTypes.end()) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = std::format("Unknown method '{}.{}'.", objectType.value()->name, expr.method),
        .span = expr.span,
      });
    }
    if (found->second.params.empty()) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = std::format("Method '{}.{}' has invalid signature.", objectType.value()->name, expr.method),
        .span = expr.span,
      });
    }
    if (!sameType(found->second.params[0], objectType.value())) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = std::format("Method '{}.{}' has incompatible self type.", objectType.value()->name, expr.method),
        .span = expr.span,
      });
    }
    if (expr.args.size() + 1 != found->second.params.size()) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = std::format(
          "Method '{}.{}' expects {} argument(s), got {}.",
          objectType.value()->name,
          expr.method,
          found->second.params.size() - 1,
          expr.args.size()
        ),
        .span = expr.span,
      });
    }
    for (std::size_t i = 0; i < expr.args.size(); ++i) {
      auto t = expr.args[i]->accept(*this);
      if (!t) {
        return std::unexpected(t.error());
      }
      if (!sameType(t.value(), found->second.params[i + 1])) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::SemanticError,
          .message = std::format("Argument type mismatch at position {} in method call '{}.{}'.", i + 1, objectType.value()->name, expr.method),
          .span = expr.args[i]->span,
        });
      }
    }
    return found->second.returnType;
  }

  auto visit(const IndexExpr &expr) -> Result<TypePtr, Diagnostic> override {
    auto arrayType = expr.array->accept(*this);
    if (!arrayType) {
      return std::unexpected(arrayType.error());
    }
    if (arrayType.value()->base != BaseType::Array || !arrayType.value()->elementType) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = "Index access requires an array value.",
        .span = expr.array->span,
      });
    }
    auto idxType = expr.index->accept(*this);
    if (!idxType) {
      return std::unexpected(idxType.error());
    }
    if (idxType.value()->base != BaseType::I32) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = "Array index must be i32.",
        .span = expr.index->span,
      });
    }
    return arrayType.value()->elementType;
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

  auto visit(const ArrayAssignStmt &stmt) -> Result<void, Diagnostic> override {
    auto lhs = lookupSymbol(stmt.arrayName, stmt.span);
    if (!lhs) {
      return std::unexpected(lhs.error());
    }
    if (lhs.value()->base != BaseType::Array || !lhs.value()->elementType) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = std::format("'{}' is not an array.", stmt.arrayName),
        .span = stmt.span,
      });
    }
    auto idxType = stmt.index->accept(*this);
    if (!idxType) {
      return std::unexpected(idxType.error());
    }
    if (idxType.value()->base != BaseType::I32) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = "Array index must be i32.",
        .span = stmt.index->span,
      });
    }
    auto rhs = stmt.value->accept(*this);
    if (!rhs) {
      return std::unexpected(rhs.error());
    }
    if (!sameType(lhs.value()->elementType, rhs.value())) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = std::format("Type mismatch in indexed assignment to '{}'.", stmt.arrayName),
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
    if (!sameType(currentFunctionReturnType, actual)) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::SemanticError,
        .message = "Inconsistent return type in function.",
        .span = stmt.span,
      });
    }
    return {};
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
