#pragma once

#include "thagore/common/diagnostics.hpp"
#include "thagore/common/result.hpp"
#include "thagore/common/source.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace thagore {

enum class BaseType {
  Unknown,
  Void,
  I32,
  F32,
  Bool,
  String,
  Struct,
  Array,
};

enum class OwnershipKind {
  Owned,
  Borrowed,
  Temporary,
};

struct Type {
  BaseType base {BaseType::Unknown};
  std::string name {};
  std::shared_ptr<Type> elementType {};
  std::size_t arraySize {0};
};

using TypePtr = std::shared_ptr<Type>;

enum class NodeKind : std::uint16_t {
  ModuleDecl,
  FunctionDecl,
  StructDecl,
  BlockStmt,
  LetStmt,
  AssignStmt,
  ReturnStmt,
  IfStmt,
  LoopStmt,
  ExprStmt,
  BinaryExpr,
  LiteralExpr,
  ArrayLiteralExpr,
  IdentifierExpr,
  CallExpr,
  MemberExpr,
  MethodCallExpr,
  IndexExpr,
  ArrayAssignStmt,
};

struct Node {
  NodeKind kind;
  SourceSpan span {};
  explicit Node(NodeKind kind_, SourceSpan span_) : kind(kind_), span(std::move(span_)) {}
  virtual ~Node() = default;
};

struct Expr;
struct Stmt;
struct Decl;
struct BlockStmt;
struct BinaryExpr;
struct LiteralExpr;
struct IdentifierExpr;
struct CallExpr;
struct MemberExpr;
struct MethodCallExpr;
struct ArrayLiteralExpr;
struct IndexExpr;
struct LetStmt;
struct AssignStmt;
struct ArrayAssignStmt;
struct ReturnStmt;
struct IfStmt;
struct LoopStmt;
struct ExprStmt;
struct FunctionDecl;
struct StructDecl;
struct ModuleDecl;

template <typename R>
struct ExprVisitor {
  virtual ~ExprVisitor() = default;
  virtual auto visit(const BinaryExpr &) -> Result<R, Diagnostic> = 0;
  virtual auto visit(const LiteralExpr &) -> Result<R, Diagnostic> = 0;
  virtual auto visit(const ArrayLiteralExpr &) -> Result<R, Diagnostic> = 0;
  virtual auto visit(const IdentifierExpr &) -> Result<R, Diagnostic> = 0;
  virtual auto visit(const CallExpr &) -> Result<R, Diagnostic> = 0;
  virtual auto visit(const MemberExpr &) -> Result<R, Diagnostic> = 0;
  virtual auto visit(const MethodCallExpr &) -> Result<R, Diagnostic> = 0;
  virtual auto visit(const IndexExpr &) -> Result<R, Diagnostic> = 0;
};

template <typename R>
struct StmtVisitor {
  virtual ~StmtVisitor() = default;
  virtual auto visit(const BlockStmt &) -> Result<R, Diagnostic> = 0;
  virtual auto visit(const LetStmt &) -> Result<R, Diagnostic> = 0;
  virtual auto visit(const AssignStmt &) -> Result<R, Diagnostic> = 0;
  virtual auto visit(const ArrayAssignStmt &) -> Result<R, Diagnostic> = 0;
  virtual auto visit(const ReturnStmt &) -> Result<R, Diagnostic> = 0;
  virtual auto visit(const IfStmt &) -> Result<R, Diagnostic> = 0;
  virtual auto visit(const LoopStmt &) -> Result<R, Diagnostic> = 0;
  virtual auto visit(const ExprStmt &) -> Result<R, Diagnostic> = 0;
};

template <typename R>
struct DeclVisitor {
  virtual ~DeclVisitor() = default;
  virtual auto visit(const FunctionDecl &) -> Result<R, Diagnostic> = 0;
  virtual auto visit(const ModuleDecl &) -> Result<R, Diagnostic> = 0;
};

struct Expr : Node {
  TypePtr inferredType {};
  OwnershipKind ownership {OwnershipKind::Temporary};
  explicit Expr(NodeKind kind_, SourceSpan span_) : Node(kind_, std::move(span_)) {}
  virtual auto accept(ExprVisitor<TypePtr> &) const -> Result<TypePtr, Diagnostic> = 0;
};

struct Stmt : Node {
  explicit Stmt(NodeKind kind_, SourceSpan span_) : Node(kind_, std::move(span_)) {}
  virtual auto accept(StmtVisitor<void> &) const -> Result<void, Diagnostic> = 0;
};

struct Decl : Node {
  explicit Decl(NodeKind kind_, SourceSpan span_) : Node(kind_, std::move(span_)) {}
  virtual auto accept(DeclVisitor<void> &) const -> Result<void, Diagnostic> = 0;
};

enum class BinaryOp {
  Add,
  Sub,
  Mul,
  Div,
  Eq,
  Ne,
  Lt,
  Le,
  Gt,
  Ge,
};

struct LiteralExpr final : Expr {
  enum class Kind {
    Int,
    Float,
    String,
  };

  Kind literalKind {};
  std::string value {};
  LiteralExpr(Kind kind_, std::string value_, SourceSpan span_)
    : Expr(NodeKind::LiteralExpr, std::move(span_)), literalKind(kind_), value(std::move(value_)) {}
  auto accept(ExprVisitor<TypePtr> &visitor) const -> Result<TypePtr, Diagnostic> override { return visitor.visit(*this); }
};

struct IdentifierExpr final : Expr {
  std::string name {};
  explicit IdentifierExpr(std::string name_, SourceSpan span_)
    : Expr(NodeKind::IdentifierExpr, std::move(span_)), name(std::move(name_)) {}
  auto accept(ExprVisitor<TypePtr> &visitor) const -> Result<TypePtr, Diagnostic> override { return visitor.visit(*this); }
};

struct ArrayLiteralExpr final : Expr {
  std::vector<std::unique_ptr<Expr>> elements {};
  explicit ArrayLiteralExpr(std::vector<std::unique_ptr<Expr>> elements_, SourceSpan span_)
    : Expr(NodeKind::ArrayLiteralExpr, std::move(span_)), elements(std::move(elements_)) {}
  auto accept(ExprVisitor<TypePtr> &visitor) const -> Result<TypePtr, Diagnostic> override { return visitor.visit(*this); }
};

struct BinaryExpr final : Expr {
  BinaryOp op {};
  std::unique_ptr<Expr> left {};
  std::unique_ptr<Expr> right {};
  BinaryExpr(BinaryOp op_, std::unique_ptr<Expr> left_, std::unique_ptr<Expr> right_, SourceSpan span_)
    : Expr(NodeKind::BinaryExpr, std::move(span_)), op(op_), left(std::move(left_)), right(std::move(right_)) {}
  auto accept(ExprVisitor<TypePtr> &visitor) const -> Result<TypePtr, Diagnostic> override { return visitor.visit(*this); }
};

struct CallExpr final : Expr {
  std::string callee {};
  std::vector<std::unique_ptr<Expr>> args {};
  CallExpr(std::string callee_, std::vector<std::unique_ptr<Expr>> args_, SourceSpan span_)
    : Expr(NodeKind::CallExpr, std::move(span_)), callee(std::move(callee_)), args(std::move(args_)) {}
  auto accept(ExprVisitor<TypePtr> &visitor) const -> Result<TypePtr, Diagnostic> override { return visitor.visit(*this); }
};

struct MemberExpr final : Expr {
  std::unique_ptr<Expr> object {};
  std::string member {};
  MemberExpr(std::unique_ptr<Expr> object_, std::string member_, SourceSpan span_)
    : Expr(NodeKind::MemberExpr, std::move(span_)), object(std::move(object_)), member(std::move(member_)) {}
  auto accept(ExprVisitor<TypePtr> &visitor) const -> Result<TypePtr, Diagnostic> override { return visitor.visit(*this); }
};

struct MethodCallExpr final : Expr {
  std::unique_ptr<Expr> object {};
  std::string method {};
  std::vector<std::unique_ptr<Expr>> args {};
  MethodCallExpr(std::unique_ptr<Expr> object_, std::string method_, std::vector<std::unique_ptr<Expr>> args_, SourceSpan span_)
    : Expr(NodeKind::MethodCallExpr, std::move(span_)),
      object(std::move(object_)),
      method(std::move(method_)),
      args(std::move(args_)) {}
  auto accept(ExprVisitor<TypePtr> &visitor) const -> Result<TypePtr, Diagnostic> override { return visitor.visit(*this); }
};

struct IndexExpr final : Expr {
  std::unique_ptr<Expr> array {};
  std::unique_ptr<Expr> index {};
  IndexExpr(std::unique_ptr<Expr> array_, std::unique_ptr<Expr> index_, SourceSpan span_)
    : Expr(NodeKind::IndexExpr, std::move(span_)), array(std::move(array_)), index(std::move(index_)) {}
  auto accept(ExprVisitor<TypePtr> &visitor) const -> Result<TypePtr, Diagnostic> override { return visitor.visit(*this); }
};

struct ExprStmt final : Stmt {
  std::unique_ptr<Expr> expr {};
  ExprStmt(std::unique_ptr<Expr> expr_, SourceSpan span_)
    : Stmt(NodeKind::ExprStmt, std::move(span_)), expr(std::move(expr_)) {}
  auto accept(StmtVisitor<void> &visitor) const -> Result<void, Diagnostic> override { return visitor.visit(*this); }
};

struct LetStmt final : Stmt {
  std::string name {};
  std::unique_ptr<Expr> init {};
  LetStmt(std::string name_, std::unique_ptr<Expr> init_, SourceSpan span_)
    : Stmt(NodeKind::LetStmt, std::move(span_)), name(std::move(name_)), init(std::move(init_)) {}
  auto accept(StmtVisitor<void> &visitor) const -> Result<void, Diagnostic> override { return visitor.visit(*this); }
};

struct AssignStmt final : Stmt {
  std::string name {};
  std::unique_ptr<Expr> value {};
  AssignStmt(std::string name_, std::unique_ptr<Expr> value_, SourceSpan span_)
    : Stmt(NodeKind::AssignStmt, std::move(span_)), name(std::move(name_)), value(std::move(value_)) {}
  auto accept(StmtVisitor<void> &visitor) const -> Result<void, Diagnostic> override { return visitor.visit(*this); }
};

struct ArrayAssignStmt final : Stmt {
  std::string arrayName {};
  std::unique_ptr<Expr> index {};
  std::unique_ptr<Expr> value {};
  ArrayAssignStmt(std::string arrayName_, std::unique_ptr<Expr> index_, std::unique_ptr<Expr> value_, SourceSpan span_)
    : Stmt(NodeKind::ArrayAssignStmt, std::move(span_)),
      arrayName(std::move(arrayName_)),
      index(std::move(index_)),
      value(std::move(value_)) {}
  auto accept(StmtVisitor<void> &visitor) const -> Result<void, Diagnostic> override { return visitor.visit(*this); }
};

struct ReturnStmt final : Stmt {
  std::unique_ptr<Expr> value {};
  ReturnStmt(std::unique_ptr<Expr> value_, SourceSpan span_)
    : Stmt(NodeKind::ReturnStmt, std::move(span_)), value(std::move(value_)) {}
  auto accept(StmtVisitor<void> &visitor) const -> Result<void, Diagnostic> override { return visitor.visit(*this); }
};

struct BlockStmt final : Stmt {
  std::vector<std::unique_ptr<Stmt>> statements {};
  explicit BlockStmt(std::vector<std::unique_ptr<Stmt>> statements_, SourceSpan span_)
    : Stmt(NodeKind::BlockStmt, std::move(span_)), statements(std::move(statements_)) {}
  auto accept(StmtVisitor<void> &visitor) const -> Result<void, Diagnostic> override { return visitor.visit(*this); }
};

struct IfStmt final : Stmt {
  std::unique_ptr<Expr> condition {};
  std::unique_ptr<BlockStmt> thenBlock {};
  std::unique_ptr<BlockStmt> elseBlock {};
  IfStmt(
    std::unique_ptr<Expr> condition_,
    std::unique_ptr<BlockStmt> thenBlock_,
    std::unique_ptr<BlockStmt> elseBlock_,
    SourceSpan span_
  )
    : Stmt(NodeKind::IfStmt, std::move(span_)),
      condition(std::move(condition_)),
      thenBlock(std::move(thenBlock_)),
      elseBlock(std::move(elseBlock_)) {}
  auto accept(StmtVisitor<void> &visitor) const -> Result<void, Diagnostic> override { return visitor.visit(*this); }
};

struct LoopStmt final : Stmt {
  std::unique_ptr<Expr> condition {};
  std::unique_ptr<BlockStmt> body {};
  LoopStmt(std::unique_ptr<Expr> condition_, std::unique_ptr<BlockStmt> body_, SourceSpan span_)
    : Stmt(NodeKind::LoopStmt, std::move(span_)), condition(std::move(condition_)), body(std::move(body_)) {}
  auto accept(StmtVisitor<void> &visitor) const -> Result<void, Diagnostic> override { return visitor.visit(*this); }
};

struct FunctionDecl final : Decl {
  struct Param {
    std::string name {};
    TypePtr type {};
    SourceSpan span {};
  };

  std::string name {};
  std::string sourceName {};
  std::string methodOwner {};
  std::vector<Param> params {};
  std::unique_ptr<BlockStmt> body {};
  TypePtr returnType {};
  OwnershipKind returnOwnership {OwnershipKind::Temporary};

  FunctionDecl(
    std::string name_,
    std::vector<Param> params_,
    std::unique_ptr<BlockStmt> body_,
    TypePtr returnType_,
    SourceSpan span_
  )
    : Decl(NodeKind::FunctionDecl, std::move(span_)),
      name(std::move(name_)),
      sourceName(name),
      params(std::move(params_)),
      body(std::move(body_)),
      returnType(std::move(returnType_)) {}

  auto accept(DeclVisitor<void> &visitor) const -> Result<void, Diagnostic> override { return visitor.visit(*this); }
};

struct StructDecl final : Decl {
  struct Field {
    std::string name {};
    TypePtr type {};
    SourceSpan span {};
  };

  std::string name {};
  std::vector<Field> fields {};

  StructDecl(std::string name_, std::vector<Field> fields_, SourceSpan span_)
    : Decl(NodeKind::StructDecl, std::move(span_)), name(std::move(name_)), fields(std::move(fields_)) {}

  auto accept(DeclVisitor<void> &) const -> Result<void, Diagnostic> override { return {}; }
};

struct ModuleDecl final : Decl {
  std::vector<std::unique_ptr<StructDecl>> structs {};
  std::vector<std::unique_ptr<FunctionDecl>> functions {};
  std::vector<std::unique_ptr<Stmt>> topLevelStatements {};
  ModuleDecl(
    std::vector<std::unique_ptr<StructDecl>> structs_,
    std::vector<std::unique_ptr<FunctionDecl>> functions_,
    std::vector<std::unique_ptr<Stmt>> topLevelStatements_,
    SourceSpan span_
  )
    : Decl(NodeKind::ModuleDecl, std::move(span_)),
      structs(std::move(structs_)),
      functions(std::move(functions_)),
      topLevelStatements(std::move(topLevelStatements_)) {}
  auto accept(DeclVisitor<void> &visitor) const -> Result<void, Diagnostic> override { return visitor.visit(*this); }
};

struct TypedModule {
  struct StructType {
    std::vector<StructDecl::Field> fields {};
  };

  struct FunctionSignature {
    TypePtr returnType {};
    std::vector<TypePtr> params {};
  };

  std::unique_ptr<ModuleDecl> module {};
  std::unordered_map<std::string, StructType> structTypes {};
  std::unordered_map<std::string, FunctionSignature> functionTypes {};
};

inline auto makeType(BaseType base) -> TypePtr {
  return std::make_shared<Type>(Type {.base = base});
}

inline auto makeStructType(std::string name) -> TypePtr {
  return std::make_shared<Type>(Type {.base = BaseType::Struct, .name = std::move(name)});
}

inline auto makeArrayType(TypePtr elementType, std::size_t arraySize) -> TypePtr {
  return std::make_shared<Type>(Type {.base = BaseType::Array, .elementType = std::move(elementType), .arraySize = arraySize});
}

} // namespace thagore
