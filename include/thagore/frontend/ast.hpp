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
  Bool,
  String,
};

enum class OwnershipKind {
  Owned,
  Borrowed,
  Temporary,
};

struct Type {
  BaseType base {BaseType::Unknown};
};

using TypePtr = std::shared_ptr<Type>;

enum class NodeKind : std::uint16_t {
  ModuleDecl,
  FunctionDecl,
  BlockStmt,
  LetStmt,
  AssignStmt,
  ReturnStmt,
  IfStmt,
  LoopStmt,
  ExprStmt,
  BinaryExpr,
  LiteralExpr,
  IdentifierExpr,
  CallExpr,
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
struct LetStmt;
struct AssignStmt;
struct ReturnStmt;
struct IfStmt;
struct LoopStmt;
struct ExprStmt;
struct FunctionDecl;
struct ModuleDecl;

template <typename R>
struct ExprVisitor {
  virtual ~ExprVisitor() = default;
  virtual auto visit(const BinaryExpr &) -> Result<R, Diagnostic> = 0;
  virtual auto visit(const LiteralExpr &) -> Result<R, Diagnostic> = 0;
  virtual auto visit(const IdentifierExpr &) -> Result<R, Diagnostic> = 0;
  virtual auto visit(const CallExpr &) -> Result<R, Diagnostic> = 0;
};

template <typename R>
struct StmtVisitor {
  virtual ~StmtVisitor() = default;
  virtual auto visit(const BlockStmt &) -> Result<R, Diagnostic> = 0;
  virtual auto visit(const LetStmt &) -> Result<R, Diagnostic> = 0;
  virtual auto visit(const AssignStmt &) -> Result<R, Diagnostic> = 0;
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
  IfStmt(std::unique_ptr<Expr> condition_, std::unique_ptr<BlockStmt> thenBlock_, SourceSpan span_)
    : Stmt(NodeKind::IfStmt, std::move(span_)), condition(std::move(condition_)), thenBlock(std::move(thenBlock_)) {}
  auto accept(StmtVisitor<void> &visitor) const -> Result<void, Diagnostic> override { return visitor.visit(*this); }
};

struct LoopStmt final : Stmt {
  std::unique_ptr<BlockStmt> body {};
  LoopStmt(std::unique_ptr<BlockStmt> body_, SourceSpan span_)
    : Stmt(NodeKind::LoopStmt, std::move(span_)), body(std::move(body_)) {}
  auto accept(StmtVisitor<void> &visitor) const -> Result<void, Diagnostic> override { return visitor.visit(*this); }
};

struct FunctionDecl final : Decl {
  std::string name {};
  std::vector<std::string> params {};
  std::unique_ptr<BlockStmt> body {};
  TypePtr returnType {};
  OwnershipKind returnOwnership {OwnershipKind::Temporary};

  FunctionDecl(
    std::string name_,
    std::vector<std::string> params_,
    std::unique_ptr<BlockStmt> body_,
    SourceSpan span_
  )
    : Decl(NodeKind::FunctionDecl, std::move(span_)),
      name(std::move(name_)),
      params(std::move(params_)),
      body(std::move(body_)) {}

  auto accept(DeclVisitor<void> &visitor) const -> Result<void, Diagnostic> override { return visitor.visit(*this); }
};

struct ModuleDecl final : Decl {
  std::vector<std::unique_ptr<FunctionDecl>> functions {};
  std::vector<std::unique_ptr<Stmt>> topLevelStatements {};
  ModuleDecl(
    std::vector<std::unique_ptr<FunctionDecl>> functions_,
    std::vector<std::unique_ptr<Stmt>> topLevelStatements_,
    SourceSpan span_
  )
    : Decl(NodeKind::ModuleDecl, std::move(span_)),
      functions(std::move(functions_)),
      topLevelStatements(std::move(topLevelStatements_)) {}
  auto accept(DeclVisitor<void> &visitor) const -> Result<void, Diagnostic> override { return visitor.visit(*this); }
};

struct TypedModule {
  std::unique_ptr<ModuleDecl> module {};
  std::unordered_map<std::string, TypePtr> functionTypes {};
};

inline auto makeType(BaseType base) -> TypePtr {
  return std::make_shared<Type>(Type {.base = base});
}

} // namespace thagore
