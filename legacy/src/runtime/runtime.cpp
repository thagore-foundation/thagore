#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <cstdio>
#include <limits>
#include <thread>
#include <string>
#include <string_view>
#include <array>
#include <optional>
#include <vector>
#include <unordered_map>
#include <unordered_set>

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
  std::vector<std::string> items {};
  AstNode *left {nullptr};
  AstNode *right {nullptr};
};

struct RuntimeInterpreter {
  std::unordered_map<std::string, int> env {};
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

auto cstrOrEmpty(const char *text) -> const char * {
  return text == nullptr ? "" : text;
}

auto isPathSeparator(char ch) -> bool {
  return ch == '/' || ch == '\\';
}

auto quoteShellArg(const std::string &arg) -> std::string {
  std::string out {"\""};
  for (char ch : arg) {
    if (ch == '"' || ch == '\\') {
      out.push_back('\\');
    }
    out.push_back(ch);
  }
  out.push_back('"');
  return out;
}

auto runCommandCapture(const std::string &command) -> std::optional<std::string> {
#if defined(_WIN32)
  FILE *pipe = _popen(command.c_str(), "rb");
#else
  FILE *pipe = popen(command.c_str(), "r");
#endif
  if (pipe == nullptr) {
    return std::nullopt;
  }

  std::string output {};
  std::array<char, 4096> buffer {};
  while (true) {
    const std::size_t read = std::fread(buffer.data(), 1, buffer.size(), pipe);
    if (read > 0) {
      output.append(buffer.data(), read);
    }
    if (read < buffer.size()) {
      if (std::feof(pipe) != 0 || std::ferror(pipe) != 0) {
        break;
      }
    }
  }

#if defined(_WIN32)
  const int exitCode = _pclose(pipe);
#else
  const int exitCode = pclose(pipe);
#endif
  if (exitCode != 0) {
    return std::nullopt;
  }
  return output;
}

void registerManagedBuffer(char *buffer) {
  if (buffer == nullptr) {
    return;
  }
  std::lock_guard lock {managedStringsMutex()};
  managedStrings().insert_or_assign(buffer, ManagedString {.buffer = buffer, .refCount = 1});
}

auto makeManagedCString(const char *text) -> char * {
  auto *out = copyCString(text);
  registerManagedBuffer(out);
  return out;
}

auto makeManagedString(const std::string &text) -> char * {
  auto *out = static_cast<char *>(std::malloc(text.size() + 1));
  if (out == nullptr) {
    return nullptr;
  }
  std::memcpy(out, text.data(), text.size());
  out[text.size()] = '\0';
  registerManagedBuffer(out);
  return out;
}

auto tokenizeExprSource(const char *source) -> std::vector<ExprToken> {
  std::vector<ExprToken> out {};
  if (source == nullptr) {
    out.push_back(ExprToken {.kind = "EOF", .text = ""});
    return out;
  }

  const std::string input = source;
  std::size_t i = 0;
  bool atLineStart = true;
  std::vector<int> indents {0};

  auto pushToken = [&](std::string kind, std::string text = {}) {
    out.push_back(ExprToken {.kind = std::move(kind), .text = std::move(text)});
  };

  auto emitDedentsTo = [&](int spaces) {
    while (indents.size() > 1 && spaces < indents.back()) {
      indents.pop_back();
      pushToken("DEDENT");
    }
    if (spaces != indents.back()) {
      pushToken("INVALID", "DEDENT_MISMATCH");
    }
  };

  while (i < input.size()) {
    if (atLineStart) {
      int spaces = 0;
      while (i < input.size() && input[i] == ' ') {
        spaces += 1;
        i += 1;
      }
      if (i < input.size() && input[i] == '\t') {
        pushToken("INVALID", "TAB_INDENT");
        while (i < input.size() && input[i] != '\n') {
          i += 1;
        }
        continue;
      }
      if (i >= input.size()) {
        break;
      }
      if (input[i] == '\n') {
        pushToken("NEWLINE", "\\n");
        i += 1;
        atLineStart = true;
        continue;
      }
      if (input[i] == '#' || (input[i] == '/' && i + 1 < input.size() && input[i + 1] == '/')) {
        while (i < input.size() && input[i] != '\n') {
          i += 1;
        }
        continue;
      }

      if (spaces > indents.back()) {
        indents.push_back(spaces);
        pushToken("INDENT");
      } else if (spaces < indents.back()) {
        emitDedentsTo(spaces);
      }
      atLineStart = false;
      continue;
    }

    const char ch = input[i];
    if (ch == '\r' || ch == ' ' || ch == '\t') {
      i += 1;
      continue;
    }
    if (ch == '#' || (ch == '/' && i + 1 < input.size() && input[i + 1] == '/')) {
      while (i < input.size() && input[i] != '\n') {
        i += 1;
      }
      continue;
    }
    if (ch == '\n') {
      pushToken("NEWLINE", "\\n");
      i += 1;
      atLineStart = true;
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      std::string text {};
      while (i < input.size() && std::isdigit(static_cast<unsigned char>(input[i])) != 0) {
        text.push_back(input[i]);
        i += 1;
      }
      pushToken("INT", text);
      continue;
    }
    if (ch == 'v' && i + 1 < input.size() && input[i + 1] == '"') {
      i += 2;
      std::string text {};
      while (i < input.size() && input[i] != '"') {
        if (input[i] == '\n') {
          pushToken("INVALID", "UNTERMINATED_INTERP_STRING");
          break;
        }
        text.push_back(input[i]);
        i += 1;
      }
      if (i < input.size() && input[i] == '"') {
        i += 1;
      }
      pushToken("INTERP_STRING", text);
      continue;
    }
    if (ch == '"') {
      i += 1;
      std::string text {};
      while (i < input.size() && input[i] != '"') {
        if (input[i] == '\n') {
          pushToken("INVALID", "UNTERMINATED_STRING");
          break;
        }
        text.push_back(input[i]);
        i += 1;
      }
      if (i < input.size() && input[i] == '"') {
        i += 1;
      }
      pushToken("STRING", text);
      continue;
    }
    if (std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_') {
      std::string text {};
      while (
        i < input.size() &&
        (std::isalnum(static_cast<unsigned char>(input[i])) != 0 || input[i] == '_')
      ) {
        text.push_back(input[i]);
        i += 1;
      }
      if (text == "let") {
        pushToken("LET", text);
      } else if (text == "func") {
        pushToken("FUNC", text);
      } else if (text == "if") {
        pushToken("IF", text);
      } else if (text == "while") {
        pushToken("WHILE", text);
      } else if (text == "return") {
        pushToken("RETURN", text);
      } else if (text == "print") {
        pushToken("PRINT", text);
      } else {
        pushToken("IDENT", text);
      }
      continue;
    }

    switch (ch) {
      case '+': pushToken("PLUS", "+"); i += 1; break;
      case '-': pushToken("MINUS", "-"); i += 1; break;
      case '*': pushToken("STAR", "*"); i += 1; break;
      case '/': pushToken("SLASH", "/"); i += 1; break;
      case '(': pushToken("LPAREN", "("); i += 1; break;
      case ')': pushToken("RPAREN", ")"); i += 1; break;
      case '=': pushToken("EQUAL", "="); i += 1; break;
      case ':': pushToken("COLON", ":"); i += 1; break;
      case ',': pushToken("COMMA", ","); i += 1; break;
      case '>': pushToken("GT", ">"); i += 1; break;
      case '<': pushToken("LT", "<"); i += 1; break;
      default:
        pushToken("INVALID", std::string(1, ch));
        i += 1;
        break;
    }
  }

  while (indents.size() > 1) {
    indents.pop_back();
    pushToken("DEDENT");
  }
  out.push_back(ExprToken {.kind = "EOF", .text = ""});
  return out;
}

auto trimLeft(std::string_view text) -> std::string_view {
  std::size_t i = 0;
  while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r')) {
    i += 1;
  }
  return text.substr(i);
}

auto leadingSpaces(std::string_view text) -> int {
  int spaces = 0;
  while (spaces < static_cast<int>(text.size()) && text[static_cast<std::size_t>(spaces)] == ' ') {
    spaces += 1;
  }
  return spaces;
}

auto startsWith(std::string_view value, std::string_view prefix) -> bool {
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

auto unescapeBasicString(std::string_view text) -> std::string {
  std::string out {};
  bool escaped = false;
  for (char ch : text) {
    if (escaped) {
      if (ch == 'n') {
        out.push_back('\n');
      } else if (ch == 't') {
        out.push_back('\t');
      } else if (ch == '"' || ch == '\\') {
        out.push_back(ch);
      } else {
        out.push_back(ch);
      }
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    out.push_back(ch);
  }
  if (escaped) {
    out.push_back('\\');
  }
  return out;
}

auto parsePrintStringArg(std::string_view line) -> std::optional<std::string> {
  if (!startsWith(line, "print(") || line.empty() || line.back() != ')') {
    return std::nullopt;
  }
  std::string_view inner = line.substr(6, line.size() - 7);
  inner = trimLeft(inner);
  if (inner.empty()) {
    return std::nullopt;
  }

  std::size_t start = 0;
  if (startsWith(inner, "v\"")) {
    start = 2;
  } else if (inner.front() == '"') {
    start = 1;
  } else {
    return std::nullopt;
  }

  bool escaped = false;
  for (std::size_t i = start; i < inner.size(); ++i) {
    const char ch = inner[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      return unescapeBasicString(inner.substr(start, i - start));
    }
  }
  return std::nullopt;
}

auto parseProgramSource(const char *source) -> AstNode * {
  auto *node = new AstNode {};
  node->kind = "Program";
  if (source == nullptr) {
    return node;
  }

  const std::string src = source;
  std::size_t cursor = 0;
  bool inMain = false;
  int mainIndent = 0;
  int bodyIndent = -1;

  while (cursor <= src.size()) {
    const std::size_t end = src.find('\n', cursor);
    const std::size_t lineEnd = end == std::string::npos ? src.size() : end;
    const std::string_view line {src.data() + cursor, lineEnd - cursor};
    const std::string_view trimmed = trimLeft(line);

    if (!trimmed.empty() && !startsWith(trimmed, "#") && !startsWith(trimmed, "//")) {
      const int indent = leadingSpaces(line);
      if (!inMain) {
        if (indent == 0 && startsWith(trimmed, "func main()") && trimmed.find(':') != std::string_view::npos) {
          inMain = true;
          mainIndent = indent;
          bodyIndent = -1;
        }
      } else {
        if (indent <= mainIndent) {
          break;
        }
        if (bodyIndent < 0) {
          bodyIndent = indent;
        }
        if (indent < bodyIndent) {
          break;
        }
        if (indent == bodyIndent) {
          auto printText = parsePrintStringArg(trimmed);
          if (printText.has_value()) {
            node->items.push_back(*printText);
          }
        }
      }
    }

    if (end == std::string::npos) {
      break;
    }
    cursor = end + 1;
  }

  return node;
}

auto escapeCStringForC(std::string_view text) -> std::string {
  std::string out {};
  for (unsigned char ch : text) {
    if (ch == '\\') {
      out.append("\\\\");
    } else if (ch == '"') {
      out.append("\\\"");
    } else if (ch == '\n') {
      out.append("\\n");
    } else if (ch == '\t') {
      out.append("\\t");
    } else if (ch < 32 || ch > 126) {
      char buf[5] {};
      std::snprintf(buf, sizeof(buf), "\\x%02X", ch);
      out.append(buf);
    } else {
      out.push_back(static_cast<char>(ch));
    }
  }
  return out;
}

auto escapeCStringForLLVM(std::string_view text) -> std::string {
  std::string out {};
  for (unsigned char ch : text) {
    if (ch >= 32 && ch <= 126 && ch != '"' && ch != '\\') {
      out.push_back(static_cast<char>(ch));
    } else {
      char buf[4] {};
      std::snprintf(buf, sizeof(buf), "%02X", ch);
      out.push_back('\\');
      out.append(buf);
    }
  }
  return out;
}

class ExprParser {
public:
  explicit ExprParser(const std::vector<ExprToken> &tokens_) : tokens(tokens_) {}

  auto parseExpr() -> AstNode * {
    return parseAddSub();
  }

  auto parseStatement() -> AstNode * {
    if (match("LET")) {
      if (current().kind != "IDENT") {
        return makeLiteralNode("0");
      }
      const auto name = current().text;
      ++pos;
      if (!match("EQUAL")) {
        return makeLiteralNode("0");
      }
      auto *expr = parseAddSub();
      return makeLetNode(name, expr);
    }
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
    if (current().kind == "IDENT") {
      auto name = current().text;
      ++pos;
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
  std::string out {};
  appendAstLine(out, 0, "Binary(" + root->op + ")");
  appendAstNode(out, root->left, 2, "Left");
  appendAstNode(out, root->right, 2, "Right");
  return out;
}

auto evalExprWithEnv(AstNode *node, RuntimeInterpreter *interp) -> int {
  if (node == nullptr) {
    return 0;
  }
  if (node->kind == "Literal") {
    return std::atoi(node->value.c_str());
  }
  if (node->kind == "Variable") {
    if (interp == nullptr) {
      return 0;
    }
    if (auto it = interp->env.find(node->value); it != interp->env.end()) {
      return it->second;
    }
    return 0;
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
  }
  return 0;
}

auto execStmtWithEnv(AstNode *node, RuntimeInterpreter *interp) -> int {
  if (node == nullptr) {
    return 0;
  }
  if (node->kind == "Let") {
    const int value = evalExprWithEnv(node->left, interp);
    if (interp != nullptr) {
      interp->env[node->value] = value;
    }
    return value;
  }
  return evalExprWithEnv(node, interp);
}

void freeAstNodeRecursive(AstNode *node, std::unordered_set<AstNode *> &seen) {
  if (node == nullptr) {
    return;
  }
  if (!seen.insert(node).second) {
    return;
  }
  freeAstNodeRecursive(node->left, seen);
  freeAstNodeRecursive(node->right, seen);
  delete node;
}

} // namespace

extern "C" {

void __thg_mem_free(void *ptr);

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
    return makeManagedCString("");
  }

  const int total = static_cast<int>(std::strlen(s));
  if (start < 0 || start >= total) {
    return makeManagedCString("");
  }

  if (len < 0) {
    return makeManagedCString("");
  }

  const int maxLen = total - start;
  const int actualLen = len > maxLen ? maxLen : len;
  auto *out = static_cast<char *>(std::malloc(static_cast<std::size_t>(actualLen) + 1));
  if (out == nullptr) {
    return nullptr;
  }
  std::memcpy(out, s + start, static_cast<std::size_t>(actualLen));
  out[actualLen] = '\0';
  registerManagedBuffer(out);
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
  if (it->second.refCount > 1) {
    --it->second.refCount;
    return;
  }
  if (it->second.refCount <= 1) {
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

void __thg_throw(const char *message) {
  if (message == nullptr) {
    std::fprintf(stderr, "thagore throw: <null>\n");
  } else {
    std::fprintf(stderr, "thagore throw: %s\n", message);
  }
  std::abort();
}

void *__thg_mem_alloc(int size) {
  if (size <= 0) {
    return nullptr;
  }
  return std::malloc(static_cast<std::size_t>(size));
}

void *__thg_mem_realloc(void *ptr, int new_size) {
  if (new_size <= 0) {
    __thg_mem_free(ptr);
    return nullptr;
  }
  if (ptr == nullptr) {
    return std::malloc(static_cast<std::size_t>(new_size));
  }

  auto *key = static_cast<const char *>(ptr);
  {
    std::lock_guard lock {managedStringsMutex()};
    auto &table = managedStrings();
    auto it = table.find(key);
    if (it != table.end()) {
      if (it->second.refCount <= 1) {
        auto *resized =
          static_cast<char *>(std::realloc(it->second.buffer, static_cast<std::size_t>(new_size)));
        if (resized == nullptr) {
          return nullptr;
        }
        resized[new_size - 1] = '\0';

        const auto refs = it->second.refCount;
        if (resized != it->second.buffer) {
          table.erase(it);
          table.insert_or_assign(resized, ManagedString {.buffer = resized, .refCount = refs});
        } else {
          it->second.buffer = resized;
          it->second.refCount = refs;
        }
        return resized;
      }

      auto *resized = static_cast<char *>(std::malloc(static_cast<std::size_t>(new_size)));
      if (resized == nullptr) {
        return nullptr;
      }
      const auto old_len = std::strlen(it->second.buffer);
      const auto copy_len = std::min<std::size_t>(static_cast<std::size_t>(new_size - 1), old_len);
      if (copy_len > 0) {
        std::memcpy(resized, it->second.buffer, copy_len);
      }
      resized[copy_len] = '\0';
      table.insert_or_assign(resized, ManagedString {.buffer = resized, .refCount = 1});
      --it->second.refCount;
      return resized;
    }
  }
  return std::realloc(ptr, static_cast<std::size_t>(new_size));
}

void __thg_mem_free(void *ptr) {
  if (ptr == nullptr) {
    return;
  }
  const auto *key = static_cast<const char *>(ptr);
  {
    std::lock_guard lock {managedStringsMutex()};
    auto &table = managedStrings();
    auto it = table.find(key);
    if (it != table.end()) {
      if (it->second.refCount > 1) {
        --it->second.refCount;
      } else {
        std::free(it->second.buffer);
        table.erase(it);
      }
      return;
    }
  }
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
  registerManagedBuffer(res);
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
  registerManagedBuffer(copy);
  return copy;
}

void __thg_str_free(char *s) {
  __thg_release(s);
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

int __thg_str_contains(const char *text, const char *needle) {
  const std::string hay = cstrOrEmpty(text);
  const std::string ned = cstrOrEmpty(needle);
  if (ned.empty()) {
    return 1;
  }
  return hay.find(ned) != std::string::npos ? 1 : 0;
}

int __thg_str_starts_with(const char *text, const char *prefix) {
  const std::string hay = cstrOrEmpty(text);
  const std::string pre = cstrOrEmpty(prefix);
  if (pre.size() > hay.size()) {
    return 0;
  }
  return std::equal(pre.begin(), pre.end(), hay.begin()) ? 1 : 0;
}

int __thg_str_ends_with(const char *text, const char *suffix) {
  const std::string hay = cstrOrEmpty(text);
  const std::string suf = cstrOrEmpty(suffix);
  if (suf.size() > hay.size()) {
    return 0;
  }
  return std::equal(suf.rbegin(), suf.rend(), hay.rbegin()) ? 1 : 0;
}

const char *__thg_str_trim(const char *text) {
  const std::string src = cstrOrEmpty(text);
  std::size_t start = 0;
  while (start < src.size() && std::isspace(static_cast<unsigned char>(src[start])) != 0) {
    ++start;
  }
  std::size_t end = src.size();
  while (end > start && std::isspace(static_cast<unsigned char>(src[end - 1])) != 0) {
    --end;
  }
  return makeManagedString(src.substr(start, end - start));
}

const char *__thg_str_replace(const char *text, const char *oldValue, const char *newValue) {
  const std::string src = cstrOrEmpty(text);
  const std::string from = cstrOrEmpty(oldValue);
  const std::string to = cstrOrEmpty(newValue);
  if (from.empty()) {
    return makeManagedString(src);
  }

  std::string out {};
  std::size_t cursor = 0;
  while (cursor < src.size()) {
    const std::size_t pos = src.find(from, cursor);
    if (pos == std::string::npos) {
      out.append(src.substr(cursor));
      break;
    }
    out.append(src.substr(cursor, pos - cursor));
    out.append(to);
    cursor = pos + from.size();
  }
  return makeManagedString(out);
}

const char *__thg_str_lower(const char *text) {
  std::string out = cstrOrEmpty(text);
  for (auto &ch : out) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return makeManagedString(out);
}

const char *__thg_str_upper(const char *text) {
  std::string out = cstrOrEmpty(text);
  for (auto &ch : out) {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  return makeManagedString(out);
}

int __thg_str_compare(const char *left, const char *right) {
  const int cmp = std::strcmp(cstrOrEmpty(left), cstrOrEmpty(right));
  if (cmp < 0) {
    return -1;
  }
  if (cmp > 0) {
    return 1;
  }
  return 0;
}

int __string_codepoint(const char *ch) {
  if (ch == nullptr || *ch == '\0') {
    return 0;
  }
  return static_cast<int>(static_cast<unsigned char>(*ch));
}

const char *__string_from_codepoint(int cp) {
  if (cp < 0 || cp > 255) {
    cp = '?';
  }
  char buf[2] {};
  buf[0] = static_cast<char>(cp);
  buf[1] = '\0';
  return makeManagedCString(buf);
}

const char *__thg_path_strip_trailing(const char *path) {
  std::string out = cstrOrEmpty(path);
  while (!out.empty() && isPathSeparator(out.back())) {
    out.pop_back();
  }
  return makeManagedString(out);
}

const char *__thg_path_strip_leading(const char *path) {
  std::string out = cstrOrEmpty(path);
  std::size_t pos = 0;
  while (pos < out.size() && isPathSeparator(out[pos])) {
    ++pos;
  }
  return makeManagedString(out.substr(pos));
}

const char *__thg_path_basename(const char *path) {
  std::string value = cstrOrEmpty(path);
  while (!value.empty() && isPathSeparator(value.back())) {
    value.pop_back();
  }
  if (value.empty()) {
    return makeManagedCString("");
  }
  const std::size_t sep = value.find_last_of("/\\");
  if (sep == std::string::npos) {
    return makeManagedString(value);
  }
  return makeManagedString(value.substr(sep + 1));
}

const char *__thg_path_dirname(const char *path) {
  std::string value = cstrOrEmpty(path);
  while (!value.empty() && isPathSeparator(value.back())) {
    value.pop_back();
  }
  if (value.empty()) {
    return makeManagedCString("");
  }
  const std::size_t sep = value.find_last_of("/\\");
  if (sep == std::string::npos) {
    return makeManagedCString("");
  }
  if (sep == 0) {
    return makeManagedCString("/");
  }
  return makeManagedString(value.substr(0, sep));
}

const char *__thg_path_ext(const char *path) {
  std::string value = cstrOrEmpty(path);
  while (!value.empty() && isPathSeparator(value.back())) {
    value.pop_back();
  }
  if (value.empty()) {
    return makeManagedCString("");
  }
  const std::size_t sep = value.find_last_of("/\\");
  std::string base = sep == std::string::npos ? value : value.substr(sep + 1);
  if (base.size() <= 1) {
    return makeManagedCString("");
  }
  const std::size_t dot = base.find_last_of('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= base.size()) {
    return makeManagedCString("");
  }
  return makeManagedString(base.substr(dot + 1));
}

const char *__thg_path_join2(const char *left, const char *right) {
  std::string lhs = cstrOrEmpty(left);
  std::string rhs = cstrOrEmpty(right);
  if (lhs.empty()) {
    return makeManagedString(rhs);
  }
  if (rhs.empty()) {
    return makeManagedString(lhs);
  }

  const bool lhsSep = isPathSeparator(lhs.back());
  const bool rhsSep = isPathSeparator(rhs.front());
  if (lhsSep && rhsSep) {
    lhs.pop_back();
  } else if (!lhsSep && !rhsSep) {
    lhs.push_back('/');
  }
  lhs.append(rhs);
  return makeManagedString(lhs);
}

const char *__thg_fmt_trim_trailing(const char *text) {
  const std::string src = cstrOrEmpty(text);
  std::string out {};
  std::string line {};
  for (char ch : src) {
    if (ch == '\n') {
      while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
        line.pop_back();
      }
      out.append(line);
      out.push_back('\n');
      line.clear();
    } else {
      line.push_back(ch);
    }
  }
  while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
    line.pop_back();
  }
  out.append(line);
  return makeManagedString(out);
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

int __thg_token_free(void *token) {
  if (token == nullptr) {
    return 0;
  }
  auto *box = static_cast<TokenBox *>(token);
  std::free(box->kind);
  std::free(box->text);
  std::free(box);
  return 1;
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
  registerManagedBuffer(buffer);

  *outLen = totalLen;
  return buffer;
}

void *__thg_lex_tokenize(const char *source) {
  auto *stream = new TokenStream {};
  stream->tokens = tokenizeExprSource(source);
  return stream;
}

int __thg_tok_free_stream(void *streamPtr) {
  if (streamPtr == nullptr) {
    return 0;
  }
  delete static_cast<TokenStream *>(streamPtr);
  return 1;
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
  if (kind == "NEWLINE") return 12;
  if (kind == "INDENT") return 13;
  if (kind == "DEDENT") return 14;
  if (kind == "COLON") return 15;
  if (kind == "FUNC") return 16;
  if (kind == "IF") return 17;
  if (kind == "WHILE") return 18;
  if (kind == "RETURN") return 19;
  if (kind == "STRING") return 20;
  if (kind == "INTERP_STRING") return 21;
  if (kind == "PRINT") return 22;
  if (kind == "GT") return 23;
  if (kind == "LT") return 24;
  if (kind == "COMMA") return 25;
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

char *__thg_ast_debug(void *nodePtr) {
  auto text = buildAstDebugString(static_cast<AstNode *>(nodePtr));
  return makeManagedString(text);
}

int __thg_ast_free(void *nodePtr) {
  if (nodePtr == nullptr) {
    return 0;
  }
  std::unordered_set<AstNode *> seen {};
  freeAstNodeRecursive(static_cast<AstNode *>(nodePtr), seen);
  return 1;
}

const char *__env_get(const char *key) {
  if (key == nullptr) {
    return nullptr;
  }
  const char *value = std::getenv(key);
  if (value == nullptr) {
    return nullptr;
  }
  return makeManagedCString(value);
}

int __env_set(const char *key, const char *value) {
  if (key == nullptr || *key == '\0') {
    return 0;
  }
#if defined(_WIN32)
  return _putenv_s(key, value == nullptr ? "" : value) == 0 ? 1 : 0;
#else
  if (value == nullptr) {
    return unsetenv(key) == 0 ? 1 : 0;
  }
  return setenv(key, value, 1) == 0 ? 1 : 0;
#endif
}

const char *__env_cwd() {
  std::error_code ec {};
  const auto cwd = std::filesystem::current_path(ec);
  if (ec) {
    return nullptr;
  }
  return makeManagedString(cwd.string());
}

const char *__env_args() {
  std::string out {};
  for (int i = 0; i < g_argc; ++i) {
    if (i > 0) {
      out.push_back('\n');
    }
    const char *arg = g_argv != nullptr ? g_argv[i] : "";
    out.append(arg == nullptr ? "" : arg);
  }
  return makeManagedString(out);
}

const char *__fs_read_text(const char *path) {
  if (path == nullptr || *path == '\0') {
    return nullptr;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return nullptr;
  }
  std::string content {};
  input.seekg(0, std::ios::end);
  const auto size = input.tellg();
  if (size > 0) {
    content.resize(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    input.read(content.data(), static_cast<std::streamsize>(content.size()));
  }
  return makeManagedString(content);
}

int __fs_write_text(const char *path, const char *text) {
  if (path == nullptr || *path == '\0') {
    return 0;
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return 0;
  }
  if (text != nullptr) {
    output.write(text, static_cast<std::streamsize>(std::strlen(text)));
  }
  return output.good() ? 1 : 0;
}

int __fs_exists(const char *path) {
  if (path == nullptr || *path == '\0') {
    return 0;
  }
  std::error_code ec {};
  const bool exists = std::filesystem::exists(path, ec);
  return (!ec && exists) ? 1 : 0;
}

int __fs_mkdir(const char *path) {
  if (path == nullptr || *path == '\0') {
    return 0;
  }
  std::error_code ec {};
  const bool created = std::filesystem::create_directories(path, ec);
  if (ec) {
    return 0;
  }
  if (created || std::filesystem::is_directory(path, ec)) {
    return 1;
  }
  return 0;
}

const char *__fs_list_dir(const char *path) {
  if (path == nullptr || *path == '\0') {
    return nullptr;
  }
  std::error_code ec {};
  std::filesystem::directory_iterator it(path, ec);
  if (ec) {
    return nullptr;
  }
  std::string out {};
  bool first = true;
  for (const auto &entry : it) {
    if (!first) {
      out.push_back('\n');
    }
    first = false;
    out.append(entry.path().filename().string());
  }
  return makeManagedString(out);
}

void *__fs_open_binary(const char *path, const char *mode) {
  if (path == nullptr || mode == nullptr) {
    return nullptr;
  }
  return std::fopen(path, mode);
}

int __fs_write_bytes(void *handle, const char *buffer) {
  if (handle == nullptr || buffer == nullptr) {
    return 0;
  }
  const auto len = std::strlen(buffer);
  const auto written = std::fwrite(buffer, 1, len, static_cast<FILE *>(handle));
  return static_cast<int>(written);
}

const char *__fs_read_bytes(void *handle, int size) {
  if (handle == nullptr || size <= 0) {
    return makeManagedCString("");
  }
  auto data = std::string(static_cast<std::size_t>(size), '\0');
  const auto read = std::fread(data.data(), 1, static_cast<std::size_t>(size), static_cast<FILE *>(handle));
  data.resize(read);
  return makeManagedString(data);
}

int __fs_seek(void *handle, int offset, int whence) {
  if (handle == nullptr) {
    return -1;
  }
  return std::fseek(static_cast<FILE *>(handle), offset, whence);
}

int __fs_close(void *handle) {
  if (handle == nullptr) {
    return 0;
  }
  return std::fclose(static_cast<FILE *>(handle));
}

const char *__thg_fs_read_text(const char *path) {
  return __fs_read_text(path);
}

int __thg_fs_write_text(const char *path, const char *text) {
  return __fs_write_text(path, text);
}

int __thg_fs_remove(const char *path) {
  if (path == nullptr || *path == '\0') {
    return 0;
  }
  std::error_code ec {};
  const bool removed = std::filesystem::remove(path, ec);
  if (ec) {
    return 0;
  }
  if (removed || !std::filesystem::exists(path, ec)) {
    return ec ? 0 : 1;
  }
  return 0;
}

const char *__http_get(const char *url) {
  if (url == nullptr || *url == '\0') {
    return nullptr;
  }
  const std::string cmd = "curl -fsSL --max-time 20 " + quoteShellArg(url);
  auto out = runCommandCapture(cmd);
  if (!out) {
    return nullptr;
  }
  return makeManagedString(*out);
}

const char *__http_post(const char *url, const char *body) {
  if (url == nullptr || *url == '\0') {
    return nullptr;
  }
  const std::string payload = cstrOrEmpty(body);
  const std::string cmd = std::string("curl -fsSL --max-time 20 -X POST --data ")
    + quoteShellArg(payload)
    + " "
    + quoteShellArg(url);
  auto out = runCommandCapture(cmd);
  if (!out) {
    return nullptr;
  }
  return makeManagedString(*out);
}

int __time_now_ms() {
  static const auto start = std::chrono::steady_clock::now();
  const auto now = std::chrono::steady_clock::now();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
  if (ms > static_cast<long long>(std::numeric_limits<int>::max())) {
    return std::numeric_limits<int>::max();
  }
  if (ms < static_cast<long long>(std::numeric_limits<int>::min())) {
    return std::numeric_limits<int>::min();
  }
  return static_cast<int>(ms);
}

int __time_sleep(int ms) {
  if (ms <= 0) {
    return 0;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  return 0;
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
  return parseProgramSource(source);
}

const char *__thg_codegen_emit_c(void *root) {
  auto *program = static_cast<AstNode *>(root);
  std::string out {};
  out += "#include <stdio.h>\n";
  out += "int main(void) {\n";
  if (program != nullptr && program->kind == "Program") {
    for (const auto &text : program->items) {
      out += "  puts(\"";
      out += escapeCStringForC(text);
      out += "\");\n";
    }
  }
  out += "  return 0;\n";
  out += "}\n";
  return makeManagedString(out);
}

const char *__thg_codegen_emit_llvm(void *root) {
  auto *program = static_cast<AstNode *>(root);
  std::string out {};
  out += "declare i32 @puts(ptr)\n";

  std::vector<std::string> printItems {};
  if (program != nullptr && program->kind == "Program") {
    printItems = program->items;
  }

  for (std::size_t i = 0; i < printItems.size(); ++i) {
    const auto escaped = escapeCStringForLLVM(printItems[i]);
    const std::size_t len = printItems[i].size() + 1;
    out += "@.str";
    out += std::to_string(i);
    out += " = private unnamed_addr constant [";
    out += std::to_string(len);
    out += " x i8] c\"";
    out += escaped;
    out += "\\00\"\n";
  }

  out += "define i32 @main() {\n";
  out += "entry:\n";
  for (std::size_t i = 0; i < printItems.size(); ++i) {
    out += "  call i32 @puts(ptr @.str";
    out += std::to_string(i);
    out += ")\n";
  }
  out += "  ret i32 0\n";
  out += "}\n";
  return makeManagedString(out);
}

const char *__thg_codegen_emit_llvm_from_source(const char *source, const char *module_name) {
  if (source == nullptr || source[0] == '\0') {
    return makeManagedCString("");
  }
  const std::string sourceText {source};

  // Simple scripts (no imports) are handled by lightweight fallback emitter
  // to avoid invoking helper stage0 and noisy diagnostics.
  if (sourceText.find("import ") == std::string::npos && sourceText.find("use ") == std::string::npos) {
    auto *fallback = parseProgramSource(source);
    return __thg_codegen_emit_llvm(fallback);
  }

  std::string moduleNameText {cstrOrEmpty(module_name)};
  if (moduleNameText.empty()) {
    moduleNameText = "thg_module";
  }
  for (char &ch : moduleNameText) {
    const bool alphaNum = std::isalnum(static_cast<unsigned char>(ch)) != 0;
    if (!alphaNum && ch != '_') {
      ch = '_';
    }
  }

  const auto nonce = std::to_string(__time_now_ms()) + "_" + std::to_string(std::rand());
  const auto sourcePath = std::filesystem::path(moduleNameText + "_" + nonce + ".tg");
  const auto irPath = std::filesystem::path(moduleNameText + "_" + nonce + ".ll");

  {
    std::ofstream out(sourcePath, std::ios::binary);
    if (!out) {
      return makeManagedCString("");
    }
    out << sourceText;
  }

  std::filesystem::path helperPath {"legacy\\stage0.exe"};
  if (!std::filesystem::exists(helperPath)) {
    auto *fallback = parseProgramSource(source);
    return __thg_codegen_emit_llvm(fallback);
  }

  const std::string command =
    helperPath.string() + " " +
    sourcePath.string() + " --emit-ir -o " +
    irPath.string();

  const int code = std::system(command.c_str());
  if (code != 0) {
    std::fprintf(stderr, "codegen helper command failed: %s\n", command.c_str());
    std::error_code rmErr {};
    std::filesystem::remove(sourcePath, rmErr);
    std::filesystem::remove(irPath, rmErr);
    auto *fallback = parseProgramSource(source);
    return __thg_codegen_emit_llvm(fallback);
  }

  std::ifstream in(irPath, std::ios::binary);
  if (!in) {
    std::error_code rmErr {};
    std::filesystem::remove(sourcePath, rmErr);
    std::filesystem::remove(irPath, rmErr);
    auto *fallback = parseProgramSource(source);
    return __thg_codegen_emit_llvm(fallback);
  }
  std::string ir((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

  std::error_code rmErr {};
  std::filesystem::remove(sourcePath, rmErr);
  std::filesystem::remove(irPath, rmErr);

  return makeManagedString(ir);
}

int __thg_eval_expr(void *nodePtr) {
  return evalExprWithEnv(static_cast<AstNode *>(nodePtr), nullptr);
}

void *__thg_interp_new() {
  return new RuntimeInterpreter {};
}

int __thg_interp_free(void *interpPtr) {
  if (interpPtr == nullptr) {
    return 0;
  }
  delete static_cast<RuntimeInterpreter *>(interpPtr);
  return 1;
}

int __thg_interp_eval_expr(void *interpPtr, void *nodePtr) {
  auto *interp = static_cast<RuntimeInterpreter *>(interpPtr);
  return evalExprWithEnv(static_cast<AstNode *>(nodePtr), interp);
}

int __thg_interp_exec_stmt(void *interpPtr, void *nodePtr) {
  auto *interp = static_cast<RuntimeInterpreter *>(interpPtr);
  return execStmtWithEnv(static_cast<AstNode *>(nodePtr), interp);
}

}
