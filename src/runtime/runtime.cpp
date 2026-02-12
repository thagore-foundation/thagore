#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <mutex>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>
#include <unordered_map>

namespace {
struct ManagedString {
  char *buffer;
  std::uint32_t refCount;
};

struct TokenBox {
  char *kind;
  char *text;
};

struct ExprToken {
  std::string kind {};
  std::string text {};
};

struct TokenStream {
  std::vector<ExprToken> tokens {};
};

struct AstNode {
  std::string kind {};
  std::string op {};
  std::string value {};
  AstNode *left {nullptr};
  AstNode *right {nullptr};
  AstNode *extra {nullptr};
  std::vector<AstNode *> statements {};
  std::vector<std::string> params {};
  std::vector<AstNode *> args {};
};

struct RuntimeInterpreter {
  std::unordered_map<std::string, AstNode *> functions {};
  std::unordered_map<std::string, int> globals {};
  std::vector<std::unordered_map<std::string, int>> frames {};
};

int g_argc = 0;
char **g_argv = nullptr;

auto managedStrings() -> std::unordered_map<const char *, ManagedString> & {
  static auto *table = new std::unordered_map<const char *, ManagedString> {};
  return *table;
}

auto managedStringsMutex() -> std::mutex & {
  static auto *guard = new std::mutex {};
  return *guard;
}

auto copyCString(const char *text) -> char * {
  if (text == nullptr) {
    text = "";
  }
  const auto len = std::strlen(text);
  auto *out = static_cast<char *>(std::malloc(len + 1));
  if (out == nullptr) {
    return nullptr;
  }
  std::memcpy(out, text, len);
  out[len] = '\0';
  return out;
}

auto tokenizeExprSource(const char *source) -> std::vector<ExprToken> {
  std::vector<ExprToken> out {};
  if (source == nullptr) {
    out.push_back(ExprToken {.kind = "EOF", .text = ""});
    return out;
  }

  const auto *p = source;
  while (*p != '\0') {
    if (std::isspace(static_cast<unsigned char>(*p)) != 0) {
      ++p;
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(*p)) != 0) {
      std::string text {};
      while (*p != '\0' && std::isdigit(static_cast<unsigned char>(*p)) != 0) {
        text.push_back(*p);
        ++p;
      }
      out.push_back(ExprToken {.kind = "INT", .text = std::move(text)});
      continue;
    }
    if (std::isalpha(static_cast<unsigned char>(*p)) != 0 || *p == '_') {
      std::string text {};
      while (*p != '\0' && (std::isalnum(static_cast<unsigned char>(*p)) != 0 || *p == '_')) {
        text.push_back(*p);
        ++p;
      }
      if (text == "let") {
        out.push_back(ExprToken {.kind = "LET", .text = text});
      } else if (text == "if") {
        out.push_back(ExprToken {.kind = "IF", .text = text});
      } else if (text == "else") {
        out.push_back(ExprToken {.kind = "ELSE", .text = text});
      } else if (text == "while") {
        out.push_back(ExprToken {.kind = "WHILE", .text = text});
      } else if (text == "func") {
        out.push_back(ExprToken {.kind = "FUNC", .text = text});
      } else if (text == "return") {
        out.push_back(ExprToken {.kind = "RETURN", .text = text});
      } else {
        out.push_back(ExprToken {.kind = "IDENT", .text = text});
      }
      continue;
    }

    if (*p == '=' && *(p + 1) == '=') {
      out.push_back(ExprToken {.kind = "EQEQ", .text = "=="});
      p += 2;
      continue;
    }
    if (*p == '!' && *(p + 1) == '=') {
      out.push_back(ExprToken {.kind = "BANGEQ", .text = "!="});
      p += 2;
      continue;
    }
    if (*p == '>' && *(p + 1) == '=') {
      out.push_back(ExprToken {.kind = "GTE", .text = ">="});
      p += 2;
      continue;
    }
    if (*p == '<' && *(p + 1) == '=') {
      out.push_back(ExprToken {.kind = "LTE", .text = "<="});
      p += 2;
      continue;
    }

    switch (*p) {
      case '+':
        out.push_back(ExprToken {.kind = "PLUS", .text = "+"});
        ++p;
        break;
      case '-':
        out.push_back(ExprToken {.kind = "MINUS", .text = "-"});
        ++p;
        break;
      case '*':
        out.push_back(ExprToken {.kind = "STAR", .text = "*"});
        ++p;
        break;
      case '/':
        out.push_back(ExprToken {.kind = "SLASH", .text = "/"});
        ++p;
        break;
      case '(':
        out.push_back(ExprToken {.kind = "LPAREN", .text = "("});
        ++p;
        break;
      case ')':
        out.push_back(ExprToken {.kind = "RPAREN", .text = ")"});
        ++p;
        break;
      case '=':
        out.push_back(ExprToken {.kind = "EQUAL", .text = "="});
        ++p;
        break;
      case '>':
        out.push_back(ExprToken {.kind = "GT", .text = ">"});
        ++p;
        break;
      case '<':
        out.push_back(ExprToken {.kind = "LT", .text = "<"});
        ++p;
        break;
      case '{':
        out.push_back(ExprToken {.kind = "LBRACE", .text = "{"});
        ++p;
        break;
      case '}':
        out.push_back(ExprToken {.kind = "RBRACE", .text = "}"});
        ++p;
        break;
      case ';':
        out.push_back(ExprToken {.kind = "SEMI", .text = ";"});
        ++p;
        break;
      case ',':
        out.push_back(ExprToken {.kind = "COMMA", .text = ","});
        ++p;
        break;
      default:
        out.push_back(ExprToken {.kind = "INVALID", .text = std::string(1, *p)});
        ++p;
        break;
    }
  }
  out.push_back(ExprToken {.kind = "EOF", .text = ""});
  return out;
}

class ExprParser {
public:
  explicit ExprParser(const std::vector<ExprToken> &tokens_) : tokens(tokens_) {}

  auto parseExpr() -> AstNode * {
    return parseComparison();
  }

  auto parseStatement() -> AstNode * {
    if (match("SEMI")) {
      return makeLiteralNode("0");
    }
    if (match("LET")) {
      if (current().kind != "IDENT") {
        return makeLiteralNode("0");
      }
      const auto name = current().text;
      ++pos;
      if (!match("EQUAL")) {
        return makeLiteralNode("0");
      }
      auto *expr = parseExpr();
      return makeLetNode(name, expr);
    }
    if (match("FUNC")) {
      if (current().kind != "IDENT") {
        return makeLiteralNode("0");
      }
      const auto fnName = current().text;
      ++pos;
      if (!match("LPAREN")) {
        return makeLiteralNode("0");
      }
      auto params = parseParamList();
      if (!match("RPAREN")) {
        return makeLiteralNode("0");
      }
      auto *body = parseBlock();
      auto *fn = makeFuncNode(fnName, body);
      fn->params = std::move(params);
      return fn;
    }
    if (match("RETURN")) {
      auto *expr = parseExpr();
      return makeReturnNode(expr);
    }
    if (match("IF")) {
      if (!match("LPAREN")) {
        return makeLiteralNode("0");
      }
      auto *condition = parseExpr();
      if (!match("RPAREN")) {
        return makeLiteralNode("0");
      }
      auto *thenBlock = parseBlock();
      AstNode *elseBlock = nullptr;
      if (match("ELSE")) {
        elseBlock = parseBlock();
      }
      return makeIfNode(condition, thenBlock, elseBlock);
    }
    if (match("WHILE")) {
      if (!match("LPAREN")) {
        return makeLiteralNode("0");
      }
      auto *condition = parseExpr();
      if (!match("RPAREN")) {
        return makeLiteralNode("0");
      }
      auto *body = parseBlock();
      return makeWhileNode(condition, body);
    }
    if (current().kind == "LBRACE") {
      return parseBlock();
    }
    if (current().kind == "IDENT" && peekKind(1) == "EQUAL") {
      const auto name = current().text;
      ++pos;
      ++pos;
      auto *expr = parseExpr();
      return makeAssignNode(name, expr);
    }
    return parseExpr();
  }

  auto parseProgram() -> AstNode * {
    auto *block = makeBlockNode();
    while (current().kind != "EOF") {
      if (match("SEMI")) {
        continue;
      }
      auto *stmt = parseStatement();
      block->statements.push_back(stmt);
      if (current().kind == "SEMI") {
        ++pos;
      }
    }
    return block;
  }

private:
  const std::vector<ExprToken> &tokens;
  std::size_t pos {0};

  auto current() const -> const ExprToken & {
    if (pos >= tokens.size()) {
      static const auto *eof = new ExprToken {.kind = "EOF", .text = ""};
      return *eof;
    }
    return tokens[pos];
  }

  auto match(const char *kind) -> bool {
    if (current().kind == kind) {
      ++pos;
      return true;
    }
    return false;
  }

  auto peekKind(std::size_t offset) const -> std::string {
    const auto idx = pos + offset;
    if (idx >= tokens.size()) {
      return "EOF";
    }
    return tokens[idx].kind;
  }

  auto makeLiteralNode(const std::string &value) -> AstNode * {
    auto *node = new AstNode {};
    node->kind = "Literal";
    node->value = value;
    return node;
  }

  auto makeBinaryNode(const std::string &op, AstNode *left, AstNode *right) -> AstNode * {
    auto *node = new AstNode {};
    node->kind = "Binary";
    node->op = op;
    node->left = left;
    node->right = right;
    return node;
  }

  auto makeVariableNode(const std::string &name) -> AstNode * {
    auto *node = new AstNode {};
    node->kind = "Variable";
    node->value = name;
    return node;
  }

  auto makeLetNode(const std::string &name, AstNode *expr) -> AstNode * {
    auto *node = new AstNode {};
    node->kind = "Let";
    node->value = name;
    node->left = expr;
    return node;
  }

  auto makeAssignNode(const std::string &name, AstNode *expr) -> AstNode * {
    auto *node = new AstNode {};
    node->kind = "Assign";
    node->value = name;
    node->left = expr;
    return node;
  }

  auto makeBlockNode() -> AstNode * {
    auto *node = new AstNode {};
    node->kind = "Block";
    return node;
  }

  auto makeIfNode(AstNode *condition, AstNode *thenBlock, AstNode *elseBlock) -> AstNode * {
    auto *node = new AstNode {};
    node->kind = "If";
    node->left = condition;
    node->right = thenBlock;
    node->extra = elseBlock;
    return node;
  }

  auto makeWhileNode(AstNode *condition, AstNode *body) -> AstNode * {
    auto *node = new AstNode {};
    node->kind = "While";
    node->left = condition;
    node->right = body;
    return node;
  }

  auto makeFuncNode(const std::string &name, AstNode *body) -> AstNode * {
    auto *node = new AstNode {};
    node->kind = "FuncDecl";
    node->value = name;
    node->left = body;
    return node;
  }

  auto makeCallNode(const std::string &name, std::vector<AstNode *> args) -> AstNode * {
    auto *node = new AstNode {};
    node->kind = "CallExpr";
    node->value = name;
    node->args = std::move(args);
    return node;
  }

  auto makeReturnNode(AstNode *expr) -> AstNode * {
    auto *node = new AstNode {};
    node->kind = "ReturnStmt";
    node->left = expr;
    return node;
  }

  auto parseParamList() -> std::vector<std::string> {
    std::vector<std::string> params {};
    if (current().kind == "RPAREN") {
      return params;
    }
    while (true) {
      if (current().kind != "IDENT") {
        return {};
      }
      params.push_back(current().text);
      ++pos;
      if (match("COMMA")) {
        continue;
      }
      break;
    }
    return params;
  }

  auto parseArgList() -> std::vector<AstNode *> {
    std::vector<AstNode *> args {};
    if (current().kind == "RPAREN") {
      return args;
    }
    while (true) {
      args.push_back(parseExpr());
      if (match("COMMA")) {
        continue;
      }
      break;
    }
    return args;
  }

  auto parseBlock() -> AstNode * {
    if (!match("LBRACE")) {
      return makeLiteralNode("0");
    }

    auto *block = makeBlockNode();
    while (current().kind != "RBRACE" && current().kind != "EOF") {
      if (match("SEMI")) {
        continue;
      }
      auto *stmt = parseStatement();
      block->statements.push_back(stmt);
      if (current().kind == "SEMI") {
        ++pos;
      }
    }
    (void)match("RBRACE");
    return block;
  }

  auto parseFactor() -> AstNode * {
    if (match("MINUS")) {
      auto *right = parseFactor();
      return makeBinaryNode("-", makeLiteralNode("0"), right);
    }
    if (match("LPAREN")) {
      auto *inner = parseExpr();
      (void)match("RPAREN");
      return inner;
    }
    if (current().kind == "INT") {
      auto value = current().text;
      ++pos;
      return makeLiteralNode(value);
    }
    if (current().kind == "IDENT") {
      auto name = current().text;
      ++pos;
      if (match("LPAREN")) {
        auto args = parseArgList();
        (void)match("RPAREN");
        return makeCallNode(name, std::move(args));
      }
      return makeVariableNode(name);
    }
    return makeLiteralNode("0");
  }

  auto parseMulDiv() -> AstNode * {
    auto *left = parseFactor();
    while (true) {
      if (match("STAR")) {
        auto *right = parseFactor();
        left = makeBinaryNode("*", left, right);
        continue;
      }
      if (match("SLASH")) {
        auto *right = parseFactor();
        left = makeBinaryNode("/", left, right);
        continue;
      }
      break;
    }
    return left;
  }

  auto parseAddSub() -> AstNode * {
    auto *left = parseMulDiv();
    while (true) {
      if (match("PLUS")) {
        auto *right = parseMulDiv();
        left = makeBinaryNode("+", left, right);
        continue;
      }
      if (match("MINUS")) {
        auto *right = parseMulDiv();
        left = makeBinaryNode("-", left, right);
        continue;
      }
      break;
    }
    return left;
  }

  auto parseComparison() -> AstNode * {
    auto *left = parseAddSub();
    while (true) {
      if (match("EQEQ")) {
        auto *right = parseAddSub();
        left = makeBinaryNode("==", left, right);
        continue;
      }
      if (match("BANGEQ")) {
        auto *right = parseAddSub();
        left = makeBinaryNode("!=", left, right);
        continue;
      }
      if (match("GTE")) {
        auto *right = parseAddSub();
        left = makeBinaryNode(">=", left, right);
        continue;
      }
      if (match("LTE")) {
        auto *right = parseAddSub();
        left = makeBinaryNode("<=", left, right);
        continue;
      }
      if (match("GT")) {
        auto *right = parseAddSub();
        left = makeBinaryNode(">", left, right);
        continue;
      }
      if (match("LT")) {
        auto *right = parseAddSub();
        left = makeBinaryNode("<", left, right);
        continue;
      }
      break;
    }
    return left;
  }
};

void appendAstLine(std::string &out, int indent, const std::string &text) {
  out.append(static_cast<std::size_t>(indent), ' ');
  out.append(text);
  out.push_back('\n');
}

void appendAstNode(std::string &out, AstNode *node, int indent, std::string_view label) {
  if (node == nullptr) {
    appendAstLine(out, indent, std::string(label) + ": <null>");
    return;
  }

  if (node->kind == "Literal") {
    appendAstLine(out, indent, std::string(label) + ": Literal(" + node->value + ")");
    return;
  }
  if (node->kind == "Variable") {
    appendAstLine(out, indent, std::string(label) + ": Variable(" + node->value + ")");
    return;
  }
  if (node->kind == "Let") {
    appendAstLine(out, indent, std::string(label) + ": Let(" + node->value + ")");
    appendAstNode(out, node->left, indent + 2, "Value");
    return;
  }
  if (node->kind == "Assign") {
    appendAstLine(out, indent, std::string(label) + ": Assign(" + node->value + ")");
    appendAstNode(out, node->left, indent + 2, "Value");
    return;
  }
  if (node->kind == "Block") {
    appendAstLine(out, indent, std::string(label) + ": Block");
    for (std::size_t i = 0; i < node->statements.size(); ++i) {
      appendAstNode(out, node->statements[i], indent + 2, "Stmt" + std::to_string(i));
    }
    return;
  }
  if (node->kind == "If") {
    appendAstLine(out, indent, std::string(label) + ": If");
    appendAstNode(out, node->left, indent + 2, "Condition");
    appendAstNode(out, node->right, indent + 2, "Then");
    if (node->extra != nullptr) {
      appendAstNode(out, node->extra, indent + 2, "Else");
    }
    return;
  }
  if (node->kind == "While") {
    appendAstLine(out, indent, std::string(label) + ": While");
    appendAstNode(out, node->left, indent + 2, "Condition");
    appendAstNode(out, node->right, indent + 2, "Body");
    return;
  }
  if (node->kind == "FuncDecl") {
    appendAstLine(out, indent, std::string(label) + ": FuncDecl(" + node->value + ")");
    for (std::size_t i = 0; i < node->params.size(); ++i) {
      appendAstLine(out, indent + 2, "Param" + std::to_string(i) + ": " + node->params[i]);
    }
    appendAstNode(out, node->left, indent + 2, "Body");
    return;
  }
  if (node->kind == "CallExpr") {
    appendAstLine(out, indent, std::string(label) + ": CallExpr(" + node->value + ")");
    for (std::size_t i = 0; i < node->args.size(); ++i) {
      appendAstNode(out, node->args[i], indent + 2, "Arg" + std::to_string(i));
    }
    return;
  }
  if (node->kind == "ReturnStmt") {
    appendAstLine(out, indent, std::string(label) + ": Return");
    appendAstNode(out, node->left, indent + 2, "Value");
    return;
  }

  appendAstLine(out, indent, std::string(label) + ": Binary(" + node->op + ")");
  appendAstNode(out, node->left, indent + 2, "Left");
  appendAstNode(out, node->right, indent + 2, "Right");
}

auto buildAstDebugString(AstNode *root) -> std::string {
  if (root == nullptr) {
    return "<null>\n";
  }
  if (root->kind == "Literal") {
    return "Literal(" + root->value + ")\n";
  }
  if (root->kind == "Variable") {
    return "Variable(" + root->value + ")\n";
  }
  if (root->kind == "Let") {
    std::string out {};
    appendAstLine(out, 0, "Let(" + root->value + ")");
    appendAstNode(out, root->left, 2, "Value");
    return out;
  }
  if (root->kind == "Assign") {
    std::string out {};
    appendAstLine(out, 0, "Assign(" + root->value + ")");
    appendAstNode(out, root->left, 2, "Value");
    return out;
  }
  if (root->kind == "Block") {
    std::string out {};
    appendAstLine(out, 0, "Block");
    for (std::size_t i = 0; i < root->statements.size(); ++i) {
      appendAstNode(out, root->statements[i], 2, "Stmt" + std::to_string(i));
    }
    return out;
  }
  if (root->kind == "If") {
    std::string out {};
    appendAstLine(out, 0, "If");
    appendAstNode(out, root->left, 2, "Condition");
    appendAstNode(out, root->right, 2, "Then");
    if (root->extra != nullptr) {
      appendAstNode(out, root->extra, 2, "Else");
    }
    return out;
  }
  if (root->kind == "While") {
    std::string out {};
    appendAstLine(out, 0, "While");
    appendAstNode(out, root->left, 2, "Condition");
    appendAstNode(out, root->right, 2, "Body");
    return out;
  }
  if (root->kind == "FuncDecl") {
    std::string out {};
    appendAstLine(out, 0, "FuncDecl(" + root->value + ")");
    for (std::size_t i = 0; i < root->params.size(); ++i) {
      appendAstLine(out, 2, "Param" + std::to_string(i) + ": " + root->params[i]);
    }
    appendAstNode(out, root->left, 2, "Body");
    return out;
  }
  if (root->kind == "CallExpr") {
    std::string out {};
    appendAstLine(out, 0, "CallExpr(" + root->value + ")");
    for (std::size_t i = 0; i < root->args.size(); ++i) {
      appendAstNode(out, root->args[i], 2, "Arg" + std::to_string(i));
    }
    return out;
  }
  if (root->kind == "ReturnStmt") {
    std::string out {};
    appendAstLine(out, 0, "Return");
    appendAstNode(out, root->left, 2, "Value");
    return out;
  }
  std::string out {};
  appendAstLine(out, 0, "Binary(" + root->op + ")");
  appendAstNode(out, root->left, 2, "Left");
  appendAstNode(out, root->right, 2, "Right");
  return out;
}

struct ExecResult {
  int value {0};
  bool hasReturn {false};
};

auto resolveVariable(RuntimeInterpreter *interp, const std::string &name) -> int {
  if (interp == nullptr) {
    return 0;
  }
  for (auto it = interp->frames.rbegin(); it != interp->frames.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) {
      return found->second;
    }
  }
  if (auto global = interp->globals.find(name); global != interp->globals.end()) {
    return global->second;
  }
  return 0;
}

void assignVariable(RuntimeInterpreter *interp, const std::string &name, int value, bool declareLocal) {
  if (interp == nullptr) {
    return;
  }
  if (declareLocal) {
    if (!interp->frames.empty()) {
      interp->frames.back()[name] = value;
    } else {
      interp->globals[name] = value;
    }
    return;
  }

  for (auto it = interp->frames.rbegin(); it != interp->frames.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) {
      found->second = value;
      return;
    }
  }
  if (auto global = interp->globals.find(name); global != interp->globals.end()) {
    global->second = value;
    return;
  }
  if (!interp->frames.empty()) {
    interp->frames.back()[name] = value;
  } else {
    interp->globals[name] = value;
  }
}

auto execStmtWithEnv(AstNode *node, RuntimeInterpreter *interp) -> ExecResult;
auto evalExprWithEnv(AstNode *node, RuntimeInterpreter *interp) -> int;

auto evalCallExpr(AstNode *callNode, RuntimeInterpreter *interp) -> int {
  if (callNode == nullptr || interp == nullptr) {
    return 0;
  }
  auto fnIt = interp->functions.find(callNode->value);
  if (fnIt == interp->functions.end() || fnIt->second == nullptr) {
    return 0;
  }
  auto *fn = fnIt->second;

  std::vector<int> argValues {};
  argValues.reserve(callNode->args.size());
  for (auto *arg : callNode->args) {
    argValues.push_back(evalExprWithEnv(arg, interp));
  }

  std::unordered_map<std::string, int> frame {};
  for (std::size_t i = 0; i < fn->params.size(); ++i) {
    const int value = i < argValues.size() ? argValues[i] : 0;
    frame[fn->params[i]] = value;
  }

  interp->frames.push_back(std::move(frame));
  ExecResult result = execStmtWithEnv(fn->left, interp);
  interp->frames.pop_back();
  return result.value;
}

auto evalExprWithEnv(AstNode *node, RuntimeInterpreter *interp) -> int {
  if (node == nullptr) {
    return 0;
  }
  if (node->kind == "Literal") {
    return std::atoi(node->value.c_str());
  }
  if (node->kind == "Variable") {
    return resolveVariable(interp, node->value);
  }
  if (node->kind == "CallExpr") {
    return evalCallExpr(node, interp);
  }
  if (node->kind == "Binary") {
    const int left = evalExprWithEnv(node->left, interp);
    const int right = evalExprWithEnv(node->right, interp);
    if (node->op == "+") return left + right;
    if (node->op == "-") return left - right;
    if (node->op == "*") return left * right;
    if (node->op == "/") {
      if (right == 0) return 0;
      return left / right;
    }
    if (node->op == "==") return left == right ? 1 : 0;
    if (node->op == "!=") return left != right ? 1 : 0;
    if (node->op == ">") return left > right ? 1 : 0;
    if (node->op == "<") return left < right ? 1 : 0;
    if (node->op == ">=") return left >= right ? 1 : 0;
    if (node->op == "<=") return left <= right ? 1 : 0;
  }
  return 0;
}

auto execStmtWithEnv(AstNode *node, RuntimeInterpreter *interp) -> ExecResult {
  if (node == nullptr) {
    return {};
  }
  if (node->kind == "Let") {
    const int value = evalExprWithEnv(node->left, interp);
    assignVariable(interp, node->value, value, true);
    return ExecResult {.value = value, .hasReturn = false};
  }
  if (node->kind == "Assign") {
    const int value = evalExprWithEnv(node->left, interp);
    assignVariable(interp, node->value, value, false);
    return ExecResult {.value = value, .hasReturn = false};
  }
  if (node->kind == "FuncDecl") {
    if (interp != nullptr) {
      interp->functions[node->value] = node;
    }
    return ExecResult {.value = 0, .hasReturn = false};
  }
  if (node->kind == "ReturnStmt") {
    const int value = evalExprWithEnv(node->left, interp);
    return ExecResult {.value = value, .hasReturn = true};
  }
  if (node->kind == "Block") {
    ExecResult last {};
    for (auto *stmt : node->statements) {
      last = execStmtWithEnv(stmt, interp);
      if (last.hasReturn) {
        return last;
      }
    }
    return last;
  }
  if (node->kind == "If") {
    const int cond = evalExprWithEnv(node->left, interp);
    if (cond != 0) {
      return execStmtWithEnv(node->right, interp);
    }
    if (node->extra != nullptr) {
      return execStmtWithEnv(node->extra, interp);
    }
    return {};
  }
  if (node->kind == "While") {
    ExecResult last {};
    while (evalExprWithEnv(node->left, interp) != 0) {
      last = execStmtWithEnv(node->right, interp);
      if (last.hasReturn) {
        return last;
      }
    }
    return last;
  }
  return ExecResult {.value = evalExprWithEnv(node, interp), .hasReturn = false};
}

} // namespace

extern "C" {

void __thg_init_env(int c, char **v) {
  g_argc = c;
  g_argv = v;
}

int __thg_arg_count() {
  return g_argc;
}

const char *__thg_arg_get(int index) {
  if (g_argv == nullptr || index < 0 || index >= g_argc) {
    return nullptr;
  }
  return g_argv[index];
}

int __thg_cstr_len(const char *s) {
  if (s == nullptr) {
    return 0;
  }
  return static_cast<int>(std::strlen(s));
}

int __thg_str_len(const char *s) {
  return __thg_cstr_len(s);
}

char *__thg_fs_read_text(const char *path) {
  if (path == nullptr || *path == '\0') {
    return copyCString("");
  }

  std::FILE *file = std::fopen(path, "rb");
  if (file == nullptr) {
    return copyCString("");
  }

  if (std::fseek(file, 0, SEEK_END) != 0) {
    std::fclose(file);
    return copyCString("");
  }

  const long fileSize = std::ftell(file);
  if (fileSize < 0) {
    std::fclose(file);
    return copyCString("");
  }

  if (std::fseek(file, 0, SEEK_SET) != 0) {
    std::fclose(file);
    return copyCString("");
  }

  auto *buffer = static_cast<char *>(std::malloc(static_cast<std::size_t>(fileSize) + 1));
  if (buffer == nullptr) {
    std::fclose(file);
    return copyCString("");
  }

  const std::size_t bytesRead = std::fread(buffer, 1, static_cast<std::size_t>(fileSize), file);
  std::fclose(file);

  buffer[bytesRead] = '\0';
  return buffer;
}

char *__thg_str_substr(const char *s, int start, int len) {
  if (s == nullptr || len <= 0) {
    return copyCString("");
  }

  const int total = static_cast<int>(std::strlen(s));
  if (start < 0 || start >= total) {
    return copyCString("");
  }

  if (len < 0) {
    return copyCString("");
  }

  const int maxLen = total - start;
  const int actualLen = len > maxLen ? maxLen : len;
  auto *out = static_cast<char *>(std::malloc(static_cast<std::size_t>(actualLen) + 1));
  if (out == nullptr) {
    return nullptr;
  }
  std::memcpy(out, s + start, static_cast<std::size_t>(actualLen));
  out[actualLen] = '\0';
  return out;
}

void __thg_retain(void *ptr) {
  if (ptr == nullptr) {
    return;
  }
  const auto *key = static_cast<const char *>(ptr);
  std::lock_guard lock {managedStringsMutex()};
  auto &table = managedStrings();
  auto it = table.find(key);
  if (it != table.end()) {
    ++it->second.refCount;
  }
}

void __thg_release(void *ptr) {
  if (ptr == nullptr) {
    return;
  }
  const auto *key = static_cast<const char *>(ptr);
  std::lock_guard lock {managedStringsMutex()};
  auto &table = managedStrings();
  auto it = table.find(key);
  if (it == table.end()) {
    return;
  }
  if (it->second.refCount == 0) {
    std::free(it->second.buffer);
    table.erase(it);
    return;
  }
  --it->second.refCount;
  if (it->second.refCount == 0) {
    std::free(it->second.buffer);
    table.erase(it);
  }
}

void __thg_print_i32(std::int32_t value) {
  std::printf("%d\n", value);
}

void __thg_print_f32(float value) {
  std::printf("%f\n", static_cast<double>(value));
}

void __thg_print_str(const char *ptr, std::int32_t len) {
  if (ptr == nullptr || len <= 0) {
    std::printf("\n");
    return;
  }
  std::fwrite(ptr, sizeof(char), static_cast<std::size_t>(len), stdout);
  std::fwrite("\n", sizeof(char), 1, stdout);
}

void __thg_print_ptr(const char *ptr) {
  if (ptr == nullptr) {
    std::printf("(null)\n");
    return;
  }
  std::printf("%s\n", ptr);
}

void *__thg_mem_alloc(int size) {
  if (size <= 0) {
    return nullptr;
  }
  return std::malloc(static_cast<std::size_t>(size));
}

void *__thg_mem_realloc(void *ptr, int new_size) {
  if (new_size <= 0) {
    std::free(ptr);
    return nullptr;
  }
  return std::realloc(ptr, static_cast<std::size_t>(new_size));
}

void __thg_mem_free(void *ptr) {
  std::free(ptr);
}

void __thg_ptr_set(void *base, int index, void *value) {
  if (base == nullptr || index < 0) {
    return;
  }
  static_cast<void **>(base)[index] = value;
}

void *__thg_ptr_get(void *base, int index) {
  if (base == nullptr || index < 0) {
    return nullptr;
  }
  return static_cast<void **>(base)[index];
}

void *__thg_ptr_null() {
  return nullptr;
}

char *__thg_str_add(char *s1, char *s2) {
  if (s1 == nullptr) {
    s1 = const_cast<char *>("");
  }
  if (s2 == nullptr) {
    s2 = const_cast<char *>("");
  }

  const auto len1 = std::strlen(s1);
  const auto len2 = std::strlen(s2);
  auto *res = static_cast<char *>(std::malloc(len1 + len2 + 1));
  if (res == nullptr) {
    return nullptr;
  }

  std::memcpy(res, s1, len1);
  std::memcpy(res + len1, s2, len2);
  res[len1 + len2] = '\0';
  return res;
}

char *__thg_str_dup(char *s) {
  if (s == nullptr) {
    s = const_cast<char *>("");
  }

  const auto len = std::strlen(s);
  auto *copy = static_cast<char *>(std::malloc(len + 1));
  if (copy == nullptr) {
    return nullptr;
  }
  std::memcpy(copy, s, len);
  copy[len] = '\0';
  return copy;
}

void __thg_str_free(char *s) {
  if (s != nullptr) {
    std::free(s);
  }
}

int __thg_str_eq(char *s1, char *s2) {
  if (s1 == s2) {
    return 1;
  }
  if (s1 == nullptr || s2 == nullptr) {
    return 0;
  }
  return std::strcmp(s1, s2) == 0 ? 1 : 0;
}

int __thg_str_hash(char *s) {
  if (s == nullptr) {
    return 0;
  }
  unsigned long hash = 5381;
  int ch = 0;
  while ((ch = *s++) != 0) {
    hash = ((hash << 5) + hash) + static_cast<unsigned long>(ch);
  }
  return static_cast<int>(hash & 0x7FFFFFFFul);
}

int __thg_str_to_i32(char *s) {
  if (s == nullptr) {
    return 0;
  }
  return std::atoi(s);
}

void *__thg_token_new(const char *kind, const char *text) {
  auto *token = static_cast<TokenBox *>(std::malloc(sizeof(TokenBox)));
  if (token == nullptr) {
    return nullptr;
  }
  token->kind = copyCString(kind);
  token->text = copyCString(text);
  return token;
}

const char *__thg_token_kind(void *token) {
  if (token == nullptr) {
    return "";
  }
  return static_cast<TokenBox *>(token)->kind;
}

const char *__thg_token_text(void *token) {
  if (token == nullptr) {
    return "";
  }
  return static_cast<TokenBox *>(token)->text;
}

const char *__thg_str_concat(const char *leftPtr, std::int32_t leftLen, const char *rightPtr, std::int32_t rightLen, std::int32_t *outLen) {
  if (outLen == nullptr || leftLen < 0 || rightLen < 0) {
    return nullptr;
  }
  const std::int64_t totalLen64 = static_cast<std::int64_t>(leftLen) + static_cast<std::int64_t>(rightLen);
  if (totalLen64 > static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())) {
    return nullptr;
  }

  const auto totalLen = static_cast<std::int32_t>(totalLen64);
  auto *buffer = static_cast<char *>(std::malloc(static_cast<std::size_t>(totalLen) + 1));
  if (buffer == nullptr) {
    return nullptr;
  }

  if (leftPtr != nullptr && leftLen > 0) {
    std::memcpy(buffer, leftPtr, static_cast<std::size_t>(leftLen));
  }
  if (rightPtr != nullptr && rightLen > 0) {
    std::memcpy(buffer + leftLen, rightPtr, static_cast<std::size_t>(rightLen));
  }
  buffer[totalLen] = '\0';

  {
    std::lock_guard lock {managedStringsMutex()};
    managedStrings().emplace(buffer, ManagedString {.buffer = buffer, .refCount = 0});
  }

  *outLen = totalLen;
  return buffer;
}

void *__thg_lex_tokenize(const char *source) {
  auto *stream = new TokenStream {};
  stream->tokens = tokenizeExprSource(source);
  return stream;
}

int __thg_tok_count(void *streamPtr) {
  if (streamPtr == nullptr) {
    return 0;
  }
  const auto *stream = static_cast<TokenStream *>(streamPtr);
  return static_cast<int>(stream->tokens.size());
}

const char *__thg_tok_kind(void *streamPtr, int index) {
  if (streamPtr == nullptr || index < 0) {
    return "";
  }
  const auto *stream = static_cast<TokenStream *>(streamPtr);
  if (static_cast<std::size_t>(index) >= stream->tokens.size()) {
    return "EOF";
  }
  return stream->tokens[static_cast<std::size_t>(index)].kind.c_str();
}

const char *__thg_tok_text(void *streamPtr, int index) {
  if (streamPtr == nullptr || index < 0) {
    return "";
  }
  const auto *stream = static_cast<TokenStream *>(streamPtr);
  if (static_cast<std::size_t>(index) >= stream->tokens.size()) {
    return "";
  }
  return stream->tokens[static_cast<std::size_t>(index)].text.c_str();
}

int __thg_tok_tag(void *streamPtr, int index) {
  if (streamPtr == nullptr || index < 0) {
    return 0;
  }
  const auto *stream = static_cast<TokenStream *>(streamPtr);
  if (static_cast<std::size_t>(index) >= stream->tokens.size()) {
    return 0;
  }
  const auto &kind = stream->tokens[static_cast<std::size_t>(index)].kind;
  if (kind == "EOF") return 0;
  if (kind == "INT") return 1;
  if (kind == "PLUS") return 2;
  if (kind == "MINUS") return 3;
  if (kind == "STAR") return 4;
  if (kind == "SLASH") return 5;
  if (kind == "LPAREN") return 6;
  if (kind == "RPAREN") return 7;
  if (kind == "INVALID") return 8;
  if (kind == "IDENT") return 9;
  if (kind == "LET") return 10;
  if (kind == "EQUAL") return 11;
  if (kind == "IF") return 12;
  if (kind == "ELSE") return 13;
  if (kind == "WHILE") return 14;
  if (kind == "LBRACE") return 15;
  if (kind == "RBRACE") return 16;
  if (kind == "SEMI") return 17;
  if (kind == "EQEQ") return 18;
  if (kind == "BANGEQ") return 19;
  if (kind == "GT") return 20;
  if (kind == "LT") return 21;
  if (kind == "GTE") return 22;
  if (kind == "LTE") return 23;
  if (kind == "COMMA") return 24;
  if (kind == "FUNC") return 25;
  if (kind == "RETURN") return 26;
  return 8;
}

void *__thg_ast_new_literal(const char *value) {
  auto *node = new AstNode {};
  node->kind = "Literal";
  node->value = value == nullptr ? "" : value;
  return node;
}

void *__thg_ast_new_binary(const char *op, void *left, void *right) {
  auto *node = new AstNode {};
  node->kind = "Binary";
  node->op = op == nullptr ? "" : op;
  node->left = static_cast<AstNode *>(left);
  node->right = static_cast<AstNode *>(right);
  return node;
}

void *__thg_ast_new_var(const char *name) {
  auto *node = new AstNode {};
  node->kind = "Variable";
  node->value = name == nullptr ? "" : name;
  return node;
}

void *__thg_ast_new_let(const char *name, void *expr) {
  auto *node = new AstNode {};
  node->kind = "Let";
  node->value = name == nullptr ? "" : name;
  node->left = static_cast<AstNode *>(expr);
  return node;
}

void *__thg_ast_new_assign(const char *name, void *expr) {
  auto *node = new AstNode {};
  node->kind = "Assign";
  node->value = name == nullptr ? "" : name;
  node->left = static_cast<AstNode *>(expr);
  return node;
}

void *__thg_ast_new_block() {
  auto *node = new AstNode {};
  node->kind = "Block";
  return node;
}

int __thg_ast_block_push(void *blockPtr, void *stmtPtr) {
  auto *block = static_cast<AstNode *>(blockPtr);
  if (block == nullptr || block->kind != "Block") {
    return 0;
  }
  block->statements.push_back(static_cast<AstNode *>(stmtPtr));
  return 0;
}

void *__thg_ast_new_if(void *condition, void *thenBlock, void *elseBlock) {
  auto *node = new AstNode {};
  node->kind = "If";
  node->left = static_cast<AstNode *>(condition);
  node->right = static_cast<AstNode *>(thenBlock);
  node->extra = static_cast<AstNode *>(elseBlock);
  return node;
}

void *__thg_ast_new_while(void *condition, void *body) {
  auto *node = new AstNode {};
  node->kind = "While";
  node->left = static_cast<AstNode *>(condition);
  node->right = static_cast<AstNode *>(body);
  return node;
}

void *__thg_ast_new_func(const char *name, void *body) {
  auto *node = new AstNode {};
  node->kind = "FuncDecl";
  node->value = name == nullptr ? "" : name;
  node->left = static_cast<AstNode *>(body);
  return node;
}

int __thg_ast_func_add_param(void *funcPtr, const char *paramName) {
  auto *node = static_cast<AstNode *>(funcPtr);
  if (node == nullptr || node->kind != "FuncDecl") {
    return 0;
  }
  node->params.push_back(paramName == nullptr ? "" : paramName);
  return 0;
}

void *__thg_ast_new_call(const char *name) {
  auto *node = new AstNode {};
  node->kind = "CallExpr";
  node->value = name == nullptr ? "" : name;
  return node;
}

int __thg_ast_call_add_arg(void *callPtr, void *argPtr) {
  auto *node = static_cast<AstNode *>(callPtr);
  if (node == nullptr || node->kind != "CallExpr") {
    return 0;
  }
  node->args.push_back(static_cast<AstNode *>(argPtr));
  return 0;
}

void *__thg_ast_new_return(void *expr) {
  auto *node = new AstNode {};
  node->kind = "ReturnStmt";
  node->left = static_cast<AstNode *>(expr);
  return node;
}

const char *__thg_ast_kind(void *nodePtr) {
  if (nodePtr == nullptr) {
    return "";
  }
  return static_cast<AstNode *>(nodePtr)->kind.c_str();
}

int __thg_ast_kind_tag(void *nodePtr) {
  if (nodePtr == nullptr) {
    return 0;
  }
  const auto &kind = static_cast<AstNode *>(nodePtr)->kind;
  if (kind == "Literal") {
    return 1;
  }
  if (kind == "Binary") {
    return 2;
  }
  if (kind == "Variable") {
    return 3;
  }
  if (kind == "Let") {
    return 4;
  }
  if (kind == "Assign") {
    return 5;
  }
  if (kind == "Block") {
    return 6;
  }
  if (kind == "If") {
    return 7;
  }
  if (kind == "While") {
    return 8;
  }
  if (kind == "FuncDecl") {
    return 9;
  }
  if (kind == "CallExpr") {
    return 10;
  }
  if (kind == "ReturnStmt") {
    return 11;
  }
  return 0;
}

const char *__thg_ast_op(void *nodePtr) {
  if (nodePtr == nullptr) {
    return "";
  }
  return static_cast<AstNode *>(nodePtr)->op.c_str();
}

int __thg_ast_op_tag(void *nodePtr) {
  if (nodePtr == nullptr) {
    return 0;
  }
  const auto &op = static_cast<AstNode *>(nodePtr)->op;
  if (op == "+") {
    return 1;
  }
  if (op == "-") {
    return 2;
  }
  if (op == "*") {
    return 3;
  }
  if (op == "/") {
    return 4;
  }
  if (op == "==") {
    return 5;
  }
  if (op == "!=") {
    return 6;
  }
  if (op == ">") {
    return 7;
  }
  if (op == "<") {
    return 8;
  }
  if (op == ">=") {
    return 9;
  }
  if (op == "<=") {
    return 10;
  }
  return 0;
}

const char *__thg_ast_value(void *nodePtr) {
  if (nodePtr == nullptr) {
    return "";
  }
  return static_cast<AstNode *>(nodePtr)->value.c_str();
}

const char *__thg_ast_name(void *nodePtr) {
  if (nodePtr == nullptr) {
    return "";
  }
  return static_cast<AstNode *>(nodePtr)->value.c_str();
}

void *__thg_ast_left(void *nodePtr) {
  if (nodePtr == nullptr) {
    return nullptr;
  }
  return static_cast<AstNode *>(nodePtr)->left;
}

void *__thg_ast_right(void *nodePtr) {
  if (nodePtr == nullptr) {
    return nullptr;
  }
  return static_cast<AstNode *>(nodePtr)->right;
}

void *__thg_ast_condition(void *nodePtr) {
  if (nodePtr == nullptr) {
    return nullptr;
  }
  return static_cast<AstNode *>(nodePtr)->left;
}

void *__thg_ast_then(void *nodePtr) {
  if (nodePtr == nullptr) {
    return nullptr;
  }
  return static_cast<AstNode *>(nodePtr)->right;
}

void *__thg_ast_else(void *nodePtr) {
  if (nodePtr == nullptr) {
    return nullptr;
  }
  return static_cast<AstNode *>(nodePtr)->extra;
}

void *__thg_ast_body(void *nodePtr) {
  if (nodePtr == nullptr) {
    return nullptr;
  }
  return static_cast<AstNode *>(nodePtr)->right;
}

void *__thg_ast_func_body(void *nodePtr) {
  if (nodePtr == nullptr) {
    return nullptr;
  }
  auto *node = static_cast<AstNode *>(nodePtr);
  if (node->kind != "FuncDecl") {
    return nullptr;
  }
  return node->left;
}

int __thg_ast_func_param_count(void *nodePtr) {
  if (nodePtr == nullptr) {
    return 0;
  }
  auto *node = static_cast<AstNode *>(nodePtr);
  if (node->kind != "FuncDecl") {
    return 0;
  }
  return static_cast<int>(node->params.size());
}

const char *__thg_ast_func_param_at(void *nodePtr, int index) {
  if (nodePtr == nullptr || index < 0) {
    return "";
  }
  auto *node = static_cast<AstNode *>(nodePtr);
  if (node->kind != "FuncDecl") {
    return "";
  }
  const auto idx = static_cast<std::size_t>(index);
  if (idx >= node->params.size()) {
    return "";
  }
  return node->params[idx].c_str();
}

int __thg_ast_call_arg_count(void *nodePtr) {
  if (nodePtr == nullptr) {
    return 0;
  }
  auto *node = static_cast<AstNode *>(nodePtr);
  if (node->kind != "CallExpr") {
    return 0;
  }
  return static_cast<int>(node->args.size());
}

void *__thg_ast_call_arg_at(void *nodePtr, int index) {
  if (nodePtr == nullptr || index < 0) {
    return nullptr;
  }
  auto *node = static_cast<AstNode *>(nodePtr);
  if (node->kind != "CallExpr") {
    return nullptr;
  }
  const auto idx = static_cast<std::size_t>(index);
  if (idx >= node->args.size()) {
    return nullptr;
  }
  return node->args[idx];
}

int __thg_ast_block_count(void *nodePtr) {
  if (nodePtr == nullptr) {
    return 0;
  }
  auto *node = static_cast<AstNode *>(nodePtr);
  if (node->kind != "Block") {
    return 0;
  }
  return static_cast<int>(node->statements.size());
}

void *__thg_ast_block_get_stmt(void *nodePtr, int index) {
  if (nodePtr == nullptr || index < 0) {
    return nullptr;
  }
  auto *node = static_cast<AstNode *>(nodePtr);
  if (node->kind != "Block") {
    return nullptr;
  }
  const auto idx = static_cast<std::size_t>(index);
  if (idx >= node->statements.size()) {
    return nullptr;
  }
  return node->statements[idx];
}

char *__thg_ast_debug(void *nodePtr) {
  auto text = buildAstDebugString(static_cast<AstNode *>(nodePtr));
  return copyCString(text.c_str());
}

void *__thg_parse_expr_from_tokens(void *streamPtr) {
  if (streamPtr == nullptr) {
    return nullptr;
  }
  const auto *stream = static_cast<TokenStream *>(streamPtr);
  ExprParser parser {stream->tokens};
  return parser.parseExpr();
}

void *__thg_parse_stmt_from_source(const char *source) {
  auto tokens = tokenizeExprSource(source);
  ExprParser parser {tokens};
  return parser.parseStatement();
}

void *__thg_parse_program_from_source(const char *source) {
  auto tokens = tokenizeExprSource(source);
  ExprParser parser {tokens};
  return parser.parseProgram();
}

int __thg_eval_expr(void *nodePtr) {
  return evalExprWithEnv(static_cast<AstNode *>(nodePtr), nullptr);
}

void *__thg_interp_new() {
  return new RuntimeInterpreter {};
}

int __thg_interp_eval_expr(void *interpPtr, void *nodePtr) {
  auto *interp = static_cast<RuntimeInterpreter *>(interpPtr);
  return evalExprWithEnv(static_cast<AstNode *>(nodePtr), interp);
}

int __thg_interp_exec_stmt(void *interpPtr, void *nodePtr) {
  auto *interp = static_cast<RuntimeInterpreter *>(interpPtr);
  return execStmtWithEnv(static_cast<AstNode *>(nodePtr), interp).value;
}

}
