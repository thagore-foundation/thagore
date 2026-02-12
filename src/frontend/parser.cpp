#include "thagore/frontend/parser.hpp"

#include <format>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace thagore {
namespace {

class ParserImpl {
public:
  explicit ParserImpl(std::span<const Token> tokens_) : tokens(tokens_) {}

  auto parseModule() -> Result<std::unique_ptr<ModuleDecl>, Diagnostic> {
    std::vector<ImportDecl> imports {};
    std::vector<std::unique_ptr<StructDecl>> structs {};
    std::vector<std::unique_ptr<FunctionDecl>> functions {};
    std::vector<std::unique_ptr<Stmt>> topLevelStatements {};
    while (!check(TokenKind::Eof)) {
      skipNewlines();
      if (check(TokenKind::Eof)) {
        break;
      }
      if (check(TokenKind::KwFunc)) {
        auto fn = parseFunctionDecl(std::nullopt);
        if (!fn) {
          return std::unexpected(fn.error());
        }
        functions.push_back(std::move(fn.value()));
      } else if (check(TokenKind::KwImport)) {
        auto imp = parseImportDecl();
        if (!imp) {
          return std::unexpected(imp.error());
        }
        imports.push_back(std::move(imp.value()));
      } else if (check(TokenKind::KwExtern)) {
        auto externFn = parseExternFunctionDecl();
        if (!externFn) {
          return std::unexpected(externFn.error());
        }
        functions.push_back(std::move(externFn.value()));
      } else if (check(TokenKind::KwStruct)) {
        auto st = parseStructDecl();
        if (!st) {
          return std::unexpected(st.error());
        }
        structs.push_back(std::move(st.value()));
      } else if (check(TokenKind::KwImpl)) {
        auto implFns = parseImplBlock();
        if (!implFns) {
          return std::unexpected(implFns.error());
        }
        for (auto &fn : implFns.value()) {
          functions.push_back(std::move(fn));
        }
      } else {
        auto stmt = parseStatement();
        if (!stmt) {
          return std::unexpected(stmt.error());
        }
        topLevelStatements.push_back(std::move(stmt.value()));
      }
      skipNewlines();
    }

    SourceSpan span {};
    if (!tokens.empty()) {
      span = mergeSpan(tokens.front().span, tokens.back().span);
    }
    return std::make_unique<ModuleDecl>(
      std::move(imports),
      std::move(structs),
      std::move(functions),
      std::move(topLevelStatements),
      span
    );
  }

private:
  std::span<const Token> tokens;
  std::size_t current {0};

  auto parseTypeName(const Token *typeTok) -> Result<TypePtr, Diagnostic> {
    if (typeTok == nullptr || typeTok->kind != TokenKind::Identifier) {
      return std::unexpected(lastError("Expected type name."));
    }
    if (typeTok->lexeme == "i32") {
      return makeType(BaseType::I32);
    }
    if (typeTok->lexeme == "f32") {
      return makeType(BaseType::F32);
    }
    if (typeTok->lexeme == "f64") {
      return makeType(BaseType::F64);
    }
    if (typeTok->lexeme == "ptr") {
      return makeType(BaseType::Pointer);
    }
    if (typeTok->lexeme == "bool") {
      return makeType(BaseType::Bool);
    }
    if (typeTok->lexeme == "String") {
      return makeType(BaseType::String);
    }
    if (typeTok->lexeme == "void") {
      return makeType(BaseType::Void);
    }
    return makeStructType(typeTok->lexeme);
  }

  auto parseType() -> Result<TypePtr, Diagnostic> {
    if (match(TokenKind::LBracket)) {
      auto elementType = parseType();
      if (!elementType) {
        return std::unexpected(elementType.error());
      }
      if (!consume(TokenKind::Semicolon, "Expected ';' in array type.")) {
        return std::unexpected(lastError("Expected ';' in array type."));
      }
      const Token *sizeTok = consume(TokenKind::Integer, "Expected array size.");
      if (!sizeTok) {
        return std::unexpected(lastError("Expected array size."));
      }
      if (!consume(TokenKind::RBracket, "Expected ']' after array type.")) {
        return std::unexpected(lastError("Expected ']' after array type."));
      }
      std::size_t arraySize = 0;
      for (char ch : sizeTok->lexeme) {
        arraySize = (arraySize * 10) + static_cast<std::size_t>(ch - '0');
      }
      if (arraySize == 0) {
        return std::unexpected(makeError(sizeTok->span, "Array size must be greater than zero."));
      }
      return makeArrayType(elementType.value(), arraySize);
    }
    const Token *typeTok = consume(TokenKind::Identifier, "Expected type name.");
    if (!typeTok) {
      return std::unexpected(lastError("Expected type name."));
    }
    return parseTypeName(typeTok);
  }

  auto parseStructDecl() -> Result<std::unique_ptr<StructDecl>, Diagnostic> {
    const Token *structTok = consume(TokenKind::KwStruct, "Expected 'struct'.");
    if (!structTok) {
      return std::unexpected(lastError("Expected 'struct'."));
    }
    const Token *name = consume(TokenKind::Identifier, "Expected struct name.");
    if (!name) {
      return std::unexpected(lastError("Expected struct name."));
    }
    if (!consume(TokenKind::Colon, "Expected ':' after struct name.")) {
      return std::unexpected(lastError("Expected ':' after struct name."));
    }
    if (!consume(TokenKind::Newline, "Expected newline after struct header.")) {
      return std::unexpected(lastError("Expected newline after struct header."));
    }
    if (!consume(TokenKind::Indent, "Expected INDENT in struct body.")) {
      return std::unexpected(lastError("Expected INDENT in struct body."));
    }

    std::vector<StructDecl::Field> fields {};
    skipNewlines();
    while (!check(TokenKind::Dedent) && !check(TokenKind::Eof)) {
      const Token *fieldName = consume(TokenKind::Identifier, "Expected field name.");
      if (!fieldName) {
        return std::unexpected(lastError("Expected field name."));
      }
      if (!consume(TokenKind::Colon, "Expected ':' after field name.")) {
        return std::unexpected(lastError("Expected ':' after field name."));
      }
      auto fieldType = parseType();
      if (!fieldType) {
        return std::unexpected(fieldType.error());
      }
      if (fieldType.value()->base == BaseType::Void) {
        return std::unexpected(makeError(fieldName->span, "Field type cannot be void."));
      }
      const Token *lineEnd = consume(TokenKind::Newline, "Expected newline after struct field.");
      if (!lineEnd) {
        return std::unexpected(lastError("Expected newline after struct field."));
      }
      fields.push_back(StructDecl::Field {
        .name = fieldName->lexeme,
        .type = fieldType.value(),
        .span = mergeSpan(fieldName->span, previous()->span),
      });
      skipNewlines();
    }

    const Token *dedent = consume(TokenKind::Dedent, "Expected DEDENT after struct body.");
    if (!dedent) {
      return std::unexpected(lastError("Expected DEDENT after struct body."));
    }
    auto structSpan = mergeSpan(structTok->span, dedent->span);
    return std::make_unique<StructDecl>(name->lexeme, std::move(fields), structSpan);
  }

  auto parseImportDecl() -> Result<ImportDecl, Diagnostic> {
    const Token *importTok = consume(TokenKind::KwImport, "Expected 'import'.");
    if (!importTok) {
      return std::unexpected(lastError("Expected 'import'."));
    }
    const Token *pathTok = nullptr;
    if (check(TokenKind::String) || check(TokenKind::Identifier)) {
      pathTok = advance();
    } else {
      return std::unexpected(lastError("Expected import path after 'import'."));
    }

    std::string alias {};
    if (match(TokenKind::KwAs)) {
      const Token *aliasTok = consume(TokenKind::Identifier, "Expected alias after 'as'.");
      if (!aliasTok) {
        return std::unexpected(lastError("Expected alias after 'as'."));
      }
      alias = aliasTok->lexeme;
    } else {
      const auto path = std::filesystem::path(pathTok->lexeme);
      alias = path.stem().string();
      if (alias.empty()) {
        alias = path.filename().string();
      }
      if (alias.empty()) {
        return std::unexpected(makeError(pathTok->span, "Cannot infer module alias from import path."));
      }
    }

    const Token *end = consume(TokenKind::Newline, "Expected newline after import declaration.");
    if (!end) {
      return std::unexpected(lastError("Expected newline after import declaration."));
    }
    return ImportDecl {
      .path = pathTok->lexeme,
      .alias = alias,
      .span = mergeSpan(importTok->span, end->span),
    };
  }

  auto parseFunctionDecl(std::optional<std::string> implType) -> Result<std::unique_ptr<FunctionDecl>, Diagnostic> {
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

    std::vector<FunctionDecl::Param> params {};
    if (!check(TokenKind::RParen)) {
      while (true) {
        const Token *param = consume(TokenKind::Identifier, "Expected parameter name.");
        if (!param) {
          return std::unexpected(lastError("Expected parameter name."));
        }
        TypePtr paramType {};
        SourceSpan typeSpan = param->span;
        const bool isImplicitSelf = implType.has_value() && params.empty() && param->lexeme == "self" && !check(TokenKind::Colon);
        if (isImplicitSelf) {
          paramType = makeStructType(*implType);
        } else {
          if (!consume(TokenKind::Colon, "Expected ':' after parameter name.")) {
            return std::unexpected(lastError("Expected ':' after parameter name."));
          }
          auto parsedParamType = parseType();
          if (!parsedParamType) {
            return std::unexpected(parsedParamType.error());
          }
          if (parsedParamType.value()->base == BaseType::Void) {
            return std::unexpected(makeError(param->span, "Parameter type cannot be void."));
          }
          paramType = parsedParamType.value();
          typeSpan = previous()->span;
        }
        params.push_back(FunctionDecl::Param {
          .name = param->lexeme,
          .type = paramType,
          .span = mergeSpan(param->span, typeSpan),
        });
        if (!match(TokenKind::Comma)) {
          break;
        }
      }
    }

    if (!consume(TokenKind::RParen, "Expected ')' after parameter list.")) {
      return std::unexpected(lastError("Expected ')' after parameter list."));
    }
    if (!consume(TokenKind::Arrow, "Expected '->' before return type.")) {
      return std::unexpected(lastError("Expected '->' before return type."));
    }
    auto returnType = parseType();
    if (!returnType) {
      return std::unexpected(returnType.error());
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

    auto fn = std::make_unique<FunctionDecl>(
      name->lexeme,
      std::move(params),
      std::move(bodyPtr),
      returnType.value(),
      fnSpan
    );
    fn->sourceName = name->lexeme;
    if (implType.has_value()) {
      fn->methodOwner = *implType;
      fn->name = std::format("{}_{}", *implType, name->lexeme);
    }
    return fn;
  }

  auto parseExternFunctionDecl() -> Result<std::unique_ptr<FunctionDecl>, Diagnostic> {
    const Token *externTok = consume(TokenKind::KwExtern, "Expected 'extern'.");
    if (!externTok) {
      return std::unexpected(lastError("Expected 'extern'."));
    }
    const Token *funcTok = consume(TokenKind::KwFunc, "Expected 'func' after 'extern'.");
    if (!funcTok) {
      return std::unexpected(lastError("Expected 'func' after 'extern'."));
    }

    const Token *name = consume(TokenKind::Identifier, "Expected function name.");
    if (!name) {
      return std::unexpected(lastError("Expected function name."));
    }
    if (!consume(TokenKind::LParen, "Expected '(' after function name.")) {
      return std::unexpected(lastError("Expected '(' after function name."));
    }

    std::vector<FunctionDecl::Param> params {};
    if (!check(TokenKind::RParen)) {
      while (true) {
        const Token *param = consume(TokenKind::Identifier, "Expected parameter name.");
        if (!param) {
          return std::unexpected(lastError("Expected parameter name."));
        }
        if (!consume(TokenKind::Colon, "Expected ':' after parameter name.")) {
          return std::unexpected(lastError("Expected ':' after parameter name."));
        }
        auto parsedParamType = parseType();
        if (!parsedParamType) {
          return std::unexpected(parsedParamType.error());
        }
        if (parsedParamType.value()->base == BaseType::Void) {
          return std::unexpected(makeError(param->span, "Parameter type cannot be void."));
        }
        params.push_back(FunctionDecl::Param {
          .name = param->lexeme,
          .type = parsedParamType.value(),
          .span = mergeSpan(param->span, previous()->span),
        });
        if (!match(TokenKind::Comma)) {
          break;
        }
      }
    }

    if (!consume(TokenKind::RParen, "Expected ')' after parameter list.")) {
      return std::unexpected(lastError("Expected ')' after parameter list."));
    }
    if (!consume(TokenKind::Arrow, "Expected '->' before return type.")) {
      return std::unexpected(lastError("Expected '->' before return type."));
    }
    auto returnType = parseType();
    if (!returnType) {
      return std::unexpected(returnType.error());
    }
    const Token *lineEnd = consume(TokenKind::Newline, "Expected newline after extern function declaration.");
    if (!lineEnd) {
      return std::unexpected(lastError("Expected newline after extern function declaration."));
    }

    auto fn = std::make_unique<FunctionDecl>(
      name->lexeme,
      std::move(params),
      nullptr,
      returnType.value(),
      mergeSpan(externTok->span, lineEnd->span)
    );
    fn->sourceName = name->lexeme;
    fn->isExtern = true;
    (void)funcTok;
    return fn;
  }

  auto parseImplBlock() -> Result<std::vector<std::unique_ptr<FunctionDecl>>, Diagnostic> {
    if (!consume(TokenKind::KwImpl, "Expected 'impl'.")) {
      return std::unexpected(lastError("Expected 'impl'."));
    }
    const Token *typeName = consume(TokenKind::Identifier, "Expected type name after 'impl'.");
    if (!typeName) {
      return std::unexpected(lastError("Expected type name after 'impl'."));
    }
    if (!consume(TokenKind::Colon, "Expected ':' after impl type.")) {
      return std::unexpected(lastError("Expected ':' after impl type."));
    }
    if (!consume(TokenKind::Newline, "Expected newline after impl header.")) {
      return std::unexpected(lastError("Expected newline after impl header."));
    }
    if (!consume(TokenKind::Indent, "Expected INDENT in impl block.")) {
      return std::unexpected(lastError("Expected INDENT in impl block."));
    }

    std::vector<std::unique_ptr<FunctionDecl>> methods {};
    skipNewlines();
    while (!check(TokenKind::Dedent) && !check(TokenKind::Eof)) {
      if (!check(TokenKind::KwFunc)) {
        return std::unexpected(lastError("Expected 'func' inside impl block."));
      }
      auto method = parseFunctionDecl(typeName->lexeme);
      if (!method) {
        return std::unexpected(method.error());
      }
      methods.push_back(std::move(method.value()));
      skipNewlines();
    }
    if (!consume(TokenKind::Dedent, "Expected DEDENT after impl block.")) {
      return std::unexpected(lastError("Expected DEDENT after impl block."));
    }
    return methods;
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
    if (match(TokenKind::KwWhile)) {
      return parseWhileStatement(previous());
    }
    if (match(TokenKind::KwLoop)) {
      return parseLoopStatement(previous());
    }

    if (
      check(TokenKind::Identifier) &&
      checkOffset(1, TokenKind::Dot) &&
      checkOffset(2, TokenKind::Identifier) &&
      checkOffset(3, TokenKind::Equal)
    ) {
      const Token *objectName = advance();
      if (!consume(TokenKind::Dot, "Expected '.' in member assignment.")) {
        return std::unexpected(lastError("Expected '.' in member assignment."));
      }
      const Token *memberName = consume(TokenKind::Identifier, "Expected member name in member assignment.");
      if (!memberName) {
        return std::unexpected(lastError("Expected member name in member assignment."));
      }
      if (!consume(TokenKind::Equal, "Expected '=' in member assignment.")) {
        return std::unexpected(lastError("Expected '=' in member assignment."));
      }
      auto value = parseExpression(0);
      if (!value) {
        return std::unexpected(value.error());
      }
      const Token *end = consume(TokenKind::Newline, "Expected newline after member assignment.");
      if (!end) {
        return std::unexpected(lastError("Expected newline after member assignment."));
      }
      return std::make_unique<MemberAssignStmt>(
        objectName->lexeme,
        memberName->lexeme,
        std::move(value.value()),
        mergeSpan(objectName->span, end->span)
      );
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

    if (check(TokenKind::Identifier) && checkNext(TokenKind::LBracket)) {
      const Token *id = advance();
      if (!consume(TokenKind::LBracket, "Expected '[' in indexed assignment.")) {
        return std::unexpected(lastError("Expected '[' in indexed assignment."));
      }
      auto index = parseExpression(0);
      if (!index) {
        return std::unexpected(index.error());
      }
      if (!consume(TokenKind::RBracket, "Expected ']' after index expression.")) {
        return std::unexpected(lastError("Expected ']' after index expression."));
      }
      if (!consume(TokenKind::Equal, "Expected '=' in indexed assignment.")) {
        return std::unexpected(lastError("Expected '=' in indexed assignment."));
      }
      auto value = parseExpression(0);
      if (!value) {
        return std::unexpected(value.error());
      }
      const Token *end = consume(TokenKind::Newline, "Expected newline after indexed assignment.");
      if (!end) {
        return std::unexpected(lastError("Expected newline after indexed assignment."));
      }
      return std::make_unique<ArrayAssignStmt>(
        id->lexeme,
        std::move(index.value()),
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
    if (!consume(TokenKind::LParen, "Expected '(' after 'if'.")) {
      return std::unexpected(lastError("Expected '(' after 'if'."));
    }
    auto cond = parseExpression(0);
    if (!cond) {
      return std::unexpected(cond.error());
    }
    if (!consume(TokenKind::RParen, "Expected ')' after if condition.")) {
      return std::unexpected(lastError("Expected ')' after if condition."));
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

    std::unique_ptr<BlockStmt> elseBlock {};
    skipNewlines();
    if (match(TokenKind::KwElse)) {
      if (!consume(TokenKind::Colon, "Expected ':' after else.")) {
        return std::unexpected(lastError("Expected ':' after else."));
      }
      if (!consume(TokenKind::Newline, "Expected newline after else header.")) {
        return std::unexpected(lastError("Expected newline after else header."));
      }
      auto parsedElse = parseIndentedBlock();
      if (!parsedElse) {
        return std::unexpected(parsedElse.error());
      }
      elseBlock = std::move(parsedElse.value());
    }

    auto condExpr = std::move(cond.value());
    auto thenBlock = std::move(block.value());
    auto stmtSpan = mergeSpan(ifTok->span, elseBlock ? elseBlock->span : thenBlock->span);
    return std::make_unique<IfStmt>(std::move(condExpr), std::move(thenBlock), std::move(elseBlock), stmtSpan);
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
    return std::make_unique<LoopStmt>(nullptr, std::move(loopBody), stmtSpan);
  }

  auto parseWhileStatement(const Token *whileTok) -> Result<std::unique_ptr<Stmt>, Diagnostic> {
    if (!consume(TokenKind::LParen, "Expected '(' after 'while'.")) {
      return std::unexpected(lastError("Expected '(' after 'while'."));
    }
    auto cond = parseExpression(0);
    if (!cond) {
      return std::unexpected(cond.error());
    }
    if (!consume(TokenKind::RParen, "Expected ')' after while condition.")) {
      return std::unexpected(lastError("Expected ')' after while condition."));
    }
    if (!consume(TokenKind::Colon, "Expected ':' after while condition.")) {
      return std::unexpected(lastError("Expected ':' after while condition."));
    }
    if (!consume(TokenKind::Newline, "Expected newline after while header.")) {
      return std::unexpected(lastError("Expected newline after while header."));
    }
    auto block = parseIndentedBlock();
    if (!block) {
      return std::unexpected(block.error());
    }
    auto loopBody = std::move(block.value());
    auto condExpr = std::move(cond.value());
    auto stmtSpan = mergeSpan(whileTok->span, loopBody->span);
    return std::make_unique<LoopStmt>(std::move(condExpr), std::move(loopBody), stmtSpan);
  }

  auto parseExpression(int minBp) -> Result<std::unique_ptr<Expr>, Diagnostic> {
    auto lhs = parsePrefix();
    if (!lhs) {
      return std::unexpected(lhs.error());
    }

    while (true) {
      if (match(TokenKind::LBracket)) {
        auto index = parseExpression(0);
        if (!index) {
          return std::unexpected(index.error());
        }
        const Token *end = consume(TokenKind::RBracket, "Expected ']' after index expression.");
        if (!end) {
          return std::unexpected(lastError("Expected ']' after index expression."));
        }
        auto arrayExpr = std::move(lhs.value());
        auto exprSpan = mergeSpan(arrayExpr->span, end->span);
        lhs = std::make_unique<IndexExpr>(std::move(arrayExpr), std::move(index.value()), exprSpan);
        continue;
      }

      if (match(TokenKind::Dot)) {
        const Token *member = consume(TokenKind::Identifier, "Expected field name after '.'.");
        if (!member) {
          return std::unexpected(lastError("Expected field name after '.'."));
        }
        auto object = std::move(lhs.value());
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
          const Token *end = consume(TokenKind::RParen, "Expected ')' after method arguments.");
          if (!end) {
            return std::unexpected(lastError("Expected ')' after method arguments."));
          }
          auto exprSpan = mergeSpan(object->span, end->span);
          lhs = std::make_unique<MethodCallExpr>(std::move(object), member->lexeme, std::move(args), exprSpan);
        } else {
          auto exprSpan = mergeSpan(object->span, member->span);
          lhs = std::make_unique<MemberExpr>(std::move(object), member->lexeme, exprSpan);
        }
        continue;
      }

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
      case TokenKind::Float:
        return std::make_unique<LiteralExpr>(LiteralExpr::Kind::Float, tok->lexeme, tok->span);
      case TokenKind::String:
        return std::make_unique<LiteralExpr>(LiteralExpr::Kind::String, tok->lexeme, tok->span);
      case TokenKind::LBracket: {
        std::vector<std::unique_ptr<Expr>> elements {};
        if (!check(TokenKind::RBracket)) {
          while (true) {
            auto element = parseExpression(0);
            if (!element) {
              return std::unexpected(element.error());
            }
            elements.push_back(std::move(element.value()));
            if (!match(TokenKind::Comma)) {
              break;
            }
          }
        }
        const Token *end = consume(TokenKind::RBracket, "Expected ']' after array literal.");
        if (!end) {
          return std::unexpected(lastError("Expected ']' after array literal."));
        }
        return std::make_unique<ArrayLiteralExpr>(std::move(elements), mergeSpan(tok->span, end->span));
      }
      case TokenKind::Minus: {
        auto rhs = parseExpression(31);
        if (!rhs) {
          return std::unexpected(rhs.error());
        }
        std::unique_ptr<Expr> zero {};
        if (rhs.value()->kind == NodeKind::LiteralExpr) {
          const auto &lit = static_cast<const LiteralExpr &>(*rhs.value());
          if (lit.literalKind == LiteralExpr::Kind::Float) {
            zero = std::make_unique<LiteralExpr>(LiteralExpr::Kind::Float, "0.0", tok->span);
          }
        }
        if (!zero) {
          zero = std::make_unique<LiteralExpr>(LiteralExpr::Kind::Int, "0", tok->span);
        }
        auto right = std::move(rhs.value());
        auto exprSpan = mergeSpan(tok->span, right->span);
        return std::make_unique<BinaryExpr>(BinaryOp::Sub, std::move(zero), std::move(right), exprSpan);
      }
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

  auto checkOffset(std::size_t offset, TokenKind kind) const -> bool {
    const auto index = current + offset;
    if (index >= tokens.size()) {
      return false;
    }
    return tokens[index].kind == kind;
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
