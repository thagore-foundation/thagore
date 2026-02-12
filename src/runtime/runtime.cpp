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
    return parseAddSub();
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

  auto parseFactor() -> AstNode * {
    if (match("LPAREN")) {
      auto *inner = parseAddSub();
      (void)match("RPAREN");
      return inner;
    }
    if (current().kind == "INT") {
      auto value = current().text;
      ++pos;
      return makeLiteralNode(value);
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
  std::string out {};
  appendAstLine(out, 0, "Binary(" + root->op + ")");
  appendAstNode(out, root->left, 2, "Left");
  appendAstNode(out, root->right, 2, "Right");
  return out;
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

const char *__thg_ast_kind(void *nodePtr) {
  if (nodePtr == nullptr) {
    return "";
  }
  return static_cast<AstNode *>(nodePtr)->kind.c_str();
}

const char *__thg_ast_op(void *nodePtr) {
  if (nodePtr == nullptr) {
    return "";
  }
  return static_cast<AstNode *>(nodePtr)->op.c_str();
}

const char *__thg_ast_value(void *nodePtr) {
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

}
