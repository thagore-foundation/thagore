#include "thagore/frontend/parser.hpp"

#include <format>
#include <optional>
#include <utility>

namespace thagore {
namespace {

class ParserImpl {
public:
  explicit ParserImpl(std::span<const Token> tokens_) : tokens(tokens_) {}

  auto parseModule() -> Result<std::unique_ptr<ModuleDecl>, Diagnostic> {
    std::vector<std::unique_ptr<FunctionDecl>> functions {};
    while (!check(TokenKind::Eof)) {
      skipNewlines();
      if (check(TokenKind::Eof)) {
        break;
      }
      auto fn = parseFunctionDecl();
      if (!fn) {
        return std::unexpected(fn.error());
      }
      functions.push_back(std::move(fn.value()));
      skipNewlines();
    }

    SourceSpan span {};
    if (!tokens.empty()) {
      span = mergeSpan(tokens.front().span, tokens.back().span);
    }
    return std::make_unique<ModuleDecl>(std::move(functions), span);
  }

private:
  std::span<const Token> tokens;
  std::size_t current {0};

  auto parseFunctionDecl() -> Result<std::unique_ptr<FunctionDecl>, Diagnostic> {
    const Token *funcTok = consume(TokenKind::KwFunc, "Expected 'func'.");
    if (!funcTok) {
      return std::unexpected(lastError("Expected 'func'."));
    }

    const Token *name = consume(TokenKind::Identifier, "Expected function name.");
    if (!name) {
      return std::unexpected(lastError("Expected function name."));
    }

    if (!consume(TokenKind::LParen, "Expected '(' after function name.")) {
      return std::unexpected(lastError("Expected '(' after function name."));
    }

    std::vector<std::string> params {};
    if (!check(TokenKind::RParen)) {
      while (true) {
        const Token *param = consume(TokenKind::Identifier, "Expected parameter name.");
        if (!param) {
          return std::unexpected(lastError("Expected parameter name."));
        }
        params.push_back(param->lexeme);
        if (!match(TokenKind::Comma)) {
          break;
        }
      }
    }

    if (!consume(TokenKind::RParen, "Expected ')' after parameter list.")) {
      return std::unexpected(lastError("Expected ')' after parameter list."));
    }
    if (!consume(TokenKind::Colon, "Expected ':' after function signature.")) {
      return std::unexpected(lastError("Expected ':' after function signature."));
    }
    if (!consume(TokenKind::Newline, "Expected newline after function signature.")) {
      return std::unexpected(lastError("Expected newline after function signature."));
    }

    auto body = parseIndentedBlock();
    if (!body) {
      return std::unexpected(body.error());
    }
    auto bodyPtr = std::move(body.value());
    auto fnSpan = mergeSpan(funcTok->span, bodyPtr->span);

    return std::make_unique<FunctionDecl>(
      name->lexeme,
      std::move(params),
      std::move(bodyPtr),
      fnSpan
    );
  }

  auto parseIndentedBlock() -> Result<std::unique_ptr<BlockStmt>, Diagnostic> {
    const Token *indent = consume(TokenKind::Indent, "Expected INDENT.");
    if (!indent) {
      return std::unexpected(lastError("Expected INDENT."));
    }
    std::vector<std::unique_ptr<Stmt>> statements {};
    skipNewlines();
    while (!check(TokenKind::Dedent) && !check(TokenKind::Eof)) {
      auto stmt = parseStatement();
      if (!stmt) {
        return std::unexpected(stmt.error());
      }
      statements.push_back(std::move(stmt.value()));
      skipNewlines();
    }
    const Token *dedent = consume(TokenKind::Dedent, "Expected DEDENT.");
    if (!dedent) {
      return std::unexpected(lastError("Expected DEDENT."));
    }
    return std::make_unique<BlockStmt>(std::move(statements), mergeSpan(indent->span, dedent->span));
  }

  auto parseStatement() -> Result<std::unique_ptr<Stmt>, Diagnostic> {
    if (match(TokenKind::KwLet)) {
      return parseLetStatement(previous());
    }
    if (match(TokenKind::KwReturn)) {
      return parseReturnStatement(previous());
    }
    if (match(TokenKind::KwIf)) {
      return parseIfStatement(previous());
    }
    if (match(TokenKind::KwLoop)) {
      return parseLoopStatement(previous());
    }

    if (check(TokenKind::Identifier) && checkNext(TokenKind::Equal)) {
      const Token *id = advance();
      const Token *eq = consume(TokenKind::Equal, "Expected '='.");
      if (!eq) {
        return std::unexpected(lastError("Expected '='."));
      }
      auto value = parseExpression(0);
      if (!value) {
        return std::unexpected(value.error());
      }
      auto end = consume(TokenKind::Newline, "Expected newline after assignment.");
      if (!end) {
        return std::unexpected(lastError("Expected newline after assignment."));
      }
      return std::make_unique<AssignStmt>(
        id->lexeme,
        std::move(value.value()),
        mergeSpan(id->span, end->span)
      );
    }

    auto expr = parseExpression(0);
    if (!expr) {
      return std::unexpected(expr.error());
    }
    const Token *end = consume(TokenKind::Newline, "Expected newline after expression.");
    if (!end) {
      return std::unexpected(lastError("Expected newline after expression."));
    }
    auto exprPtr = std::move(expr.value());
    auto stmtSpan = mergeSpan(exprPtr->span, end->span);
    return std::make_unique<ExprStmt>(std::move(exprPtr), stmtSpan);
  }

  auto parseLetStatement(const Token *letTok) -> Result<std::unique_ptr<Stmt>, Diagnostic> {
    const Token *name = consume(TokenKind::Identifier, "Expected variable name.");
    if (!name) {
      return std::unexpected(lastError("Expected variable name."));
    }
    if (!consume(TokenKind::Equal, "Expected '=' after variable name.")) {
      return std::unexpected(lastError("Expected '=' after variable name."));
    }
    auto init = parseExpression(0);
    if (!init) {
      return std::unexpected(init.error());
    }
    const Token *end = consume(TokenKind::Newline, "Expected newline after let statement.");
    if (!end) {
      return std::unexpected(lastError("Expected newline after let statement."));
    }
    return std::make_unique<LetStmt>(name->lexeme, std::move(init.value()), mergeSpan(letTok->span, end->span));
  }

  auto parseReturnStatement(const Token *returnTok) -> Result<std::unique_ptr<Stmt>, Diagnostic> {
    if (check(TokenKind::Newline)) {
      const Token *end = advance();
      return std::make_unique<ReturnStmt>(nullptr, mergeSpan(returnTok->span, end->span));
    }
    auto value = parseExpression(0);
    if (!value) {
      return std::unexpected(value.error());
    }
    const Token *end = consume(TokenKind::Newline, "Expected newline after return statement.");
    if (!end) {
      return std::unexpected(lastError("Expected newline after return statement."));
    }
    return std::make_unique<ReturnStmt>(std::move(value.value()), mergeSpan(returnTok->span, end->span));
  }

  auto parseIfStatement(const Token *ifTok) -> Result<std::unique_ptr<Stmt>, Diagnostic> {
    auto cond = parseExpression(0);
    if (!cond) {
      return std::unexpected(cond.error());
    }
    if (!consume(TokenKind::Colon, "Expected ':' after if condition.")) {
      return std::unexpected(lastError("Expected ':' after if condition."));
    }
    if (!consume(TokenKind::Newline, "Expected newline after if header.")) {
      return std::unexpected(lastError("Expected newline after if header."));
    }
    auto block = parseIndentedBlock();
    if (!block) {
      return std::unexpected(block.error());
    }
    auto condExpr = std::move(cond.value());
    auto thenBlock = std::move(block.value());
    auto stmtSpan = mergeSpan(ifTok->span, thenBlock->span);
    return std::make_unique<IfStmt>(std::move(condExpr), std::move(thenBlock), stmtSpan);
  }

  auto parseLoopStatement(const Token *loopTok) -> Result<std::unique_ptr<Stmt>, Diagnostic> {
    if (!consume(TokenKind::Colon, "Expected ':' after loop.")) {
      return std::unexpected(lastError("Expected ':' after loop."));
    }
    if (!consume(TokenKind::Newline, "Expected newline after loop header.")) {
      return std::unexpected(lastError("Expected newline after loop header."));
    }
    auto block = parseIndentedBlock();
    if (!block) {
      return std::unexpected(block.error());
    }
    auto loopBody = std::move(block.value());
    auto stmtSpan = mergeSpan(loopTok->span, loopBody->span);
    return std::make_unique<LoopStmt>(std::move(loopBody), stmtSpan);
  }

  auto parseExpression(int minBp) -> Result<std::unique_ptr<Expr>, Diagnostic> {
    auto lhs = parsePrefix();
    if (!lhs) {
      return std::unexpected(lhs.error());
    }

    while (true) {
      const auto opInfo = infixBindingPower(peek().kind);
      if (!opInfo.has_value() || opInfo->first < minBp) {
        break;
      }
      const Token *opToken = advance();
      auto rhs = parseExpression(opInfo->second);
      if (!rhs) {
        return std::unexpected(rhs.error());
      }
      const auto op = tokenToBinaryOp(opToken->kind);
      auto left = std::move(lhs.value());
      auto right = std::move(rhs.value());
      auto exprSpan = mergeSpan(left->span, right->span);
      lhs = std::make_unique<BinaryExpr>(
        op,
        std::move(left),
        std::move(right),
        exprSpan
      );
    }

    return lhs;
  }

  auto parsePrefix() -> Result<std::unique_ptr<Expr>, Diagnostic> {
    const Token *tok = advance();
    if (!tok) {
      return std::unexpected(lastError("Unexpected end of input."));
    }

    switch (tok->kind) {
      case TokenKind::Integer:
        return std::make_unique<LiteralExpr>(LiteralExpr::Kind::Int, tok->lexeme, tok->span);
      case TokenKind::String:
        return std::make_unique<LiteralExpr>(LiteralExpr::Kind::String, tok->lexeme, tok->span);
      case TokenKind::Identifier: {
        if (match(TokenKind::LParen)) {
          std::vector<std::unique_ptr<Expr>> args {};
          if (!check(TokenKind::RParen)) {
            while (true) {
              auto arg = parseExpression(0);
              if (!arg) {
                return std::unexpected(arg.error());
              }
              args.push_back(std::move(arg.value()));
              if (!match(TokenKind::Comma)) {
                break;
              }
            }
          }
          const Token *end = consume(TokenKind::RParen, "Expected ')' after call arguments.");
          if (!end) {
            return std::unexpected(lastError("Expected ')' after call arguments."));
          }
          return std::make_unique<CallExpr>(tok->lexeme, std::move(args), mergeSpan(tok->span, end->span));
        }
        return std::make_unique<IdentifierExpr>(tok->lexeme, tok->span);
      }
      case TokenKind::LParen: {
        auto expr = parseExpression(0);
        if (!expr) {
          return std::unexpected(expr.error());
        }
        if (!consume(TokenKind::RParen, "Expected ')' after expression.")) {
          return std::unexpected(lastError("Expected ')' after expression."));
        }
        return expr;
      }
      default:
        return std::unexpected(makeError(tok->span, std::format("Unexpected token '{}'.", tokenKindName(tok->kind))));
    }
  }

  auto infixBindingPower(TokenKind kind) -> std::optional<std::pair<int, int>> {
    switch (kind) {
      case TokenKind::EqEq:
      case TokenKind::NotEq:
      case TokenKind::Less:
      case TokenKind::LessEq:
      case TokenKind::Greater:
      case TokenKind::GreaterEq:
        return std::pair {10, 11};
      case TokenKind::Plus:
      case TokenKind::Minus:
        return std::pair {20, 21};
      case TokenKind::Star:
      case TokenKind::Slash:
        return std::pair {30, 31};
      default:
        return std::nullopt;
    }
  }

  auto tokenToBinaryOp(TokenKind kind) -> BinaryOp {
    switch (kind) {
      case TokenKind::Plus: return BinaryOp::Add;
      case TokenKind::Minus: return BinaryOp::Sub;
      case TokenKind::Star: return BinaryOp::Mul;
      case TokenKind::Slash: return BinaryOp::Div;
      case TokenKind::EqEq: return BinaryOp::Eq;
      case TokenKind::NotEq: return BinaryOp::Ne;
      case TokenKind::Less: return BinaryOp::Lt;
      case TokenKind::LessEq: return BinaryOp::Le;
      case TokenKind::Greater: return BinaryOp::Gt;
      case TokenKind::GreaterEq: return BinaryOp::Ge;
      default: return BinaryOp::Add;
    }
  }

  void skipNewlines() {
    while (match(TokenKind::Newline)) {
    }
  }

  auto check(TokenKind kind) const -> bool {
    if (isAtEnd()) {
      return kind == TokenKind::Eof;
    }
    return tokens[current].kind == kind;
  }

  auto checkNext(TokenKind kind) const -> bool {
    if (current + 1 >= tokens.size()) {
      return false;
    }
    return tokens[current + 1].kind == kind;
  }

  auto isAtEnd() const -> bool {
    return current >= tokens.size() || tokens[current].kind == TokenKind::Eof;
  }

  auto advance() -> const Token * {
    if (isAtEnd()) {
      return nullptr;
    }
    return &tokens[current++];
  }

  auto peek() const -> const Token & {
    return tokens[current];
  }

  auto previous() const -> const Token * {
    if (current == 0) {
      return nullptr;
    }
    return &tokens[current - 1];
  }

  auto match(TokenKind kind) -> bool {
    if (!check(kind)) {
      return false;
    }
    ++current;
    return true;
  }

  auto consume(TokenKind kind, std::string_view message) -> const Token * {
    if (check(kind)) {
      return advance();
    }
    const auto span = current < tokens.size() ? tokens[current].span : tokens.back().span;
    lastDiagnostic = makeError(span, std::string(message));
    return nullptr;
  }

  auto makeError(const SourceSpan &span, std::string message) -> Diagnostic {
    return Diagnostic {
      .code = ErrorCode::ParseError,
      .message = std::move(message),
      .span = span,
    };
  }

  auto lastError(std::string_view fallback) -> Diagnostic {
    if (lastDiagnostic.has_value()) {
      return *lastDiagnostic;
    }
    const auto span = current < tokens.size() ? tokens[current].span : SourceSpan {};
    return makeError(span, std::string(fallback));
  }

  std::optional<Diagnostic> lastDiagnostic {};
};

} // namespace

auto Parser::parseModule(std::span<const Token> tokens) -> Result<std::unique_ptr<ModuleDecl>, Diagnostic> {
  if (tokens.empty()) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::ParseError,
      .message = "Token stream is empty.",
      .span = {},
    });
  }
  ParserImpl parser {tokens};
  return parser.parseModule();
}

} // namespace thagore
