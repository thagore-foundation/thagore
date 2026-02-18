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
#include <iostream>
#include <cerrno>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

struct ProgramLine {
  int indent {0};
  std::string text {};
};

struct AstNode {
  std::string kind {};
  std::string op {};
  std::string value {};
  std::vector<std::string> items {};
  std::vector<ProgramLine> lines {};
  bool hasMain {false};
  bool hasUnsupportedTopLevel {false};
  std::string firstUnsupportedTopLevel {};
  AstNode *left {nullptr};
  AstNode *right {nullptr};
};

struct RuntimeInterpreter {
  std::unordered_map<std::string, int> env {};
  bool strict {false};
  bool hadError {false};
};

struct IntentEntry {
  std::string id {};
  std::string kind {};
  std::string targetName {};
  std::string goal {};
  std::string strategy {};
  bool intentEnabled {true};
  std::vector<std::string> constraints {};
  std::vector<std::string> examples {};
  int line {0};
  bool hasGoal {false};
  bool hasStrategy {false};
  bool hasConstraintsHeader {false};
  bool hasExamplesHeader {false};
};

struct IntentRuleInfo {
  std::string id {};
  std::string goal {};
  std::string complexity {"O(n)"};
  bool deterministic {true};
  bool noHeapGrowth {false};
  bool parallelCapable {false};
  bool vectorizeCapable {false};
  int timeRank {9};
  int memoryRank {9};
  double maxError {0.0};
};

struct IntentPlan {
  std::string intentId {};
  std::string goal {};
  std::string selectedRule {};
  std::vector<std::string> candidateRules {};
  int candidateCount {0};
  bool verified {false};
  std::string verifyReason {};
  std::string constraintsDigest {};
  std::string verificationDigest {};
};

struct IntentLockEntry {
  std::string selectedRule {};
  std::string constraintsDigest {};
  std::string verificationDigest {};
};

struct CliIntentRuleRegistry {
  bool enabled {false};
  std::size_t totalBudget {std::numeric_limits<std::size_t>::max()};
  std::unordered_map<std::string, std::size_t> familyBudget {};
  std::unordered_set<std::string> allowedRules {};
  std::string sourcePath {};
};

int g_argc = 0;
char **g_argv = nullptr;
bool g_emitLlvmInternalMode = false;

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

auto quotePowerShellLiteral(const std::string &value) -> std::string {
  std::string out {"'"};
  for (char ch : value) {
    if (ch == '\'') {
      out.append("''");
      continue;
    }
    out.push_back(ch);
  }
  out.push_back('\'');
  return out;
}

auto formatExecPathForShell(const std::filesystem::path &path) -> std::string {
  std::string raw = path.string();
#if defined(_WIN32)
  if (raw.rfind("./", 0) == 0 || raw.rfind(".\\", 0) == 0) {
    raw = raw.substr(2);
  }
  std::replace(raw.begin(), raw.end(), '/', '\\');
  if (raw.find(' ') != std::string::npos) {
    return "\"" + raw + "\"";
  }
  return raw;
#else
  const bool hasSeparator = raw.find('/') != std::string::npos || raw.find('\\') != std::string::npos;
  if (!path.is_absolute() && !hasSeparator) {
    raw = "./" + raw;
  }
  return quoteShellArg(raw);
#endif
}

auto runDirectCommandMaybeTimed(const std::string &commandLine, int timeoutMs) -> int {
#if defined(_WIN32)
  STARTUPINFOA startup {};
  PROCESS_INFORMATION processInfo {};
  startup.cb = sizeof(startup);
  std::vector<char> mutableCmd(commandLine.begin(), commandLine.end());
  mutableCmd.push_back('\0');
  const BOOL created = CreateProcessA(
    nullptr,
    mutableCmd.data(),
    nullptr,
    nullptr,
    FALSE,
    CREATE_NO_WINDOW,
    nullptr,
    nullptr,
    &startup,
    &processInfo
  );
  if (!created) {
    return -1;
  }
  const DWORD waitMs = timeoutMs > 0 ? static_cast<DWORD>(timeoutMs) : INFINITE;
  const DWORD waitCode = WaitForSingleObject(processInfo.hProcess, waitMs);
  if (waitCode == WAIT_TIMEOUT) {
    TerminateProcess(processInfo.hProcess, 124);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return 124;
  }
  DWORD exitCode = 1;
  GetExitCodeProcess(processInfo.hProcess, &exitCode);
  CloseHandle(processInfo.hThread);
  CloseHandle(processInfo.hProcess);
  return static_cast<int>(exitCode);
#else
  (void)timeoutMs;
  return std::system(commandLine.c_str());
#endif
}

auto isCmdBuiltinToken(std::string token) -> bool {
  if (token.empty()) {
    return false;
  }
  const std::size_t sep = token.find_last_of("/\\");
  if (sep != std::string::npos) {
    token = token.substr(sep + 1);
  }
  const std::size_t dot = token.find('.');
  if (dot != std::string::npos) {
    token = token.substr(0, dot);
  }
  std::transform(
    token.begin(),
    token.end(),
    token.begin(),
    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); }
  );
  static const std::unordered_set<std::string> builtins {
    "assoc", "break", "call", "cd", "chdir", "cls", "color", "copy",
    "date", "del", "dir", "echo", "endlocal", "erase", "exit", "for",
    "ftype", "if", "md", "mkdir", "mklink", "move", "path", "pause",
    "popd", "prompt", "pushd", "rd", "ren", "rename", "rmdir", "set",
    "setlocal", "shift", "start", "time", "title", "type", "ver",
    "verify", "vol"
  };
  return builtins.find(token) != builtins.end();
}

auto needsWindowsShell(const std::string &command) -> bool {
#if defined(_WIN32)
  bool inDouble = false;
  for (char ch : command) {
    if (ch == '"') {
      inDouble = !inDouble;
      continue;
    }
    if (!inDouble) {
      if (ch == '|' || ch == '&' || ch == '<' || ch == '>' || ch == ';' || ch == '%' || ch == '^') {
        return true;
      }
    }
  }

  std::size_t i = 0;
  while (i < command.size() && (command[i] == ' ' || command[i] == '\t')) {
    i += 1;
  }
  if (i >= command.size()) {
    return false;
  }

  std::string token {};
  if (command[i] == '"') {
    i += 1;
    while (i < command.size() && command[i] != '"') {
      token.push_back(command[i]);
      i += 1;
    }
  } else {
    while (i < command.size() && command[i] != ' ' && command[i] != '\t') {
      token.push_back(command[i]);
      i += 1;
    }
  }
  return isCmdBuiltinToken(token);
#else
  (void)command;
  return false;
#endif
}

auto splitArgsText(const char *argsText) -> std::vector<std::string> {
  std::vector<std::string> out {};
  if (argsText == nullptr || *argsText == '\0') {
    return out;
  }
  std::string current {};
  bool sawAny = false;
  bool lastWasNewline = false;
  for (char ch : std::string_view(argsText)) {
    if (ch == '\r') {
      continue;
    }
    sawAny = true;
    if (ch == '\n') {
      out.push_back(current);
      current.clear();
      lastWasNewline = true;
      continue;
    }
    current.push_back(ch);
    lastWasNewline = false;
  }
  if (!current.empty() || (sawAny && !lastWasNewline)) {
    out.push_back(current);
  }
  return out;
}

#if defined(_WIN32)
auto quoteWindowsProcessArg(const std::string &arg) -> std::string {
  if (arg.empty()) {
    return "\"\"";
  }
  bool needsQuotes = false;
  for (char ch : arg) {
    if (ch == ' ' || ch == '\t' || ch == '"') {
      needsQuotes = true;
      break;
    }
  }
  if (!needsQuotes) {
    return arg;
  }
  std::string out {"\""};
  int backslashes = 0;
  for (char ch : arg) {
    if (ch == '\\') {
      backslashes += 1;
      continue;
    }
    if (ch == '"') {
      out.append(static_cast<std::size_t>(backslashes * 2 + 1), '\\');
      out.push_back('"');
      backslashes = 0;
      continue;
    }
    if (backslashes > 0) {
      out.append(static_cast<std::size_t>(backslashes), '\\');
      backslashes = 0;
    }
    out.push_back(ch);
  }
  if (backslashes > 0) {
    out.append(static_cast<std::size_t>(backslashes * 2), '\\');
  }
  out.push_back('"');
  return out;
}
#endif

auto runProcessArgvMaybeTimed(
  const std::string &program,
  const std::vector<std::string> &args,
  const std::string &stdoutPath,
  const std::string &stderrPath,
  int timeoutMs
) -> int {
  if (program.empty()) {
    return -1;
  }
#if defined(_WIN32)
  std::string cmdLine = quoteWindowsProcessArg(program);
  for (const auto &arg : args) {
    cmdLine.push_back(' ');
    cmdLine.append(quoteWindowsProcessArg(arg));
  }

  SECURITY_ATTRIBUTES sa {};
  sa.nLength = sizeof(sa);
  sa.lpSecurityDescriptor = nullptr;
  sa.bInheritHandle = TRUE;

  HANDLE outHandle = INVALID_HANDLE_VALUE;
  HANDLE errHandle = INVALID_HANDLE_VALUE;
  bool closeOut = false;
  bool closeErr = false;

  if (!stdoutPath.empty()) {
    outHandle = CreateFileA(
      stdoutPath.c_str(),
      GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      &sa,
      CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr
    );
    if (outHandle == INVALID_HANDLE_VALUE) {
      return -1;
    }
    closeOut = true;
  }

  if (!stderrPath.empty()) {
    if (!stdoutPath.empty() && stderrPath == stdoutPath) {
      errHandle = outHandle;
      closeErr = false;
    } else {
      errHandle = CreateFileA(
        stderrPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
      );
      if (errHandle == INVALID_HANDLE_VALUE) {
        if (closeOut && outHandle != INVALID_HANDLE_VALUE) {
          CloseHandle(outHandle);
        }
        return -1;
      }
      closeErr = true;
    }
  }

  STARTUPINFOA startup {};
  PROCESS_INFORMATION processInfo {};
  startup.cb = sizeof(startup);
  const bool hasRedirect = (!stdoutPath.empty() || !stderrPath.empty());
  if (hasRedirect) {
    startup.dwFlags |= STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = !stdoutPath.empty() ? outHandle : GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = !stderrPath.empty() ? errHandle : GetStdHandle(STD_ERROR_HANDLE);
  }

  std::vector<char> mutableCmd(cmdLine.begin(), cmdLine.end());
  mutableCmd.push_back('\0');
  const BOOL created = CreateProcessA(
    nullptr,
    mutableCmd.data(),
    nullptr,
    nullptr,
    hasRedirect ? TRUE : FALSE,
    CREATE_NO_WINDOW,
    nullptr,
    nullptr,
    &startup,
    &processInfo
  );

  if (closeOut && outHandle != INVALID_HANDLE_VALUE) {
    CloseHandle(outHandle);
  }
  if (closeErr && errHandle != INVALID_HANDLE_VALUE) {
    CloseHandle(errHandle);
  }

  if (!created) {
    return -1;
  }

  const DWORD waitMs = timeoutMs > 0 ? static_cast<DWORD>(timeoutMs) : INFINITE;
  const DWORD waitCode = WaitForSingleObject(processInfo.hProcess, waitMs);
  if (waitCode == WAIT_TIMEOUT) {
    TerminateProcess(processInfo.hProcess, 124);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return 124;
  }
  DWORD exitCode = 1;
  GetExitCodeProcess(processInfo.hProcess, &exitCode);
  CloseHandle(processInfo.hThread);
  CloseHandle(processInfo.hProcess);
  return static_cast<int>(exitCode);
#else
  std::string cmd = quoteShellArg(program);
  for (const auto &arg : args) {
    cmd.push_back(' ');
    cmd.append(quoteShellArg(arg));
  }
  if (!stdoutPath.empty()) {
    cmd.append(" > ");
    cmd.append(quoteShellArg(stdoutPath));
  }
  if (!stderrPath.empty()) {
    if (!stdoutPath.empty() && stderrPath == stdoutPath) {
      cmd.append(" 2>&1");
    } else {
      cmd.append(" 2> ");
      cmd.append(quoteShellArg(stderrPath));
    }
  }
  return runDirectCommandMaybeTimed(cmd, timeoutMs);
#endif
}

auto runCommandMaybeTimed(const std::string &command, int timeoutMs) -> int {
#if defined(_WIN32)
  STARTUPINFOA startup {};
  PROCESS_INFORMATION processInfo {};
  startup.cb = sizeof(startup);
  std::string cmdLine = "cmd /C " + command;
  std::vector<char> mutableCmd(cmdLine.begin(), cmdLine.end());
  mutableCmd.push_back('\0');
  const BOOL created = CreateProcessA(
    nullptr,
    mutableCmd.data(),
    nullptr,
    nullptr,
    FALSE,
    CREATE_NO_WINDOW,
    nullptr,
    nullptr,
    &startup,
    &processInfo
  );
  if (!created) {
    return -1;
  }
  const DWORD waitMs = timeoutMs > 0 ? static_cast<DWORD>(timeoutMs) : INFINITE;
  const DWORD waitCode = WaitForSingleObject(processInfo.hProcess, waitMs);
  if (waitCode == WAIT_TIMEOUT) {
    TerminateProcess(processInfo.hProcess, 124);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return 124;
  }
  DWORD exitCode = 1;
  GetExitCodeProcess(processInfo.hProcess, &exitCode);
  CloseHandle(processInfo.hThread);
  CloseHandle(processInfo.hProcess);
  return static_cast<int>(exitCode);
#else
  (void)timeoutMs;
  return std::system(command.c_str());
#endif
}

auto resolveSelfExecutablePath() -> std::filesystem::path {
#if defined(_WIN32)
  std::array<char, 4096> moduleBuf {};
  const DWORD moduleLen = GetModuleFileNameA(nullptr, moduleBuf.data(), static_cast<DWORD>(moduleBuf.size()));
  if (moduleLen > 0 && moduleLen < moduleBuf.size()) {
    const auto modulePath = std::filesystem::path(std::string(moduleBuf.data(), moduleLen));
    if (std::filesystem::exists(modulePath)) {
      return modulePath;
    }
  }
#endif
  if (g_argv == nullptr || g_argc <= 0 || g_argv[0] == nullptr || g_argv[0][0] == '\0') {
    return {};
  }
  std::error_code ec {};
  const auto direct = std::filesystem::path(g_argv[0]);
  if (std::filesystem::exists(direct)) {
    return direct;
  }
  const auto absolute = std::filesystem::absolute(direct, ec);
  if (!ec && std::filesystem::exists(absolute)) {
    return absolute;
  }
  const auto fileOnly = direct.filename();
  if (!fileOnly.empty() && std::filesystem::exists(fileOnly)) {
    return fileOnly;
  }
  return {};
}

auto isInternalEmitMode() -> bool {
  if (g_emitLlvmInternalMode) {
    return true;
  }
  const char *value = std::getenv("THAGORE_INTERNAL_EMIT");
  return value != nullptr && std::strcmp(value, "1") == 0;
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

auto trimRight(std::string_view text) -> std::string_view {
  std::size_t end = text.size();
  while (end > 0) {
    const char ch = text[end - 1];
    if (ch == ' ' || ch == '\t' || ch == '\r') {
      end -= 1;
      continue;
    }
    break;
  }
  return text.substr(0, end);
}

auto trim(std::string_view text) -> std::string_view {
  return trimRight(trimLeft(text));
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

auto isTopLevelDeclarationLike(std::string_view line) -> bool {
  const auto text = trim(line);
  if (text.empty()) {
    return true;
  }
  return
    startsWith(text, "import ") ||
    startsWith(text, "use ") ||
    startsWith(text, "extern ") ||
    startsWith(text, "struct ") ||
    startsWith(text, "impl ") ||
    startsWith(text, "func ");
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
    const std::string_view trimmed = trim(line);

    if (!trimmed.empty() && !startsWith(trimmed, "#") && !startsWith(trimmed, "//")) {
      const int indent = leadingSpaces(line);
      if (!inMain) {
        if (indent == 0 && startsWith(trimmed, "func main()") && trimmed.find(':') != std::string_view::npos) {
          inMain = true;
          node->hasMain = true;
          mainIndent = indent;
          bodyIndent = -1;
        } else {
          if (!isTopLevelDeclarationLike(trimmed)) {
            node->hasUnsupportedTopLevel = true;
            if (node->firstUnsupportedTopLevel.empty()) {
              node->firstUnsupportedTopLevel = std::string(trimmed);
            }
          }
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
        node->lines.push_back(ProgramLine {.indent = indent - bodyIndent, .text = std::string(trimmed)});
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

auto isFallbackProgramSupported(const AstNode *program) -> bool {
  if (program == nullptr || !program->hasMain || program->hasUnsupportedTopLevel) {
    return false;
  }
  std::size_t printableLineCount = 0;
  for (const auto &line : program->lines) {
    if (line.indent != 0) {
      return false;
    }
    if (!parsePrintStringArg(line.text).has_value()) {
      return false;
    }
    printableLineCount += 1;
  }
  return printableLineCount == program->items.size();
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

void freeAstNodeRecursive(AstNode *node, std::unordered_set<AstNode *> &seen);
auto evalExprWithEnv(AstNode *node, RuntimeInterpreter *interp) -> int;
auto execStmtWithEnv(AstNode *node, RuntimeInterpreter *interp) -> int;

auto parsePrintArgument(std::string_view line) -> std::optional<std::string_view> {
  const auto text = trim(line);
  if (!startsWith(text, "print(") || text.size() < 7 || text.back() != ')') {
    return std::nullopt;
  }
  return trim(text.substr(6, text.size() - 7));
}

auto parseControlExpr(std::string_view line, std::string_view keyword) -> std::optional<std::string_view> {
  const auto text = trim(line);
  if (!startsWith(text, keyword) || text.size() <= keyword.size()) {
    return std::nullopt;
  }
  const char boundary = text[keyword.size()];
  if (boundary != ' ' && boundary != '(') {
    return std::nullopt;
  }
  auto body = trim(text.substr(keyword.size()));
  if (body.empty() || body.back() != ':') {
    return std::nullopt;
  }
  body = trim(body.substr(0, body.size() - 1));
  if (!body.empty() && body.front() == '(' && body.back() == ')') {
    body = trim(body.substr(1, body.size() - 2));
  }
  if (body.empty()) {
    return std::nullopt;
  }
  return body;
}

auto isElseHeader(std::string_view line) -> bool {
  const auto text = trim(line);
  if (!startsWith(text, "else")) {
    return false;
  }
  const auto rest = trim(text.substr(4));
  return rest == ":";
}

auto isIdentifierText(std::string_view text) -> bool {
  const auto id = trim(text);
  if (id.empty()) {
    return false;
  }
  const unsigned char first = static_cast<unsigned char>(id.front());
  if (!(std::isalpha(first) != 0 || id.front() == '_')) {
    return false;
  }
  for (std::size_t i = 1; i < id.size(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(id[i]);
    if (std::isalnum(ch) == 0 && id[i] != '_') {
      return false;
    }
  }
  return true;
}

auto findAssignmentEquals(std::string_view line) -> std::size_t {
  const auto pos = line.find('=');
  if (pos == std::string_view::npos) {
    return pos;
  }
  if (pos > 0) {
    const char prev = line[pos - 1];
    if (prev == '!' || prev == '<' || prev == '>') {
      return std::string_view::npos;
    }
  }
  if (pos + 1 < line.size() && line[pos + 1] == '=') {
    return std::string_view::npos;
  }
  return pos;
}

void reportStrictError(RuntimeInterpreter *interp, std::string_view where, std::string_view text) {
  if (interp == nullptr || !interp->strict || interp->hadError) {
    return;
  }
  interp->hadError = true;
  std::fprintf(stderr, "Interpreter strict mode: unsupported %.*s: %.*s\n",
    static_cast<int>(where.size()), where.data(),
    static_cast<int>(text.size()), text.data());
}

auto shouldAbort(RuntimeInterpreter *interp) -> bool {
  return interp != nullptr && interp->strict && interp->hadError;
}

auto isLoopHeader(std::string_view line) -> bool {
  return trim(line) == "loop:";
}

auto hasBalancedParens(std::string_view text) -> bool {
  int depth = 0;
  for (char ch : text) {
    if (ch == '(') {
      depth += 1;
    } else if (ch == ')') {
      depth -= 1;
      if (depth < 0) {
        return false;
      }
    }
  }
  return depth == 0;
}

auto trimWrappedParens(std::string_view text) -> std::string_view {
  auto out = trim(text);
  while (out.size() >= 2 && out.front() == '(' && out.back() == ')') {
    int depth = 0;
    bool wrapsAll = true;
    for (std::size_t i = 0; i < out.size(); ++i) {
      const char ch = out[i];
      if (ch == '(') {
        depth += 1;
      } else if (ch == ')') {
        depth -= 1;
        if (depth == 0 && i + 1 < out.size()) {
          wrapsAll = false;
          break;
        }
      }
      if (depth < 0) {
        wrapsAll = false;
        break;
      }
    }
    if (!wrapsAll || depth != 0) {
      break;
    }
    out = trim(out.substr(1, out.size() - 2));
  }
  return out;
}

auto isWordBoundaryOrEdge(std::string_view text, std::size_t index) -> bool {
  if (index >= text.size()) {
    return true;
  }
  const unsigned char ch = static_cast<unsigned char>(text[index]);
  return std::isalnum(ch) == 0 && text[index] != '_';
}

auto findTopLevelKeyword(std::string_view text, std::string_view keyword) -> std::size_t {
  const auto expr = trimWrappedParens(text);
  const bool needsBoundary = !keyword.empty() && (std::isalpha(static_cast<unsigned char>(keyword.front())) != 0 || keyword.front() == '_');
  int depth = 0;
  for (std::size_t i = 0; i < expr.size(); ++i) {
    const char ch = expr[i];
    if (ch == '(') {
      depth += 1;
      continue;
    }
    if (ch == ')') {
      depth -= 1;
      continue;
    }
    if (depth != 0) {
      continue;
    }
    if (i + keyword.size() > expr.size()) {
      continue;
    }
    if (expr.substr(i, keyword.size()) != keyword) {
      continue;
    }
    if (needsBoundary) {
      if (!isWordBoundaryOrEdge(expr, i == 0 ? expr.size() : i - 1)) {
        continue;
      }
      if (!isWordBoundaryOrEdge(expr, i + keyword.size())) {
        continue;
      }
    }
    return i;
  }
  return std::string_view::npos;
}

auto evalConditionFromText(std::string_view conditionText, RuntimeInterpreter *interp) -> bool;

auto evalExprFromText(std::string_view exprText, RuntimeInterpreter *interp) -> int {
  const auto expr = trimWrappedParens(exprText);
  if (expr.empty()) {
    return 0;
  }
  if (expr == "true") {
    return 1;
  }
  if (expr == "false" || expr == "null") {
    return 0;
  }
  if (startsWith(expr, "not ")) {
    return evalConditionFromText(trim(expr.substr(4)), interp) ? 0 : 1;
  }
  if (!hasBalancedParens(expr)) {
    reportStrictError(interp, "expression", expr);
    return 0;
  }
  if (interp != nullptr && interp->strict) {
    if (
      expr.find('"') != std::string_view::npos ||
      expr.find('[') != std::string_view::npos ||
      expr.find(']') != std::string_view::npos ||
      expr.find('{') != std::string_view::npos ||
      expr.find('}') != std::string_view::npos ||
      expr.find('.') != std::string_view::npos ||
      expr.find(',') != std::string_view::npos
    ) {
      reportStrictError(interp, "expression", expr);
      return 0;
    }
    for (std::size_t i = 0; i < expr.size(); ++i) {
      if (expr[i] != '(') {
        continue;
      }
      std::size_t left = i;
      while (left > 0 && (expr[left - 1] == ' ' || expr[left - 1] == '\t')) {
        left -= 1;
      }
      if (left == 0) {
        continue;
      }
      const char prev = expr[left - 1];
      const bool likelyCall = (std::isalnum(static_cast<unsigned char>(prev)) != 0) || prev == '_' || prev == ')';
      if (likelyCall) {
        reportStrictError(interp, "call-like expression", expr);
        return 0;
      }
    }
  }
  std::string owned {expr};
  auto tokens = tokenizeExprSource(owned.c_str());
  if (interp != nullptr && interp->strict) {
    for (const auto &tok : tokens) {
      if (tok.kind == "INVALID") {
        reportStrictError(interp, "expression token", tok.text);
        return 0;
      }
    }
  }
  ExprParser parser {tokens};
  auto *node = parser.parseExpr();
  const int value = evalExprWithEnv(node, interp);
  std::unordered_set<AstNode *> seen {};
  freeAstNodeRecursive(node, seen);
  return value;
}

auto evalConditionFromText(std::string_view conditionText, RuntimeInterpreter *interp) -> bool {
  const auto expr = trimWrappedParens(conditionText);
  if (expr.empty()) {
    return false;
  }
  if (expr == "true") {
    return true;
  }
  if (expr == "false" || expr == "null") {
    return false;
  }
  if (startsWith(expr, "not ")) {
    return !evalConditionFromText(trim(expr.substr(4)), interp);
  }
  const auto orPos = findTopLevelKeyword(expr, "or");
  if (orPos != std::string_view::npos) {
    const auto lhs = trim(expr.substr(0, orPos));
    const auto rhs = trim(expr.substr(orPos + 2));
    return evalConditionFromText(lhs, interp) || evalConditionFromText(rhs, interp);
  }
  const auto andPos = findTopLevelKeyword(expr, "and");
  if (andPos != std::string_view::npos) {
    const auto lhs = trim(expr.substr(0, andPos));
    const auto rhs = trim(expr.substr(andPos + 3));
    return evalConditionFromText(lhs, interp) && evalConditionFromText(rhs, interp);
  }

  const std::array<std::string_view, 6> ops {"==", "!=", ">=", "<=", ">", "<"};
  for (const auto op : ops) {
    const auto pos = findTopLevelKeyword(expr, op);
    if (pos == std::string_view::npos) {
      continue;
    }
    const auto lhs = trim(expr.substr(0, pos));
    const auto rhs = trim(expr.substr(pos + op.size()));
    const int left = evalExprFromText(lhs, interp);
    const int right = evalExprFromText(rhs, interp);
    if (op == "==") return left == right;
    if (op == "!=") return left != right;
    if (op == ">=") return left >= right;
    if (op == "<=") return left <= right;
    if (op == ">") return left > right;
    if (op == "<") return left < right;
  }

  return evalExprFromText(expr, interp) != 0;
}

auto execStmtFromText(std::string_view stmtText, RuntimeInterpreter *interp) -> int {
  const auto stmt = trim(stmtText);
  if (stmt.empty()) {
    return 0;
  }
  std::string owned {stmt};
  auto tokens = tokenizeExprSource(owned.c_str());
  ExprParser parser {tokens};
  auto *node = parser.parseStatement();
  const int value = execStmtWithEnv(node, interp);
  std::unordered_set<AstNode *> seen {};
  freeAstNodeRecursive(node, seen);
  return value;
}

auto execLetFromText(std::string_view line, RuntimeInterpreter *interp) -> std::optional<int> {
  const auto text = trim(line);
  if (!startsWith(text, "let ")) {
    return std::nullopt;
  }
  const auto body = trim(text.substr(4));
  const auto eqPos = findAssignmentEquals(body);
  if (eqPos == std::string_view::npos) {
    return std::nullopt;
  }

  auto lhs = trim(body.substr(0, eqPos));
  const auto rhs = trim(body.substr(eqPos + 1));
  const auto colonPos = lhs.find(':');
  if (colonPos != std::string_view::npos) {
    lhs = trim(lhs.substr(0, colonPos));
  }
  if (!isIdentifierText(lhs)) {
    return std::nullopt;
  }

  const int value = evalExprFromText(rhs, interp);
  if (interp != nullptr) {
    interp->env[std::string(lhs)] = value;
  }
  return value;
}

auto execAssignFromText(std::string_view line, RuntimeInterpreter *interp) -> std::optional<int> {
  const auto text = trim(line);
  const auto eqPos = findAssignmentEquals(text);
  if (eqPos == std::string_view::npos) {
    return std::nullopt;
  }
  const auto lhs = trim(text.substr(0, eqPos));
  const auto rhs = trim(text.substr(eqPos + 1));
  if (!isIdentifierText(lhs)) {
    return std::nullopt;
  }
  const int value = evalExprFromText(rhs, interp);
  if (interp != nullptr) {
    interp->env[std::string(lhs)] = value;
  }
  return value;
}

struct BlockExecResult {
  bool returned {false};
  int value {0};
  std::size_t nextIndex {0};
};

auto skipIndentedBlock(const std::vector<ProgramLine> &lines, std::size_t start, int indent) -> std::size_t {
  std::size_t i = start;
  while (i < lines.size() && lines[i].indent >= indent) {
    i += 1;
  }
  return i;
}

auto execIndentedBlock(const std::vector<ProgramLine> &lines, std::size_t start, int indent, RuntimeInterpreter *interp)
  -> BlockExecResult {
  constexpr int kLoopGuardMaxIterations = 1000000;
  std::size_t i = start;
  int lastValue = 0;
  while (i < lines.size()) {
    if (lines[i].indent < indent) {
      break;
    }
    if (lines[i].indent > indent) {
      i += 1;
      continue;
    }
    const auto line = trim(lines[i].text);
    if (line.empty() || startsWith(line, "#") || startsWith(line, "//")) {
      i += 1;
      continue;
    }

    if (startsWith(line, "return")) {
      const auto expr = trim(line.substr(6));
      if (expr.empty()) {
        return BlockExecResult {.returned = true, .value = lastValue, .nextIndex = i + 1};
      }
      const int retValue = evalExprFromText(expr, interp);
      if (shouldAbort(interp)) {
        return BlockExecResult {.returned = true, .value = -1, .nextIndex = i + 1};
      }
      return BlockExecResult {.returned = true, .value = retValue, .nextIndex = i + 1};
    }

    if (auto ifExpr = parseControlExpr(line, "if"); ifExpr.has_value()) {
      const std::size_t thenStart = i + 1;
      int thenIndent = -1;
      if (thenStart < lines.size() && lines[thenStart].indent > indent) {
        thenIndent = lines[thenStart].indent;
      }
      std::size_t afterThen = thenStart;
      if (thenIndent > indent) {
        afterThen = skipIndentedBlock(lines, thenStart, thenIndent);
      }

      bool hasElse = false;
      std::size_t elseStart = afterThen;
      int elseIndent = -1;
      std::size_t cursor = afterThen;
      if (cursor < lines.size() && lines[cursor].indent == indent && isElseHeader(lines[cursor].text)) {
        hasElse = true;
        elseStart = cursor + 1;
        if (elseStart < lines.size() && lines[elseStart].indent > indent) {
          elseIndent = lines[elseStart].indent;
          cursor = skipIndentedBlock(lines, elseStart, elseIndent);
        } else {
          cursor = elseStart;
        }
      }

      if (evalConditionFromText(*ifExpr, interp)) {
        if (shouldAbort(interp)) {
          return BlockExecResult {.returned = true, .value = -1, .nextIndex = i + 1};
        }
        if (thenIndent > indent) {
          auto thenResult = execIndentedBlock(lines, thenStart, thenIndent, interp);
          if (thenResult.returned) {
            return thenResult;
          }
          lastValue = thenResult.value;
        }
      } else if (hasElse && elseIndent > indent) {
        auto elseResult = execIndentedBlock(lines, elseStart, elseIndent, interp);
        if (elseResult.returned) {
          return elseResult;
        }
        lastValue = elseResult.value;
      }

      i = cursor;
      continue;
    }

    if (isLoopHeader(line)) {
      const std::size_t bodyStart = i + 1;
      int bodyIndent = -1;
      if (bodyStart < lines.size() && lines[bodyStart].indent > indent) {
        bodyIndent = lines[bodyStart].indent;
      }
      std::size_t afterBody = bodyStart;
      if (bodyIndent > indent) {
        afterBody = skipIndentedBlock(lines, bodyStart, bodyIndent);
      }

      int iterations = 0;
      if (bodyIndent > indent) {
        while (true) {
          iterations += 1;
          if (iterations > kLoopGuardMaxIterations) {
            reportStrictError(interp, "loop", line);
            if (interp != nullptr && !interp->strict) {
              break;
            }
            return BlockExecResult {.returned = true, .value = -1, .nextIndex = i + 1};
          }
          auto bodyResult = execIndentedBlock(lines, bodyStart, bodyIndent, interp);
          if (bodyResult.returned) {
            return bodyResult;
          }
          lastValue = bodyResult.value;
          if (shouldAbort(interp)) {
            return BlockExecResult {.returned = true, .value = -1, .nextIndex = i + 1};
          }
        }
      }
      i = afterBody;
      continue;
    }

    if (auto whileExpr = parseControlExpr(line, "while"); whileExpr.has_value()) {
      const std::size_t bodyStart = i + 1;
      int bodyIndent = -1;
      if (bodyStart < lines.size() && lines[bodyStart].indent > indent) {
        bodyIndent = lines[bodyStart].indent;
      }
      std::size_t afterBody = bodyStart;
      if (bodyIndent > indent) {
        afterBody = skipIndentedBlock(lines, bodyStart, bodyIndent);
      }

      if (bodyIndent > indent) {
        int iterations = 0;
        while (evalConditionFromText(*whileExpr, interp)) {
          if (shouldAbort(interp)) {
            return BlockExecResult {.returned = true, .value = -1, .nextIndex = i + 1};
          }
          iterations += 1;
          if (iterations > kLoopGuardMaxIterations) {
            reportStrictError(interp, "while", line);
            if (interp != nullptr && !interp->strict) {
              break;
            }
            return BlockExecResult {.returned = true, .value = -1, .nextIndex = i + 1};
          }
          auto bodyResult = execIndentedBlock(lines, bodyStart, bodyIndent, interp);
          if (bodyResult.returned) {
            return bodyResult;
          }
          lastValue = bodyResult.value;
        }
      }

      i = afterBody;
      continue;
    }

    if (auto text = parsePrintStringArg(line); text.has_value()) {
      std::puts(text->c_str());
      i += 1;
      continue;
    }

    if (auto printArg = parsePrintArgument(line); printArg.has_value()) {
      const int value = evalExprFromText(*printArg, interp);
      if (shouldAbort(interp)) {
        return BlockExecResult {.returned = true, .value = -1, .nextIndex = i + 1};
      }
      std::printf("%d\n", value);
      lastValue = value;
      i += 1;
      continue;
    }

    if (auto letValue = execLetFromText(line, interp); letValue.has_value()) {
      if (shouldAbort(interp)) {
        return BlockExecResult {.returned = true, .value = -1, .nextIndex = i + 1};
      }
      lastValue = *letValue;
      i += 1;
      continue;
    }

    if (auto assignValue = execAssignFromText(line, interp); assignValue.has_value()) {
      if (shouldAbort(interp)) {
        return BlockExecResult {.returned = true, .value = -1, .nextIndex = i + 1};
      }
      lastValue = *assignValue;
      i += 1;
      continue;
    }

    if (interp != nullptr && interp->strict) {
      reportStrictError(interp, "statement", line);
      return BlockExecResult {.returned = true, .value = -1, .nextIndex = i + 1};
    }

    lastValue = evalExprFromText(line, interp);
    if (shouldAbort(interp)) {
      return BlockExecResult {.returned = true, .value = -1, .nextIndex = i + 1};
    }
    i += 1;
  }
  return BlockExecResult {.returned = false, .value = lastValue, .nextIndex = i};
}

auto execProgramWithEnv(AstNode *node, RuntimeInterpreter *interp) -> int {
  if (node == nullptr) {
    return 0;
  }
  if (interp != nullptr && interp->strict) {
    if (!node->hasMain) {
      reportStrictError(interp, "program", "missing func main() block");
      return -1;
    }
    if (node->hasUnsupportedTopLevel) {
      const auto text = node->firstUnsupportedTopLevel.empty()
        ? std::string_view {"<unknown>"}
        : std::string_view {node->firstUnsupportedTopLevel};
      reportStrictError(interp, "top-level statement", text);
      return -1;
    }
  }
  auto result = execIndentedBlock(node->lines, 0, 0, interp);
  if (shouldAbort(interp)) {
    return -1;
  }
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
  if (node->kind == "Program") {
    return execProgramWithEnv(node, interp);
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

const char *__io_read_line(const char *prompt) {
  if (prompt != nullptr && *prompt != '\0') {
    std::fputs(prompt, stdout);
    std::fflush(stdout);
  }
  std::string line {};
  if (!std::getline(std::cin, line)) {
    std::cin.clear();
    return makeManagedCString("");
  }
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  return makeManagedString(line);
}

const char *__io_read_all() {
  std::string data {};
  data.assign(
    std::istreambuf_iterator<char>(std::cin),
    std::istreambuf_iterator<char>()
  );
  if (std::cin.bad()) {
    std::cin.clear();
    return makeManagedCString("");
  }
  std::cin.clear();
  return makeManagedString(data);
}

int __thg_input_i32() {
  const char *line = __io_read_line("");
  if (line == nullptr) {
    return 0;
  }
  const std::string raw = line;
  std::size_t start = 0;
  while (start < raw.size() && std::isspace(static_cast<unsigned char>(raw[start])) != 0) {
    ++start;
  }
  if (start >= raw.size()) {
    return 0;
  }

  errno = 0;
  char *end = nullptr;
  const long long parsed = std::strtoll(raw.c_str() + start, &end, 10);
  if (end == raw.c_str() + start) {
    return 0;
  }
  while (end != nullptr && *end != '\0' && std::isspace(static_cast<unsigned char>(*end)) != 0) {
    ++end;
  }
  if (end != nullptr && *end != '\0') {
    return 0;
  }
  if (errno == ERANGE || parsed > static_cast<long long>(std::numeric_limits<int>::max())) {
    return std::numeric_limits<int>::max();
  }
  if (parsed < static_cast<long long>(std::numeric_limits<int>::min())) {
    return std::numeric_limits<int>::min();
  }
  return static_cast<int>(parsed);
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

int __fs_move(const char *src, const char *dst) {
  if (src == nullptr || *src == '\0' || dst == nullptr || *dst == '\0') {
    return 0;
  }
  std::error_code ec {};
  const std::filesystem::path dstPath {dst};
  const auto parent = dstPath.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      return 0;
    }
  }
  std::filesystem::rename(src, dst, ec);
  if (!ec) {
    return 1;
  }
  ec.clear();
  std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    return 0;
  }
  ec.clear();
  std::filesystem::remove(src, ec);
  return ec ? 0 : 1;
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

int __process_exec_argv(const char *program, const char *argsText, const char *stdoutPath, const char *stderrPath, int timeoutMs) {
  const std::string exe = cstrOrEmpty(program);
  if (exe.empty()) {
    return -1;
  }
  const auto args = splitArgsText(argsText);
  return runProcessArgvMaybeTimed(exe, args, cstrOrEmpty(stdoutPath), cstrOrEmpty(stderrPath), timeoutMs);
}

int __process_run(const char *command) {
  if (command == nullptr) {
    return 0;
  }
  const std::string text = command;
  if (text.empty()) {
    return 0;
  }
#if defined(_WIN32)
  if (needsWindowsShell(text)) {
    return runCommandMaybeTimed(text, 0);
  }
  const int directRc = runDirectCommandMaybeTimed(text, 0);
  if (directRc == -1) {
    return runCommandMaybeTimed(text, 0);
  }
  return directRc;
#else
  return std::system(text.c_str());
#endif
}

int __process_open_path(const char *path) {
  if (path == nullptr || *path == '\0') {
    return 1;
  }
#if defined(_WIN32)
  const std::string script = std::string("Start-Process -LiteralPath ")
    + quotePowerShellLiteral(path);
  const std::string cmd = std::string("powershell -NoProfile -ExecutionPolicy Bypass -Command ")
    + quoteShellArg(script);
  return runDirectCommandMaybeTimed(cmd, 15000);
#elif defined(__APPLE__)
  const std::string cmd = std::string("open ") + quoteShellArg(path);
  return std::system(cmd.c_str());
#else
  const std::string cmd = std::string("xdg-open ") + quoteShellArg(path) + " >/dev/null 2>&1";
  return std::system(cmd.c_str());
#endif
}

int __http_download_file(const char *url, const char *outPath) {
  if (url == nullptr || *url == '\0' || outPath == nullptr || *outPath == '\0') {
    return 1;
  }
  std::error_code ec {};
  const std::filesystem::path outPathObj {outPath};
  const auto parent = outPathObj.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      return 1;
    }
  }
#if defined(_WIN32)
  std::string script = "$ProgressPreference='SilentlyContinue';";
  script += "Invoke-WebRequest -UseBasicParsing -Uri ";
  script += quotePowerShellLiteral(url);
  script += " -OutFile ";
  script += quotePowerShellLiteral(outPath);
  const std::string cmd = std::string("powershell -NoProfile -ExecutionPolicy Bypass -Command ")
    + quoteShellArg(script);
  return runDirectCommandMaybeTimed(cmd, 0);
#else
  const std::string cmd = std::string("curl -fL --retry 2 -o ")
    + quoteShellArg(outPath)
    + " "
    + quoteShellArg(url);
  return std::system(cmd.c_str());
#endif
}

int __archive_unzip(const char *zipPath, const char *outDir) {
  if (zipPath == nullptr || *zipPath == '\0' || outDir == nullptr || *outDir == '\0') {
    return 1;
  }
  std::error_code ec {};
  std::filesystem::create_directories(outDir, ec);
  if (ec) {
    return 1;
  }
#if defined(_WIN32)
  std::string script = "Expand-Archive -Force -LiteralPath ";
  script += quotePowerShellLiteral(zipPath);
  script += " -DestinationPath ";
  script += quotePowerShellLiteral(outDir);
  const std::string powershellCmd = std::string("powershell -NoProfile -ExecutionPolicy Bypass -Command ")
    + quoteShellArg(script);
  const int powershellRc = runDirectCommandMaybeTimed(powershellCmd, 0);
  if (powershellRc == 0) {
    return 0;
  }
  const std::string tarCmd = std::string("tar -xf ")
    + quoteShellArg(zipPath)
    + " -C "
    + quoteShellArg(outDir);
  return runDirectCommandMaybeTimed(tarCmd, 0);
#else
  const std::string unzipCmd = std::string("unzip -o ")
    + quoteShellArg(zipPath)
    + " -d "
    + quoteShellArg(outDir);
  const int unzipRc = std::system(unzipCmd.c_str());
  if (unzipRc == 0) {
    return 0;
  }
  const std::string tarCmd = std::string("tar -xf ")
    + quoteShellArg(zipPath)
    + " -C "
    + quoteShellArg(outDir);
  return std::system(tarCmd.c_str());
#endif
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

const char *__thg_codegen_emit_llvm_from_source(const char *source, const char *module_name);

static auto cliProbeCommand(const std::string &bin) -> int {
#if defined(_WIN32)
  const std::string cmd = quoteShellArg(bin) + " --version >nul 2>nul";
#else
  const std::string cmd = quoteShellArg(bin) + " --version >/dev/null 2>&1";
#endif
  return std::system(cmd.c_str());
}

static auto cliIsWindows() -> bool {
#if defined(_WIN32)
  return true;
#else
  return false;
#endif
}

static auto cliIsMacos() -> bool {
#if defined(__APPLE__)
  return true;
#else
  return false;
#endif
}

static auto cliDetectClang() -> std::string {
  const std::vector<std::string> candidates {
    "clang",
    "C:\\Program Files\\LLVM\\bin\\clang.exe",
    "C:\\Progra~1\\LLVM\\bin\\clang.exe",
    "llvm/clang+llvm-21.1.8-x86_64-pc-windows-msvc/bin/clang.exe",
    "/usr/bin/clang",
    "/usr/local/bin/clang",
  };
  for (const auto &candidate : candidates) {
    if (cliProbeCommand(candidate) == 0) {
      return candidate;
    }
  }
  return "";
}

static auto cliDetectLinker() -> std::string {
  if (cliIsWindows()) {
    return cliDetectClang();
  }
  const std::vector<std::string> candidates {
    "clang++",
    "/usr/bin/clang++",
    "/usr/local/bin/clang++",
  };
  for (const auto &candidate : candidates) {
    if (cliProbeCommand(candidate) == 0) {
      return candidate;
    }
  }
  return cliDetectClang();
}

static auto cliDetectRuntimeLib() -> std::string {
  const std::vector<std::string> candidates {
    "thag_runtime.lib",
    "runtime/build/thag_runtime.lib",
    "runtime/build/Release/thag_runtime.lib",
    "build/thag_runtime.lib",
    "libthag_runtime.a",
    "runtime/build/libthag_runtime.a",
    "build/libthag_runtime.a",
  };
  for (const auto &candidate : candidates) {
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return "";
}

static auto cliReadText(const std::string &path) -> std::string {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return "";
  }
  std::string data {
    std::istreambuf_iterator<char>(in),
    std::istreambuf_iterator<char>()
  };
  return data;
}

static auto cliWriteText(const std::string &path, const std::string &text) -> bool {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }
  out << text;
  return out.good();
}

static auto cliBasenameNoExt(const std::string &path) -> std::string {
  std::filesystem::path p {path};
  auto stem = p.stem().string();
  if (stem.empty()) {
    stem = "out";
  }
  return stem;
}

static auto cliHasSpace(const std::string &text) -> bool {
  return text.find(' ') != std::string::npos;
}

static auto cliIntentModeValid(const std::string &mode) -> bool {
  return mode == "off" || mode == "min" || mode == "max";
}

static auto cliIntentFallbackValid(const std::string &mode) -> bool {
  return mode == "deny" || mode == "allow";
}

static auto cliIntentRuleFamily(std::string_view ruleId) -> std::string {
  constexpr std::string_view prefix = "rule.";
  if (!startsWith(ruleId, prefix)) {
    return "misc";
  }
  const auto rem = ruleId.substr(prefix.size());
  const auto dot = rem.find('.');
  if (dot == std::string_view::npos || dot == 0) {
    return "misc";
  }
  return std::string(rem.substr(0, dot));
}

static auto cliIntentParseUnsigned(std::string_view text, std::size_t *out) -> bool {
  if (out == nullptr) {
    return false;
  }
  const auto valueText = trim(text);
  if (valueText.empty()) {
    return false;
  }
  std::size_t value = 0;
  for (char ch : valueText) {
    if (ch < '0' || ch > '9') {
      return false;
    }
    value = value * 10 + static_cast<std::size_t>(ch - '0');
  }
  *out = value;
  return true;
}

static auto cliIntentParseBoolEnabled(std::string_view text, bool defaultValue) -> bool {
  std::string value = std::string(trim(text));
  for (char &ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  if (value.empty()) {
    return defaultValue;
  }
  if (value == "0" || value == "false" || value == "off" || value == "no") {
    return false;
  }
  return true;
}

static auto cliIntentLoadRuleRegistry(CliIntentRuleRegistry *outRegistry) -> bool {
  if (outRegistry == nullptr) {
    return false;
  }
  *outRegistry = CliIntentRuleRegistry {};
  const char *envPath = std::getenv("THAG_INTENT_REGISTRY");
  if (envPath == nullptr || envPath[0] == '\0') {
    return false;
  }
  std::filesystem::path path {envPath};
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    return false;
  }

  CliIntentRuleRegistry registry {};
  registry.enabled = true;
  registry.sourcePath = path.string();

  std::string line {};
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const auto text = std::string(trim(line));
    if (text.empty() || startsWith(text, "#")) {
      continue;
    }
    if (startsWith(text, "enabled=")) {
      registry.enabled = cliIntentParseBoolEnabled(std::string_view(text).substr(8), true);
      continue;
    }
    if (startsWith(text, "budget.total=")) {
      std::size_t parsed = 0;
      if (cliIntentParseUnsigned(std::string_view(text).substr(13), &parsed)) {
        registry.totalBudget = parsed;
      }
      continue;
    }
    if (startsWith(text, "budget.family.")) {
      const std::size_t eq = text.find('=');
      if (eq == std::string::npos || eq <= 14 || (eq + 1) >= text.size()) {
        continue;
      }
      const auto family = std::string(trim(std::string_view(text).substr(14, eq - 14)));
      std::size_t parsed = 0;
      if (!family.empty() && cliIntentParseUnsigned(std::string_view(text).substr(eq + 1), &parsed)) {
        registry.familyBudget[family] = parsed;
      }
      continue;
    }
    if (startsWith(text, "rule=")) {
      const auto id = std::string(trim(std::string_view(text).substr(5)));
      if (!id.empty()) {
        registry.allowedRules.insert(id);
      }
      continue;
    }
  }

  *outRegistry = std::move(registry);
  return true;
}

static auto cliIntentPolicyValid(const std::string &policy) -> bool {
  return policy.empty() || policy == "safe" || policy == "fast" || policy == "debug";
}

static auto cliIntentSupportedGoals() -> const std::vector<std::string> & {
  static const std::vector<std::string> goals {
    "auto_plan",
    "reduce_sum",
    "map_filter_reduce",
    "deduplicate_sorted",
    "binary_search",
    "binary_search_sorted",
    "lower_bound_sorted",
    "upper_bound_sorted",
    "count_less_sorted",
    "count_less_equal_sorted",
    "count_greater_sorted",
    "count_greater_equal_sorted",
    "count_equal_sorted",
    "count_not_equal_sorted",
    "count_range_sorted",
    "count_outside_range_sorted",
    "two_sum_sorted_exists",
    "string_contains",
    "dot_product",
    "polynomial_eval",
    "fibonacci_dp",
    "tribonacci_dp",
    "factorial_iterative",
    "power_fast",
    "gcd_euclid",
    "is_prime_fast",
    "count_divisors_sqrt",
    "interval_cover_greedy",
    "bit_peel_iterative",
    "sum_formula",
    "sum_squares_formula",
    "sum_cubes_formula",
    "sum_even_squares_formula",
    "sum_odd_squares_formula",
    "sum_even_cubes_formula",
    "sum_odd_cubes_formula",
    "sum_even_formula",
    "sum_odd_formula",
    "sort_ascending",
    "search_element",
    "sqrt_bounded_loop",
  };
  return goals;
}

static auto cliIntentSupportedGoalsCsv() -> const std::string & {
  static const std::string csv = []() {
    const auto &goals = cliIntentSupportedGoals();
    std::string out {};
    for (std::size_t i = 0; i < goals.size(); ++i) {
      if (i > 0) {
        out.push_back(',');
      }
      out += goals[i];
    }
    return out;
  }();
  return csv;
}

static auto cliIntentGoalSupported(const std::string &goal) -> bool {
  const auto &goals = cliIntentSupportedGoals();
  return std::find(goals.begin(), goals.end(), goal) != goals.end();
}

static auto cliIntentRuleIdFromStrategy(std::string_view rawStrategy) -> std::optional<std::string> {
  std::string strategy = std::string(trim(rawStrategy));
  for (char &ch : strategy) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  if (strategy.empty()) {
    return std::nullopt;
  }
  if (strategy == "dp.fibonacci.iterative" || strategy == "dp.fib.v1") {
    return std::string("rule.fibonacci_dp.iterative.v1");
  }
  if (strategy == "dp.tribonacci.iterative" || strategy == "dp.trib.v1") {
    return std::string("rule.dp.trib.iterative.v1");
  }
  if (strategy == "math.factorial.iterative" || strategy == "math.factorial.loop.v1") {
    return std::string("rule.math.factorial.loop.v1");
  }
  if (strategy == "math.pow.binary_exp" || strategy == "pow.binary_exp" || strategy == "math.pow.fast.v1") {
    return std::string("rule.math.pow.binary_exp.v1");
  }
  if (strategy == "math.gcd.euclid" || strategy == "math.gcd.modulo.v1") {
    return std::string("rule.math.gcd.euclid.v1");
  }
  if (strategy == "number.prime.sqrt" || strategy == "number.prime.sqrt.v1") {
    return std::string("rule.number.prime.sqrt.v1");
  }
  if (strategy == "number.divisors.sqrt" || strategy == "number.divisors.sqrt.v1") {
    return std::string("rule.number.divisors.sqrt.v1");
  }
  if (strategy == "greedy.sweep.v1" || strategy == "greedy.sweep.interval_cover.v1"
      || strategy == "greedy.interval_cover.v1") {
    return std::string("rule.greedy.interval_cover.v1");
  }
  if (strategy == "search.binary.v1" || strategy == "search.binary.sorted.v1") {
    return std::string("rule.search.binary.sorted.v1");
  }
  if (strategy == "search.lower_bound.v1" || strategy == "search.lower_bound.sorted.v1") {
    return std::string("rule.search.lower_bound.sorted.v1");
  }
  if (strategy == "search.upper_bound.v1" || strategy == "search.upper_bound.sorted.v1") {
    return std::string("rule.search.upper_bound.sorted.v1");
  }
  if (strategy == "search.count_less.v1" || strategy == "search.count_less.sorted.v1") {
    return std::string("rule.search.count_less.sorted.v1");
  }
  if (strategy == "search.count_less_equal.v1" || strategy == "search.count_less_equal.sorted.v1"
      || strategy == "search.count_le.v1") {
    return std::string("rule.search.count_less_equal.sorted.v1");
  }
  if (strategy == "search.count_greater.v1" || strategy == "search.count_greater.sorted.v1"
      || strategy == "search.count_gt.v1") {
    return std::string("rule.search.count_greater.sorted.v1");
  }
  if (strategy == "search.count_greater_equal.v1" || strategy == "search.count_greater_equal.sorted.v1"
      || strategy == "search.count_ge.v1") {
    return std::string("rule.search.count_greater_equal.sorted.v1");
  }
  if (strategy == "search.count_equal.v1" || strategy == "search.count_equal.sorted.v1") {
    return std::string("rule.search.count_equal.sorted.v1");
  }
  if (strategy == "search.count_not_equal.v1" || strategy == "search.count_not_equal.sorted.v1"
      || strategy == "search.count_ne.v1") {
    return std::string("rule.search.count_not_equal.sorted.v1");
  }
  if (strategy == "search.count_range.v1" || strategy == "search.count_range.sorted.v1") {
    return std::string("rule.search.count_range.sorted.v1");
  }
  if (strategy == "search.count_outside_range.v1" || strategy == "search.count_outside_range.sorted.v1") {
    return std::string("rule.search.count_outside_range.sorted.v1");
  }
  if (strategy == "search.two_sum.v1" || strategy == "search.two_sum.sorted.v1") {
    return std::string("rule.search.two_sum.sorted.v1");
  }
  if (strategy == "number.bit_peel.iterative" || strategy == "number.bit_peel.fold.v1") {
    return std::string("rule.number.bit_peel.iterative.v1");
  }
  if (strategy == "math.sum.formula.v1") {
    return std::string("rule.math.sum.formula.v1");
  }
  if (strategy == "math.sum_squares.formula.v1") {
    return std::string("rule.math.sum_squares.formula.v1");
  }
  if (strategy == "math.sum_cubes.formula.v1") {
    return std::string("rule.math.sum_cubes.formula.v1");
  }
  if (strategy == "math.sum_even_squares.formula.v1") {
    return std::string("rule.math.sum_even_squares.formula.v1");
  }
  if (strategy == "math.sum_odd_squares.formula.v1") {
    return std::string("rule.math.sum_odd_squares.formula.v1");
  }
  if (strategy == "math.sum_even_cubes.formula.v1") {
    return std::string("rule.math.sum_even_cubes.formula.v1");
  }
  if (strategy == "math.sum_odd_cubes.formula.v1") {
    return std::string("rule.math.sum_odd_cubes.formula.v1");
  }
  if (strategy == "math.sum_even.formula.v1") {
    return std::string("rule.math.sum_even.formula.v1");
  }
  if (strategy == "math.sum_odd.formula.v1") {
    return std::string("rule.math.sum_odd.formula.v1");
  }
  if (strategy == "math.sqrt.newton.v1") {
    return std::string("rule.sqrt_bounded_loop.mul_guard.v2");
  }
  if (strategy == "search.identity.bounds.v1") {
    return std::string("rule.search_element.binary_iter.v2");
  }
  return std::nullopt;
}

static auto cliIntentHashHex(const std::string &text) -> std::string {
  std::uint64_t hash = 1469598103934665603ull;
  for (unsigned char ch : text) {
    hash ^= static_cast<std::uint64_t>(ch);
    hash *= 1099511628211ull;
  }
  char out[17] {};
  std::snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(hash));
  return std::string(out);
}

static auto cliIntentDigest(const std::string &text) -> std::string {
  return "sha256:fnv1a64-" + cliIntentHashHex(text);
}

static auto cliJsonEscape(const std::string &text) -> std::string {
  std::string out {};
  out.reserve(text.size() + 8);
  for (unsigned char ch : text) {
    if (ch == '\\') {
      out += "\\\\";
    } else if (ch == '"') {
      out += "\\\"";
    } else if (ch == '\n') {
      out += "\\n";
    } else if (ch == '\r') {
      out += "\\r";
    } else if (ch == '\t') {
      out += "\\t";
    } else if (ch < 32) {
      char buf[7] {};
      std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(ch));
      out += buf;
    } else {
      out.push_back(static_cast<char>(ch));
    }
  }
  return out;
}

static auto cliIntentTargetFingerprint() -> std::string {
#if defined(_M_X64) || defined(__x86_64__)
  const std::string arch = "x86_64";
#elif defined(_M_IX86) || defined(__i386__)
  const std::string arch = "x86";
#elif defined(_M_ARM64) || defined(__aarch64__)
  const std::string arch = "aarch64";
#else
  const std::string arch = "unknown";
#endif

#if defined(_WIN32)
  return arch + "-pc-windows-msvc";
#elif defined(__APPLE__)
  return arch + "-apple-darwin";
#elif defined(__linux__)
  return arch + "-unknown-linux-gnu";
#else
  return arch + "-unknown";
#endif
}

static auto cliIntentToLower(std::string text) -> std::string {
  for (char &ch : text) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return text;
}

static void cliIntentApplyPolicyDefaults(
  const std::string &policy,
  bool explicitIntentMode,
  bool explicitFallbackMode,
  bool explicitStrictLock,
  std::string *intentMode,
  std::string *intentFallbackMode,
  bool *strictLock
) {
  if (intentMode == nullptr || intentFallbackMode == nullptr || strictLock == nullptr) {
    return;
  }
  if (policy.empty()) {
    return;
  }

  std::string defaultMode {"off"};
  std::string defaultFallback {"deny"};
  bool defaultStrictLock = false;
  if (policy == "safe") {
    defaultMode = "max";
    defaultFallback = "deny";
    defaultStrictLock = true;
  } else if (policy == "fast") {
    defaultMode = "min";
    defaultFallback = "allow";
    defaultStrictLock = false;
  } else if (policy == "debug") {
    defaultMode = "off";
    defaultFallback = "allow";
    defaultStrictLock = false;
  }

  if (!explicitIntentMode) {
    *intentMode = defaultMode;
  }
  if (!explicitFallbackMode) {
    *intentFallbackMode = defaultFallback;
  }
  if (!explicitStrictLock) {
    *strictLock = defaultStrictLock;
  }
}

static auto cliIntentNormalizeConstraint(const std::string &text) -> std::string {
  std::string out {};
  out.reserve(text.size());
  for (char ch : text) {
    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
      continue;
    }
    out.push_back(ch);
  }
  return cliIntentToLower(out);
}

static auto cliIntentConstraintDisablesIntent(const std::string &text) -> bool {
  const auto norm = cliIntentNormalizeConstraint(text);
  return norm == "intent==false" || norm == "intent=false" || norm == "intent==0" || norm == "intent=0";
}

static auto cliIntentTryParseFloat(const std::string &text, double *valueOut) -> bool {
  if (valueOut == nullptr || text.empty()) {
    return false;
  }
  char *endPtr = nullptr;
  const double value = std::strtod(text.c_str(), &endPtr);
  if (endPtr == text.c_str() || *endPtr != '\0') {
    return false;
  }
  *valueOut = value;
  return true;
}

static auto cliIntentComplexityRank(const std::string &complexityText) -> int {
  const std::string norm = cliIntentNormalizeConstraint(complexityText);
  if (norm == "o(1)") {
    return 0;
  }
  if (norm == "o(logn)" || norm == "o(log2n)") {
    return 1;
  }
  if (norm == "o(sqrtn)" || norm == "o(sqrt(n))") {
    return 2;
  }
  if (norm == "o(n)") {
    return 3;
  }
  if (norm == "o(nlogn)") {
    return 4;
  }
  if (norm == "o(n^2)" || norm == "o(n2)") {
    return 5;
  }
  return 99;
}

static auto cliIntentComplexityAtMost(const std::string &ruleComplexity, const std::string &limitComplexity) -> bool {
  return cliIntentComplexityRank(ruleComplexity) <= cliIntentComplexityRank(limitComplexity);
}

static void cliIntentCollectRulesForGoal(const std::string &goal, std::vector<IntentRuleInfo> *outRules) {
  if (outRules == nullptr) {
    return;
  }
  outRules->clear();

  auto add_rule = [&](const char *id, const char *complexity, bool deterministic, bool noHeapGrowth, bool parallelCapable, bool vectorizeCapable,
                      int timeRank, int memoryRank, double maxError) {
    IntentRuleInfo rule {};
    rule.id = id;
    rule.goal = goal;
    rule.complexity = complexity;
    rule.deterministic = deterministic;
    rule.noHeapGrowth = noHeapGrowth;
    rule.parallelCapable = parallelCapable;
    rule.vectorizeCapable = vectorizeCapable;
    rule.timeRank = timeRank;
    rule.memoryRank = memoryRank;
    rule.maxError = maxError;
    outRules->push_back(rule);
  };

  if (goal == "reduce_sum") {
    add_rule("rule.reduce_sum.scalar.v1", "O(n)", true, true, false, false, 3, 1, 0.0);
    add_rule("rule.reduce_sum.simd.v2", "O(n)", true, true, true, true, 1, 1, 0.0);
    add_rule("rule.reduce_sum.parallel.v3", "O(n)", false, true, true, true, 1, 2, 0.0);
    return;
  }
  if (goal == "map_filter_reduce") {
    add_rule("rule.map_filter_reduce.fused.v1", "O(n)", true, false, false, false, 3, 3, 0.0);
    add_rule("rule.map_filter_reduce.fused.v2", "O(n)", true, false, true, true, 2, 2, 0.0);
    add_rule("rule.map_filter_reduce.parallel.v3", "O(n)", false, false, true, true, 1, 3, 0.0);
    return;
  }
  if (goal == "deduplicate_sorted") {
    add_rule("rule.deduplicate_sorted.linear.v1", "O(n)", true, true, false, false, 2, 1, 0.0);
    add_rule("rule.deduplicate_sorted.branchless.v2", "O(n)", true, true, true, true, 1, 1, 0.0);
    return;
  }
  if (goal == "binary_search") {
    add_rule("rule.binary_search.classic.v1", "O(logn)", true, true, false, false, 2, 1, 0.0);
    add_rule("rule.binary_search.branchless.v2", "O(logn)", true, true, true, true, 1, 1, 0.0);
    return;
  }
  if (goal == "binary_search_sorted") {
    add_rule("rule.search.binary.sorted.v1", "O(logn)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "lower_bound_sorted") {
    add_rule("rule.search.lower_bound.sorted.v1", "O(logn)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "upper_bound_sorted") {
    add_rule("rule.search.upper_bound.sorted.v1", "O(logn)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "count_less_sorted") {
    add_rule("rule.search.count_less.sorted.v1", "O(logn)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "count_less_equal_sorted") {
    add_rule("rule.search.count_less_equal.sorted.v1", "O(logn)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "count_greater_sorted") {
    add_rule("rule.search.count_greater.sorted.v1", "O(logn)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "count_greater_equal_sorted") {
    add_rule("rule.search.count_greater_equal.sorted.v1", "O(logn)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "count_equal_sorted") {
    add_rule("rule.search.count_equal.sorted.v1", "O(logn)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "count_not_equal_sorted") {
    add_rule("rule.search.count_not_equal.sorted.v1", "O(logn)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "count_range_sorted") {
    add_rule("rule.search.count_range.sorted.v1", "O(logn)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "count_outside_range_sorted") {
    add_rule("rule.search.count_outside_range.sorted.v1", "O(logn)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "two_sum_sorted_exists") {
    add_rule("rule.search.two_sum.sorted.v1", "O(n)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "string_contains") {
    add_rule("rule.string_contains.scan.v1", "O(n)", true, true, false, false, 2, 1, 0.0);
    add_rule("rule.string_contains.twoway.v2", "O(n)", true, true, true, true, 1, 1, 0.0);
    return;
  }
  if (goal == "dot_product") {
    add_rule("rule.dot_product.scalar.v1", "O(n)", true, true, false, false, 3, 1, 0.0);
    add_rule("rule.dot_product.simd.v2", "O(n)", true, true, true, true, 1, 1, 1e-7);
    add_rule("rule.dot_product.fastmath.v3", "O(n)", false, true, true, true, 1, 1, 1e-4);
    return;
  }
  if (goal == "polynomial_eval") {
    add_rule("rule.polynomial_eval.horner.v1", "O(n)", true, true, false, false, 3, 1, 0.0);
    add_rule("rule.polynomial_eval.simd.v2", "O(n)", true, true, false, true, 1, 1, 1e-7);
    add_rule("rule.polynomial_eval.fastmath.v3", "O(n)", false, true, false, true, 1, 1, 1e-5);
    return;
  }
  if (goal == "fibonacci_dp") {
    add_rule("rule.fibonacci_dp.iterative.v1", "O(n)", true, true, false, false, 2, 1, 0.0);
    add_rule("rule.fibonacci_dp.iterative.v2", "O(n)", true, true, false, false, 1, 1, 0.0);
    add_rule("rule.fibonacci_dp.memoized.v3", "O(n)", true, false, false, false, 1, 3, 0.0);
    return;
  }
  if (goal == "tribonacci_dp") {
    add_rule("rule.dp.trib.iterative.v1", "O(n)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "factorial_iterative") {
    add_rule("rule.math.factorial.loop.v1", "O(n)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "power_fast") {
    add_rule("rule.math.pow.binary_exp.v1", "O(logn)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "gcd_euclid") {
    add_rule("rule.math.gcd.euclid.v1", "O(logn)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "is_prime_fast") {
    add_rule("rule.number.prime.sqrt.v1", "O(sqrt(n))", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "count_divisors_sqrt") {
    add_rule("rule.number.divisors.sqrt.v1", "O(sqrt(n))", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "interval_cover_greedy") {
    add_rule("rule.greedy.interval_cover.v1", "O(nlogn)", true, false, false, false, 1, 2, 0.0);
    return;
  }
  if (goal == "bit_peel_iterative") {
    add_rule("rule.number.bit_peel.iterative.v1", "O(logn)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "sum_formula") {
    add_rule("rule.math.sum.formula.v1", "O(1)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "sum_squares_formula") {
    add_rule("rule.math.sum_squares.formula.v1", "O(1)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "sum_cubes_formula") {
    add_rule("rule.math.sum_cubes.formula.v1", "O(1)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "sum_even_squares_formula") {
    add_rule("rule.math.sum_even_squares.formula.v1", "O(1)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "sum_odd_squares_formula") {
    add_rule("rule.math.sum_odd_squares.formula.v1", "O(1)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "sum_even_cubes_formula") {
    add_rule("rule.math.sum_even_cubes.formula.v1", "O(1)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "sum_odd_cubes_formula") {
    add_rule("rule.math.sum_odd_cubes.formula.v1", "O(1)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "sum_even_formula") {
    add_rule("rule.math.sum_even.formula.v1", "O(1)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "sum_odd_formula") {
    add_rule("rule.math.sum_odd.formula.v1", "O(1)", true, true, false, false, 1, 1, 0.0);
    return;
  }
  if (goal == "sort_ascending") {
    add_rule("rule.sort_ascending.quicksort.v1", "O(nlogn)", true, false, false, false, 2, 3, 0.0);
    add_rule("rule.sort_ascending.introsort.v2", "O(nlogn)", true, false, true, true, 1, 3, 0.0);
    return;
  }
  if (goal == "search_element") {
    add_rule("rule.search_element.binary_classic.v1", "O(logn)", true, true, false, false, 2, 1, 0.0);
    add_rule("rule.search_element.binary_iter.v2", "O(logn)", true, true, true, false, 1, 1, 0.0);
    return;
  }
  if (goal == "sqrt_bounded_loop") {
    add_rule("rule.sqrt_bounded_loop.div_guard.v1", "O(sqrt(n))", true, true, false, false, 2, 1, 0.0);
    add_rule("rule.sqrt_bounded_loop.mul_guard.v2", "O(sqrt(n))", true, true, false, false, 1, 1, 0.0);
    return;
  }
}

static auto cliIntentRuleFor(const std::string &goal, const std::string &mode) -> std::string {
  std::vector<IntentRuleInfo> rules {};
  cliIntentCollectRulesForGoal(goal, &rules);
  if (rules.empty()) {
    return "rule.unsupported";
  }
  if (mode == "max" && rules.size() > 1) {
    return rules[1].id;
  }
  return rules[0].id;
}

static auto cliIntentFindRuleById(
  const std::vector<IntentRuleInfo> &rules,
  const std::string &ruleId,
  IntentRuleInfo *outRule
) -> bool {
  if (outRule == nullptr) {
    return false;
  }
  for (const auto &rule : rules) {
    if (rule.id == ruleId) {
      *outRule = rule;
      return true;
    }
  }
  return false;
}

static auto cliIntentVerifyRuleConstraints(
  const IntentRuleInfo &rule,
  const IntentEntry &entry,
  std::string *reasonOut
) -> bool {
  if (entry.hasExamplesHeader && entry.examples.empty()) {
    if (reasonOut != nullptr) {
      *reasonOut = "examples section is empty";
    }
    return false;
  }

  for (const auto &example : entry.examples) {
    if (example.find("==") == std::string::npos) {
      if (reasonOut != nullptr) {
        *reasonOut = "example assertion must use == operator";
      }
      return false;
    }
  }

  for (const auto &rawConstraint : entry.constraints) {
    const auto norm = cliIntentNormalizeConstraint(rawConstraint);
    if (norm.empty()) {
      continue;
    }

    if (startsWith(norm, "deterministic==")) {
      const auto value = norm.substr(15);
      if (value == "true" || value == "1") {
        if (!rule.deterministic) {
          if (reasonOut != nullptr) {
            *reasonOut = "deterministic==true is violated";
          }
          return false;
        }
      } else if (value == "false" || value == "0") {
        if (rule.deterministic) {
          if (reasonOut != nullptr) {
            *reasonOut = "deterministic==false is violated";
          }
          return false;
        }
      } else {
        if (reasonOut != nullptr) {
          *reasonOut = "invalid deterministic constraint value";
        }
        return false;
      }
      continue;
    }

    if (startsWith(norm, "parallel==")) {
      const auto value = norm.substr(10);
      if ((value == "true" || value == "1") && !rule.parallelCapable) {
        if (reasonOut != nullptr) {
          *reasonOut = "parallel==true is violated";
        }
        return false;
      }
      continue;
    }

    if (startsWith(norm, "vectorize==")) {
      const auto value = norm.substr(11);
      if ((value == "true" || value == "1") && !rule.vectorizeCapable) {
        if (reasonOut != nullptr) {
          *reasonOut = "vectorize==true is violated";
        }
        return false;
      }
      if ((value == "false" || value == "0") && rule.vectorizeCapable) {
        if (reasonOut != nullptr) {
          *reasonOut = "vectorize==false is violated";
        }
        return false;
      }
      continue;
    }

    if (startsWith(norm, "no_heap_growth==")) {
      const auto value = norm.substr(16);
      if ((value == "true" || value == "1") && !rule.noHeapGrowth) {
        if (reasonOut != nullptr) {
          *reasonOut = "no_heap_growth==true is violated";
        }
        return false;
      }
      continue;
    }

    if (startsWith(norm, "stable==")) {
      const auto value = norm.substr(8);
      if ((value == "true" || value == "1") && !rule.deterministic) {
        if (reasonOut != nullptr) {
          *reasonOut = "stable==true is violated";
        }
        return false;
      }
      continue;
    }

    if (startsWith(norm, "time<=")) {
      const auto limit = norm.substr(6);
      if (!cliIntentComplexityAtMost(rule.complexity, limit)) {
        if (reasonOut != nullptr) {
          *reasonOut = "time constraint is violated";
        }
        return false;
      }
      continue;
    }

    if (startsWith(norm, "max_error<=")) {
      const auto boundText = norm.substr(11);
      double bound = 0.0;
      if (!cliIntentTryParseFloat(boundText, &bound)) {
        if (reasonOut != nullptr) {
          *reasonOut = "invalid max_error value";
        }
        return false;
      }
      if (rule.maxError > bound + 1e-15) {
        if (reasonOut != nullptr) {
          *reasonOut = "max_error constraint is violated";
        }
        return false;
      }
      continue;
    }

    if (reasonOut != nullptr) {
      *reasonOut = "unsupported constraint `" + rawConstraint + "`";
    }
    return false;
  }

  return true;
}

static auto cliIntentScoreRule(
  const IntentRuleInfo &rule,
  const IntentEntry &entry,
  const std::string &mode,
  const std::string &target
) -> int {
  int score = rule.timeRank * 100 + rule.memoryRank * 10;
  if (mode == "max" && rule.parallelCapable) {
    score -= 15;
  }
  if (mode == "max" && rule.vectorizeCapable) {
    score -= 10;
  }
  if (target.find("x86_64") != std::string::npos && rule.parallelCapable) {
    score -= 10;
  }
  if (target.find("x86_64") != std::string::npos && rule.vectorizeCapable) {
    score -= 10;
  }
  if (!rule.deterministic) {
    score += 50;
  }

  for (const auto &rawConstraint : entry.constraints) {
    const auto norm = cliIntentNormalizeConstraint(rawConstraint);
    if (startsWith(norm, "parallel==true")) {
      score += rule.parallelCapable ? -30 : 2000;
    } else if (startsWith(norm, "vectorize==true")) {
      score += rule.vectorizeCapable ? -25 : 2000;
    } else if (startsWith(norm, "deterministic==true")) {
      score += rule.deterministic ? -30 : 5000;
    } else if (startsWith(norm, "no_heap_growth==true")) {
      score += rule.noHeapGrowth ? -20 : 2500;
    } else if (startsWith(norm, "stable==true")) {
      score += rule.deterministic ? -20 : 2500;
    } else if (startsWith(norm, "time<=")) {
      const auto limit = norm.substr(6);
      score += cliIntentComplexityAtMost(rule.complexity, limit) ? -40 : 4000;
    } else if (startsWith(norm, "max_error<=")) {
      const auto boundText = norm.substr(11);
      double bound = 0.0;
      if (cliIntentTryParseFloat(boundText, &bound)) {
        score += rule.maxError <= bound ? -20 : 3000;
      } else {
        score += 3000;
      }
    } else {
      score += 6000;
    }
  }
  return score;
}

static auto cliIntentSelectPlanForEntry(
  const IntentEntry &entry,
  const std::string &mode,
  IntentPlan *planOut,
  std::string *errorOut
) -> bool {
  if (planOut == nullptr) {
    return false;
  }
  *planOut = IntentPlan {};
  planOut->intentId = entry.id;
  planOut->goal = entry.goal;

  if (!entry.intentEnabled) {
    std::string constraintBlob {};
    for (const auto &constraint : entry.constraints) {
      constraintBlob += constraint;
      constraintBlob.push_back('\n');
    }
    std::string verifyBlob {};
    verifyBlob += entry.goal;
    verifyBlob.push_back('|');
    verifyBlob += "rule.intent.off";
    verifyBlob.push_back('|');
    verifyBlob += cliIntentTargetFingerprint();
    verifyBlob.push_back('|');
    verifyBlob += constraintBlob;

    planOut->selectedRule = "rule.intent.off";
    planOut->verified = true;
    planOut->verifyReason = "intent-disabled";
    planOut->candidateCount = 0;
    planOut->constraintsDigest = cliIntentDigest(constraintBlob);
    planOut->verificationDigest = cliIntentDigest(verifyBlob);
    return true;
  }

  std::vector<IntentRuleInfo> rules {};
  cliIntentCollectRulesForGoal(entry.goal, &rules);
  if (rules.empty()) {
    if (errorOut != nullptr) {
      *errorOut = "unsupported goal `" + entry.goal + "`";
    }
    return false;
  }

  for (const auto &rule : rules) {
    planOut->candidateRules.push_back(rule.id);
  }

  const auto target = cliIntentTargetFingerprint();
  auto finalizeVerifiedPlan = [&](const IntentRuleInfo &rule) {
    std::string constraintBlob {};
    for (const auto &constraint : entry.constraints) {
      constraintBlob += constraint;
      constraintBlob.push_back('\n');
    }
    std::string verifyBlob {};
    verifyBlob += entry.goal;
    verifyBlob.push_back('|');
    verifyBlob += rule.id;
    verifyBlob.push_back('|');
    verifyBlob += target;
    verifyBlob.push_back('|');
    verifyBlob += constraintBlob;
    for (const auto &example : entry.examples) {
      verifyBlob += example;
      verifyBlob.push_back('\n');
    }

    planOut->selectedRule = rule.id;
    planOut->verified = true;
    planOut->verifyReason = "ok";
    planOut->constraintsDigest = cliIntentDigest(constraintBlob);
    planOut->verificationDigest = cliIntentDigest(verifyBlob);
  };

  if (!entry.strategy.empty()) {
    const auto pinnedRuleId = cliIntentRuleIdFromStrategy(entry.strategy);
    if (!pinnedRuleId.has_value()) {
      if (errorOut != nullptr) {
        *errorOut = "unsupported strategy `" + entry.strategy + "`";
      }
      planOut->selectedRule = "rule.unsupported";
      planOut->verified = false;
      planOut->verifyReason = "unsupported strategy";
      return false;
    }

    IntentRuleInfo pinnedRule {};
    if (!cliIntentFindRuleById(rules, *pinnedRuleId, &pinnedRule)) {
      if (errorOut != nullptr) {
        *errorOut = "strategy `" + entry.strategy + "` is not compatible with goal `" + entry.goal + "`";
      }
      planOut->selectedRule = *pinnedRuleId;
      planOut->verified = false;
      planOut->verifyReason = "strategy-goal-mismatch";
      return false;
    }

    std::string verifyReason {};
    planOut->candidateCount = 1;
    if (!cliIntentVerifyRuleConstraints(pinnedRule, entry, &verifyReason)) {
      if (errorOut != nullptr) {
        *errorOut = verifyReason;
      }
      planOut->selectedRule = pinnedRule.id;
      planOut->verified = false;
      planOut->verifyReason = verifyReason;
      return false;
    }
    finalizeVerifiedPlan(pinnedRule);
    return true;
  }

  std::vector<std::pair<int, std::string>> scored {};
  const std::size_t cap = mode == "min" ? std::min<std::size_t>(2, rules.size()) : rules.size();
  for (std::size_t i = 0; i < cap; ++i) {
    scored.emplace_back(cliIntentScoreRule(rules[i], entry, mode, target), rules[i].id);
  }
  std::sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) {
    if (a.first == b.first) {
      return a.second < b.second;
    }
    return a.first < b.first;
  });

  planOut->candidateCount = static_cast<int>(scored.size());
  std::string lastReason {"no candidate"};
  for (const auto &item : scored) {
    IntentRuleInfo rule {};
    if (!cliIntentFindRuleById(rules, item.second, &rule)) {
      continue;
    }
    std::string verifyReason {};
    if (cliIntentVerifyRuleConstraints(rule, entry, &verifyReason)) {
      finalizeVerifiedPlan(rule);
      return true;
    }
    lastReason = verifyReason;
  }

  planOut->selectedRule = rules.front().id;
  planOut->verified = false;
  planOut->verifyReason = lastReason;
  if (errorOut != nullptr) {
    *errorOut = lastReason;
  }
  return false;
}

static auto cliIntentBuildPlans(
  const std::vector<IntentEntry> &entries,
  const std::string &mode,
  std::vector<IntentPlan> *plansOut,
  std::string *errorOut
) -> bool {
  if (plansOut == nullptr) {
    return false;
  }
  plansOut->clear();
  CliIntentRuleRegistry registry {};
  const bool hasRegistry = cliIntentLoadRuleRegistry(&registry);
  const bool registryEnabled = hasRegistry && registry.enabled;
  std::size_t appliedTotal = 0;
  std::unordered_map<std::string, std::size_t> appliedFamily {};
  for (const auto &entry : entries) {
    IntentPlan plan {};
    std::string err {};
    if (!cliIntentSelectPlanForEntry(entry, mode, &plan, &err)) {
      if (errorOut != nullptr) {
        *errorOut = "intent " + entry.id + " failed: " + err;
      }
      return false;
    }
    if (registryEnabled && plan.selectedRule != "rule.intent.off") {
      const auto &reg = registry;
      if (!reg.allowedRules.empty() && reg.allowedRules.find(plan.selectedRule) == reg.allowedRules.end()) {
        if (errorOut != nullptr) {
          *errorOut = "intent " + entry.id + " blocked by registry-rule-disabled (" + plan.selectedRule + ")";
        }
        return false;
      }
      if (reg.totalBudget != std::numeric_limits<std::size_t>::max() && appliedTotal >= reg.totalBudget) {
        if (errorOut != nullptr) {
          *errorOut = "intent " + entry.id + " blocked by registry-total-budget-exceeded";
        }
        return false;
      }
      const auto family = cliIntentRuleFamily(plan.selectedRule);
      const auto familyLimitIt = reg.familyBudget.find(family);
      if (familyLimitIt != reg.familyBudget.end() && appliedFamily[family] >= familyLimitIt->second) {
        if (errorOut != nullptr) {
          *errorOut = "intent " + entry.id + " blocked by registry-family-budget-exceeded:" + family;
        }
        return false;
      }
      appliedTotal += 1;
      appliedFamily[family] += 1;
    }
    plansOut->push_back(std::move(plan));
  }
  return true;
}

static auto cliIntentParseTargetName(std::string_view header) -> std::string {
  const auto text = trim(header);
  if (!startsWith(text, "intent func ")) {
    return {};
  }
  const auto rest = trim(text.substr(12));
  const std::size_t paren = rest.find('(');
  if (paren == std::string_view::npos) {
    return {};
  }
  const auto name = trim(rest.substr(0, paren));
  if (name.empty()) {
    return {};
  }
  return std::string(name);
}

static void cliIntentParseEntries(
  const std::string &source,
  const std::string &inputPath,
  std::vector<IntentEntry> *outEntries
) {
  if (outEntries == nullptr) {
    return;
  }
  outEntries->clear();
  std::vector<std::string> lines {};
  {
    std::size_t cursor = 0;
    while (cursor <= source.size()) {
      const std::size_t end = source.find('\n', cursor);
      const std::size_t lineEnd = end == std::string::npos ? source.size() : end;
      lines.emplace_back(source.substr(cursor, lineEnd - cursor));
      if (end == std::string::npos) {
        break;
      }
      cursor = end + 1;
    }
  }

  for (std::size_t i = 0; i < lines.size(); ++i) {
    const std::string_view raw = lines[i];
    const std::string_view text = trim(raw);
    if (text.empty() || startsWith(text, "#") || startsWith(text, "//")) {
      continue;
    }
    if (leadingSpaces(raw) != 0) {
      continue;
    }
    if (!startsWith(text, "intent ")) {
      continue;
    }

    IntentEntry entry {};
    entry.line = static_cast<int>(i + 1);
    entry.id = inputPath + ":" + std::to_string(entry.line) + ":intent_" + std::to_string(outEntries->size() + 1);

    const bool hasColon = !text.empty() && text.back() == ':';
    if (startsWith(text, "intent func ") && hasColon) {
      entry.kind = "func";
      entry.targetName = cliIntentParseTargetName(text);
    } else if (startsWith(text, "intent loop ") && hasColon) {
      entry.kind = "loop";
    } else if (startsWith(text, "intent calc(") && hasColon) {
      entry.kind = "calc";
    } else if (text == "intent block:") {
      entry.kind = "block";
    }

    const int baseIndent = leadingSpaces(raw);
    int constraintsIndent = -1;
    int examplesIndent = -1;
    for (std::size_t j = i + 1; j < lines.size(); ++j) {
      const std::string_view bodyRaw = lines[j];
      const std::string_view body = trim(bodyRaw);
      if (body.empty() || startsWith(body, "#") || startsWith(body, "//")) {
        continue;
      }
      const int bodyIndent = leadingSpaces(bodyRaw);
      if (bodyIndent <= baseIndent) {
        break;
      }
      if (startsWith(body, "goal:")) {
        const auto goalText = trim(body.substr(5));
        if (!goalText.empty()) {
          const auto parsedGoal = std::string(goalText);
          entry.goal = parsedGoal;
          entry.hasGoal = true;
          if (cliIntentToLower(parsedGoal) == "off") {
            entry.intentEnabled = false;
          }
        }
        constraintsIndent = -1;
        examplesIndent = -1;
        continue;
      }
      if (startsWith(body, "strategy:")) {
        const auto strategyText = trim(body.substr(9));
        if (!strategyText.empty()) {
          entry.strategy = std::string(strategyText);
          entry.hasStrategy = true;
        }
        constraintsIndent = -1;
        examplesIndent = -1;
        continue;
      }
      if (body == "constraints:") {
        entry.hasConstraintsHeader = true;
        constraintsIndent = bodyIndent;
        examplesIndent = -1;
        continue;
      }
      if (body == "examples:") {
        entry.hasExamplesHeader = true;
        constraintsIndent = -1;
        examplesIndent = bodyIndent;
        continue;
      }
      if (constraintsIndent >= 0 && bodyIndent > constraintsIndent) {
        entry.constraints.emplace_back(std::string(body));
        if (cliIntentConstraintDisablesIntent(entry.constraints.back())) {
          entry.intentEnabled = false;
        }
      } else if (constraintsIndent >= 0 && bodyIndent <= constraintsIndent) {
        constraintsIndent = -1;
      }
      if (examplesIndent >= 0 && bodyIndent > examplesIndent) {
        entry.examples.emplace_back(std::string(body));
      } else if (examplesIndent >= 0 && bodyIndent <= examplesIndent) {
        examplesIndent = -1;
      }
    }
    outEntries->push_back(std::move(entry));
  }
}

static auto cliIntentParseSingleI32ParamNameInline(std::string_view funcHeader, std::string *paramNameOut) -> bool {
  if (paramNameOut == nullptr) {
    return false;
  }
  const auto header = trim(funcHeader);
  if (!startsWith(header, "func ")) {
    return false;
  }
  const std::size_t lp = header.find('(');
  const std::size_t rp = header.find(')', lp == std::string_view::npos ? 0 : lp + 1);
  if (lp == std::string_view::npos || rp == std::string_view::npos || rp <= (lp + 1)) {
    return false;
  }
  const auto params = trim(header.substr(lp + 1, rp - lp - 1));
  if (params.empty() || params.find(',') != std::string_view::npos) {
    return false;
  }
  const std::size_t colon = params.find(':');
  if (colon == std::string_view::npos) {
    return false;
  }
  const auto name = trim(params.substr(0, colon));
  const auto type = trim(params.substr(colon + 1));
  if (name.empty() || type != "i32") {
    return false;
  }
  *paramNameOut = std::string(name);
  return true;
}

static auto cliIntentCountOccurrences(const std::string &text, const std::string &needle) -> int {
  if (needle.empty()) {
    return 0;
  }
  int count = 0;
  std::size_t pos = 0;
  while (true) {
    pos = text.find(needle, pos);
    if (pos == std::string::npos) {
      break;
    }
    ++count;
    pos += needle.size();
  }
  return count;
}

static auto cliIntentInferAutoGoalByName(std::string_view rawName) -> std::string {
  std::string name = std::string(trim(rawName));
  for (char &ch : name) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  if (name.empty()) {
    return {};
  }

  auto has = [&](std::string_view needle) {
    return name.find(needle) != std::string::npos;
  };

  if (has("trib")) {
    return "tribonacci_dp";
  }
  if (has("fib")) {
    return "fibonacci_dp";
  }
  if (has("sort")) {
    return "sort_ascending";
  }
  if (has("lower_bound") || (has("lower") && has("bound"))) {
    return "lower_bound_sorted";
  }
  if (has("upper_bound") || (has("upper") && has("bound"))) {
    return "upper_bound_sorted";
  }
  if (has("count_range") || (has("count") && has("range"))) {
    return "count_range_sorted";
  }
  if (has("count_outside") || (has("outside") && has("range"))) {
    return "count_outside_range_sorted";
  }
  if (has("count_less_equal") || has("count_le")) {
    return "count_less_equal_sorted";
  }
  if (has("count_less") || has("count_lt")) {
    return "count_less_sorted";
  }
  if (has("count_greater_equal") || has("count_ge")) {
    return "count_greater_equal_sorted";
  }
  if (has("count_greater") || has("count_gt")) {
    return "count_greater_sorted";
  }
  if (has("count_not_equal") || has("count_ne")) {
    return "count_not_equal_sorted";
  }
  if (has("count_equal")) {
    return "count_equal_sorted";
  }
  if (has("two_sum") || has("pair_sum")) {
    return "two_sum_sorted_exists";
  }
  if (has("binary_search") || has("bsearch")) {
    return "binary_search_sorted";
  }
  if (has("search") || has("find")) {
    return "search_element";
  }
  if (has("factorial")) {
    return "factorial_iterative";
  }
  if (has("pow") || has("exp")) {
    return "power_fast";
  }
  if (has("gcd")) {
    return "gcd_euclid";
  }
  if (has("prime")) {
    return "is_prime_fast";
  }
  if (has("divisor")) {
    return "count_divisors_sqrt";
  }
  if (has("sprinkler") || has("interval_cover") || has("cover")) {
    return "interval_cover_greedy";
  }
  if (has("bit_peel")) {
    return "bit_peel_iterative";
  }
  if (has("sum_even_squares")) {
    return "sum_even_squares_formula";
  }
  if (has("sum_odd_squares")) {
    return "sum_odd_squares_formula";
  }
  if (has("sum_even_cubes")) {
    return "sum_even_cubes_formula";
  }
  if (has("sum_odd_cubes")) {
    return "sum_odd_cubes_formula";
  }
  if (has("sum_squares")) {
    return "sum_squares_formula";
  }
  if (has("sum_cubes")) {
    return "sum_cubes_formula";
  }
  if (has("sum_even")) {
    return "sum_even_formula";
  }
  if (has("sum_odd")) {
    return "sum_odd_formula";
  }
  if (has("sum")) {
    return "sum_formula";
  }
  if (has("sqrt")) {
    return "sqrt_bounded_loop";
  }
  return {};
}

static auto cliIntentInferGoalFromFunctionBody(
  const std::vector<std::string> &lines,
  std::size_t funcStart,
  std::size_t funcEnd,
  const IntentEntry &entry,
  std::string *goalOut,
  std::string *reasonOut
) -> bool {
  if (goalOut == nullptr) {
    return false;
  }
  std::string body {};
  for (std::size_t i = funcStart + 1; i < funcEnd; ++i) {
    body += lines[i];
    body.push_back('\n');
  }
  const auto header = std::string_view(lines[funcStart]);

  const std::string callNeedle = entry.targetName + "(";
  if (cliIntentCountOccurrences(body, callNeedle) >= 3
      && body.find("return ") != std::string::npos
      && cliIntentCountOccurrences(body, "+") >= 2) {
    *goalOut = "tribonacci_dp";
    return true;
  }

  if (cliIntentCountOccurrences(body, callNeedle) >= 2 && body.find("return ") != std::string::npos && body.find('+') != std::string::npos) {
    *goalOut = "fibonacci_dp";
    return true;
  }

  const int whileCount = cliIntentCountOccurrences(body, "while (");
  if (whileCount >= 2 && body.find("[j]") != std::string::npos && body.find("[j + 1]") != std::string::npos && body.find('>') != std::string::npos) {
    *goalOut = "sort_ascending";
    return true;
  }

  if (body.find("mid") != std::string::npos && body.find("while (") != std::string::npos && body.find("==") != std::string::npos) {
    *goalOut = "search_element";
    return true;
  }

  std::string paramName {};
  if (cliIntentParseSingleI32ParamNameInline(header, &paramName)) {
    const std::string sqrtCond = "while (i <= " + paramName + "):";
    if (body.find(sqrtCond) != std::string::npos && body.find('%') != std::string::npos) {
      *goalOut = "sqrt_bounded_loop";
      return true;
    }
  }

  const auto nameGoal = cliIntentInferAutoGoalByName(entry.targetName);
  if (!nameGoal.empty()) {
    *goalOut = nameGoal;
    return true;
  }

  if (reasonOut != nullptr) {
    *reasonOut = "unable to infer algorithm pattern for auto_plan";
  }
  return false;
}

static auto cliIntentResolveAutoGoals(
  const std::string &source,
  std::vector<IntentEntry> *entries,
  std::string *errorOut
) -> bool {
  if (entries == nullptr) {
    return false;
  }
  std::vector<std::string> lines {};
  {
    std::size_t cursor = 0;
    while (cursor <= source.size()) {
      const std::size_t end = source.find('\n', cursor);
      const std::size_t lineEnd = end == std::string::npos ? source.size() : end;
      lines.emplace_back(source.substr(cursor, lineEnd - cursor));
      if (end == std::string::npos) {
        break;
      }
      cursor = end + 1;
    }
  }

  for (auto &entry : *entries) {
    if (!entry.intentEnabled || entry.goal != "auto_plan") {
      continue;
    }
    if (entry.kind != "func" || entry.targetName.empty()) {
      if (errorOut != nullptr) {
        *errorOut = "auto_plan currently supports only `intent func` with explicit target name";
      }
      return false;
    }

    const std::string funcPrefix = "func " + entry.targetName + "(";
    std::size_t funcStart = std::string::npos;
    for (std::size_t i = 0; i < lines.size(); ++i) {
      const auto text = trim(lines[i]);
      if (startsWith(text, funcPrefix) && !text.empty() && text.back() == ':') {
        funcStart = i;
        break;
      }
    }
    if (funcStart == std::string::npos) {
      if (errorOut != nullptr) {
        *errorOut = "auto_plan target function `" + entry.targetName + "` was not found";
      }
      return false;
    }

    const int baseIndent = leadingSpaces(lines[funcStart]);
    std::size_t funcEnd = lines.size();
    for (std::size_t i = funcStart + 1; i < lines.size(); ++i) {
      const auto text = trim(lines[i]);
      if (text.empty() || startsWith(text, "#") || startsWith(text, "//")) {
        continue;
      }
      if (leadingSpaces(lines[i]) <= baseIndent) {
        funcEnd = i;
        break;
      }
    }

    std::string inferredGoal {};
    std::string inferReason {};
    if (!cliIntentInferGoalFromFunctionBody(lines, funcStart, funcEnd, entry, &inferredGoal, &inferReason)) {
      if (errorOut != nullptr) {
        *errorOut = "intent at line " + std::to_string(entry.line) + " auto_plan failed: " + inferReason;
      }
      return false;
    }
    entry.goal = inferredGoal;
  }
  return true;
}

static auto cliIntentValidateEntries(
  const std::vector<IntentEntry> &entries,
  bool requireKnownGoal,
  std::string *error
) -> bool {
  for (const auto &entry : entries) {
    if (entry.kind.empty()) {
      if (error != nullptr) {
        *error = "invalid intent header at line " + std::to_string(entry.line);
      }
      return false;
    }
    if (!entry.hasGoal) {
      if (error != nullptr) {
        *error = "intent at line " + std::to_string(entry.line) + " is missing goal";
      }
      return false;
    }
    if (!entry.intentEnabled) {
      continue;
    }
    if (!entry.hasConstraintsHeader) {
      if (error != nullptr) {
        *error = "intent at line " + std::to_string(entry.line) + " is missing constraints section";
      }
      return false;
    }
    if (entry.constraints.empty()) {
      if (error != nullptr) {
        *error = "intent at line " + std::to_string(entry.line) + " has empty constraints section";
      }
      return false;
    }
    if (entry.hasExamplesHeader && entry.examples.empty()) {
      if (error != nullptr) {
        *error = "intent at line " + std::to_string(entry.line) + " has empty examples section";
      }
      return false;
    }
    if (requireKnownGoal && !cliIntentGoalSupported(entry.goal)) {
      if (error != nullptr) {
        *error = "intent at line " + std::to_string(entry.line) + " uses unsupported goal `" + entry.goal + "`";
      }
      return false;
    }
  }
  return true;
}

static auto cliIntentFindQuotedValueRange(
  const std::string &text,
  const std::string &key,
  std::size_t startPos,
  std::size_t endPos,
  std::string *valueOut,
  std::size_t *keyPosOut
) -> bool {
  if (valueOut == nullptr) {
    return false;
  }
  const std::string keyPattern = "\"" + key + "\"";
  const std::size_t keyPos = text.find(keyPattern, startPos);
  if (keyPos == std::string::npos || keyPos >= endPos) {
    return false;
  }
  const std::size_t colonPos = text.find(':', keyPos + keyPattern.size());
  if (colonPos == std::string::npos || colonPos >= endPos) {
    return false;
  }
  const std::size_t quoteStart = text.find('"', colonPos + 1);
  if (quoteStart == std::string::npos || quoteStart >= endPos) {
    return false;
  }
  std::string value {};
  bool escaped = false;
  for (std::size_t i = quoteStart + 1; i < endPos; ++i) {
    const char ch = text[i];
    if (escaped) {
      value.push_back(ch);
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      *valueOut = value;
      if (keyPosOut != nullptr) {
        *keyPosOut = keyPos;
      }
      return true;
    }
    value.push_back(ch);
  }
  return false;
}

static auto cliIntentParseLockFile(
  const std::string &lockText,
  std::string *sourceDigestOut,
  std::unordered_map<std::string, IntentLockEntry> *entriesOut,
  std::string *errorOut
) -> bool {
  if (entriesOut == nullptr) {
    return false;
  }
  entriesOut->clear();
  if (sourceDigestOut != nullptr) {
    sourceDigestOut->clear();
  }

  std::string sourceDigest {};
  if (cliIntentFindQuotedValueRange(lockText, "source_digest", 0, lockText.size(), &sourceDigest, nullptr)) {
    if (sourceDigestOut != nullptr) {
      *sourceDigestOut = sourceDigest;
    }
  }

  std::size_t cursor = 0;
  while (true) {
    std::string intentId {};
    std::size_t intentPos = 0;
    if (!cliIntentFindQuotedValueRange(lockText, "intent_id", cursor, lockText.size(), &intentId, &intentPos)) {
      break;
    }
    std::size_t nextIntentPos = lockText.find("\"intent_id\"", intentPos + 1);
    if (nextIntentPos == std::string::npos) {
      nextIntentPos = lockText.size();
    }

    IntentLockEntry lockEntry {};
    (void)cliIntentFindQuotedValueRange(lockText, "selected_rule", intentPos, nextIntentPos, &lockEntry.selectedRule, nullptr);
    (void)cliIntentFindQuotedValueRange(lockText, "constraints_digest", intentPos, nextIntentPos, &lockEntry.constraintsDigest, nullptr);
    (void)cliIntentFindQuotedValueRange(lockText, "verification_digest", intentPos, nextIntentPos, &lockEntry.verificationDigest, nullptr);

    if (lockEntry.selectedRule.empty()) {
      if (errorOut != nullptr) {
        *errorOut = "lock entry is missing selected_rule for intent_id `" + intentId + "`";
      }
      return false;
    }
    entriesOut->insert_or_assign(intentId, lockEntry);
    cursor = nextIntentPos;
  }
  return true;
}

static auto cliIntentValidateLockAgainstPlans(
  const std::string &lockPath,
  const std::string &sourceDigest,
  const std::vector<IntentPlan> &plans,
  bool strictLock,
  std::vector<std::string> *warningsOut,
  std::string *errorOut
) -> bool {
  if (!std::filesystem::exists(lockPath)) {
    if (strictLock) {
      if (errorOut != nullptr) {
        *errorOut = "strict lock is enabled but lockfile is missing: " + lockPath;
      }
      return false;
    }
    return true;
  }

  const auto lockText = cliReadText(lockPath);
  if (lockText.empty()) {
    if (strictLock) {
      if (errorOut != nullptr) {
        *errorOut = "lockfile is empty: " + lockPath;
      }
      return false;
    }
    if (warningsOut != nullptr) {
      warningsOut->push_back("lockfile is empty: " + lockPath);
    }
    return true;
  }

  std::unordered_map<std::string, IntentLockEntry> lockEntries {};
  std::string lockSourceDigest {};
  std::string parseError {};
  if (!cliIntentParseLockFile(lockText, &lockSourceDigest, &lockEntries, &parseError)) {
    if (strictLock) {
      if (errorOut != nullptr) {
        *errorOut = "failed to parse lockfile: " + parseError;
      }
      return false;
    }
    if (warningsOut != nullptr) {
      warningsOut->push_back("failed to parse lockfile: " + parseError);
    }
    return true;
  }

  if (!lockSourceDigest.empty() && lockSourceDigest != sourceDigest) {
    const std::string msg = "source digest mismatch between source and lockfile";
    if (strictLock) {
      if (errorOut != nullptr) {
        *errorOut = msg;
      }
      return false;
    }
    if (warningsOut != nullptr) {
      warningsOut->push_back(msg);
    }
  }

  for (const auto &plan : plans) {
    const auto it = lockEntries.find(plan.intentId);
    if (it == lockEntries.end()) {
      const std::string msg = "lockfile missing entry for " + plan.intentId;
      if (strictLock) {
        if (errorOut != nullptr) {
          *errorOut = msg;
        }
        return false;
      }
      if (warningsOut != nullptr) {
        warningsOut->push_back(msg);
      }
      continue;
    }
    if (it->second.selectedRule != plan.selectedRule) {
      const std::string msg = "selected_rule mismatch for " + plan.intentId;
      if (strictLock) {
        if (errorOut != nullptr) {
          *errorOut = msg;
        }
        return false;
      }
      if (warningsOut != nullptr) {
        warningsOut->push_back(msg);
      }
    }
    if (!plan.constraintsDigest.empty() && !it->second.constraintsDigest.empty()
        && it->second.constraintsDigest != plan.constraintsDigest) {
      const std::string msg = "constraints_digest mismatch for " + plan.intentId;
      if (strictLock) {
        if (errorOut != nullptr) {
          *errorOut = msg;
        }
        return false;
      }
      if (warningsOut != nullptr) {
        warningsOut->push_back(msg);
      }
    }
  }
  return true;
}

static auto cliIntentSplitLines(const std::string &source, bool *hasTrailingNewline) -> std::vector<std::string> {
  if (hasTrailingNewline != nullptr) {
    *hasTrailingNewline = !source.empty() && source.back() == '\n';
  }
  std::vector<std::string> lines {};
  std::size_t cursor = 0;
  while (cursor <= source.size()) {
    const std::size_t end = source.find('\n', cursor);
    const std::size_t lineEnd = end == std::string::npos ? source.size() : end;
    lines.emplace_back(source.substr(cursor, lineEnd - cursor));
    if (end == std::string::npos) {
      break;
    }
    cursor = end + 1;
  }
  return lines;
}

static auto cliIntentJoinLines(const std::vector<std::string> &lines, bool trailingNewline) -> std::string {
  std::string out {};
  for (std::size_t i = 0; i < lines.size(); ++i) {
    out += lines[i];
    if ((i + 1) < lines.size()) {
      out.push_back('\n');
    }
  }
  if (trailingNewline) {
    out.push_back('\n');
  }
  return out;
}

static auto cliIntentParseSingleI32ParamName(std::string_view funcHeader, std::string *paramNameOut) -> bool {
  if (paramNameOut == nullptr) {
    return false;
  }
  const auto header = trim(funcHeader);
  if (!startsWith(header, "func ")) {
    return false;
  }
  const std::size_t lp = header.find('(');
  const std::size_t rp = header.find(')', lp == std::string_view::npos ? 0 : lp + 1);
  if (lp == std::string_view::npos || rp == std::string_view::npos || rp <= (lp + 1)) {
    return false;
  }
  const auto params = trim(header.substr(lp + 1, rp - lp - 1));
  if (params.empty() || params.find(',') != std::string_view::npos) {
    return false;
  }
  const std::size_t colon = params.find(':');
  if (colon == std::string_view::npos) {
    return false;
  }
  const auto name = trim(params.substr(0, colon));
  const auto type = trim(params.substr(colon + 1));
  if (name.empty() || type != "i32") {
    return false;
  }
  *paramNameOut = std::string(name);
  return true;
}

static auto cliIntentRewriteFibonacciFunc(
  const std::string &source,
  const IntentEntry &entry,
  std::string *outSource,
  std::string *reasonOut
) -> bool {
  if (outSource == nullptr) {
    return false;
  }
  if (entry.kind != "func" || entry.targetName.empty()) {
    if (reasonOut != nullptr) {
      *reasonOut = "intent target is not a function";
    }
    return false;
  }

  bool trailingNewline = false;
  auto lines = cliIntentSplitLines(source, &trailingNewline);
  std::size_t funcStart = std::string::npos;
  const std::string funcPrefix = "func " + entry.targetName + "(";
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const auto text = trim(lines[i]);
    if (startsWith(text, funcPrefix) && !text.empty() && text.back() == ':') {
      funcStart = i;
      break;
    }
  }
  if (funcStart == std::string::npos) {
    if (reasonOut != nullptr) {
      *reasonOut = "target function `" + entry.targetName + "` was not found";
    }
    return false;
  }

  const int baseIndent = leadingSpaces(lines[funcStart]);
  std::size_t funcEnd = lines.size();
  for (std::size_t i = funcStart + 1; i < lines.size(); ++i) {
    const auto text = trim(lines[i]);
    if (text.empty() || startsWith(text, "#") || startsWith(text, "//")) {
      continue;
    }
    if (leadingSpaces(lines[i]) <= baseIndent) {
      funcEnd = i;
      break;
    }
  }

  const std::string callNeedle = entry.targetName + "(";
  bool hasRecursiveSumReturn = false;
  for (std::size_t i = funcStart + 1; i < funcEnd; ++i) {
    const auto text = trim(lines[i]);
    if (!startsWith(text, "return ") || text.find('+') == std::string_view::npos) {
      continue;
    }
    const std::size_t p1 = text.find(callNeedle);
    if (p1 == std::string_view::npos) {
      continue;
    }
    const std::size_t p2 = text.find(callNeedle, p1 + callNeedle.size());
    if (p2 != std::string_view::npos) {
      hasRecursiveSumReturn = true;
      break;
    }
  }
  if (!hasRecursiveSumReturn) {
    if (reasonOut != nullptr) {
      *reasonOut = "function body does not match supported recursive fibonacci pattern";
    }
    return false;
  }

  std::string paramName {};
  if (!cliIntentParseSingleI32ParamName(lines[funcStart], &paramName)) {
    if (reasonOut != nullptr) {
      *reasonOut = "function signature must be a single i32 parameter";
    }
    return false;
  }

  const std::string indent0(static_cast<std::size_t>(baseIndent), ' ');
  const std::string indent1(static_cast<std::size_t>(baseIndent + 4), ' ');
  const std::string indent2(static_cast<std::size_t>(baseIndent + 8), ' ');
  std::vector<std::string> rewrittenFunc {
    lines[funcStart],
    indent1 + "if (" + paramName + " < 2):",
    indent2 + "return " + paramName,
    indent1 + "let a = 0",
    indent1 + "let b = 1",
    indent1 + "let i = 2",
    indent1 + "while (i <= " + paramName + "):",
    indent2 + "let c = a + b",
    indent2 + "a = b",
    indent2 + "b = c",
    indent2 + "i = i + 1",
    indent1 + "return b",
  };

  std::vector<std::string> merged {};
  merged.reserve(lines.size() + rewrittenFunc.size());
  merged.insert(merged.end(), lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(funcStart));
  merged.insert(merged.end(), rewrittenFunc.begin(), rewrittenFunc.end());
  merged.insert(merged.end(), lines.begin() + static_cast<std::ptrdiff_t>(funcEnd), lines.end());

  *outSource = cliIntentJoinLines(merged, trailingNewline);
  (void)indent0;
  return true;
}

static auto cliIntentRewriteTribonacciFunc(
  const std::string &source,
  const IntentEntry &entry,
  std::string *outSource,
  std::string *reasonOut
) -> bool {
  if (outSource == nullptr) {
    return false;
  }
  if (entry.kind != "func" || entry.targetName.empty()) {
    if (reasonOut != nullptr) {
      *reasonOut = "intent target is not a function";
    }
    return false;
  }

  bool trailingNewline = false;
  auto lines = cliIntentSplitLines(source, &trailingNewline);
  std::size_t funcStart = std::string::npos;
  const std::string funcPrefix = "func " + entry.targetName + "(";
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const auto text = trim(lines[i]);
    if (startsWith(text, funcPrefix) && !text.empty() && text.back() == ':') {
      funcStart = i;
      break;
    }
  }
  if (funcStart == std::string::npos) {
    if (reasonOut != nullptr) {
      *reasonOut = "target function `" + entry.targetName + "` was not found";
    }
    return false;
  }

  const int baseIndent = leadingSpaces(lines[funcStart]);
  std::size_t funcEnd = lines.size();
  for (std::size_t i = funcStart + 1; i < lines.size(); ++i) {
    const auto text = trim(lines[i]);
    if (text.empty() || startsWith(text, "#") || startsWith(text, "//")) {
      continue;
    }
    if (leadingSpaces(lines[i]) <= baseIndent) {
      funcEnd = i;
      break;
    }
  }

  const std::string callNeedle = entry.targetName + "(";
  auto count_occurrences = [](std::string_view text, std::string_view needle) -> int {
    if (needle.empty()) {
      return 0;
    }
    int count = 0;
    std::size_t pos = 0;
    while (true) {
      const auto found = text.find(needle, pos);
      if (found == std::string_view::npos) {
        break;
      }
      ++count;
      pos = found + needle.size();
    }
    return count;
  };

  bool hasRecursiveSumReturn = false;
  for (std::size_t i = funcStart + 1; i < funcEnd; ++i) {
    const auto text = trim(lines[i]);
    if (!startsWith(text, "return ")) {
      continue;
    }
    const int selfCalls = count_occurrences(text, callNeedle);
    const int plusCount = count_occurrences(text, "+");
    if (selfCalls >= 3 && plusCount >= 2) {
      hasRecursiveSumReturn = true;
      break;
    }
  }
  if (!hasRecursiveSumReturn) {
    if (reasonOut != nullptr) {
      *reasonOut = "function body does not match supported recursive tribonacci pattern";
    }
    return false;
  }

  std::string paramName {};
  if (!cliIntentParseSingleI32ParamName(lines[funcStart], &paramName)) {
    if (reasonOut != nullptr) {
      *reasonOut = "function signature must be a single i32 parameter";
    }
    return false;
  }

  std::string body {};
  for (std::size_t i = funcStart + 1; i < funcEnd; ++i) {
    body += lines[i];
    body.push_back('\n');
  }
  const std::string base0 = "if (" + paramName + " == 0):";
  const std::string base1 = "if (" + paramName + " == 1):";
  const std::string base2 = "if (" + paramName + " == 2):";
  if (body.find(base0) == std::string::npos
      || body.find(base1) == std::string::npos
      || body.find(base2) == std::string::npos
      || body.find("return 0") == std::string::npos
      || count_occurrences(body, "return 1") < 2) {
    if (reasonOut != nullptr) {
      *reasonOut = "tribonacci rewrite requires base cases n=0->0, n=1->1, n=2->1";
    }
    return false;
  }

  const std::string indent0(static_cast<std::size_t>(baseIndent), ' ');
  const std::string indent1(static_cast<std::size_t>(baseIndent + 4), ' ');
  const std::string indent2(static_cast<std::size_t>(baseIndent + 8), ' ');
  std::vector<std::string> rewrittenFunc {
    lines[funcStart],
    indent1 + "if (" + paramName + " == 0):",
    indent2 + "return 0",
    indent1 + "if (" + paramName + " == 1):",
    indent2 + "return 1",
    indent1 + "if (" + paramName + " == 2):",
    indent2 + "return 1",
    indent1 + "let a = 0",
    indent1 + "let b = 1",
    indent1 + "let c = 1",
    indent1 + "let i = 3",
    indent1 + "while (i <= " + paramName + "):",
    indent2 + "let d = a + b + c",
    indent2 + "a = b",
    indent2 + "b = c",
    indent2 + "c = d",
    indent2 + "i = i + 1",
    indent1 + "return c",
  };

  std::vector<std::string> merged {};
  merged.reserve(lines.size() + rewrittenFunc.size());
  merged.insert(merged.end(), lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(funcStart));
  merged.insert(merged.end(), rewrittenFunc.begin(), rewrittenFunc.end());
  merged.insert(merged.end(), lines.begin() + static_cast<std::ptrdiff_t>(funcEnd), lines.end());

  *outSource = cliIntentJoinLines(merged, trailingNewline);
  (void)indent0;
  return true;
}

static auto cliIntentRewriteSqrtLoopFunc(
  const std::string &source,
  const IntentEntry &entry,
  std::string *outSource,
  std::string *reasonOut
) -> bool {
  if (outSource == nullptr) {
    return false;
  }
  if (entry.kind != "func" || entry.targetName.empty()) {
    if (reasonOut != nullptr) {
      *reasonOut = "intent target is not a function";
    }
    return false;
  }

  bool trailingNewline = false;
  auto lines = cliIntentSplitLines(source, &trailingNewline);
  std::size_t funcStart = std::string::npos;
  const std::string funcPrefix = "func " + entry.targetName + "(";
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const auto text = trim(lines[i]);
    if (startsWith(text, funcPrefix) && !text.empty() && text.back() == ':') {
      funcStart = i;
      break;
    }
  }
  if (funcStart == std::string::npos) {
    if (reasonOut != nullptr) {
      *reasonOut = "target function `" + entry.targetName + "` was not found";
    }
    return false;
  }

  std::string paramName {};
  if (!cliIntentParseSingleI32ParamName(lines[funcStart], &paramName)) {
    if (reasonOut != nullptr) {
      *reasonOut = "function signature must be a single i32 parameter";
    }
    return false;
  }

  const int baseIndent = leadingSpaces(lines[funcStart]);
  std::size_t funcEnd = lines.size();
  for (std::size_t i = funcStart + 1; i < lines.size(); ++i) {
    const auto text = trim(lines[i]);
    if (text.empty() || startsWith(text, "#") || startsWith(text, "//")) {
      continue;
    }
    if (leadingSpaces(lines[i]) <= baseIndent) {
      funcEnd = i;
      break;
    }
  }

  const std::string needle = "while (i <= " + paramName + "):";
  const std::string replacement = "while ((i * i) <= " + paramName + "):";
  std::size_t whileLine = std::string::npos;
  bool hasModulo = false;
  for (std::size_t i = funcStart + 1; i < funcEnd; ++i) {
    const auto text = trim(lines[i]);
    if (text == needle && whileLine == std::string::npos) {
      whileLine = i;
    }
    if (text.find("% i") != std::string::npos || text.find("%i") != std::string::npos) {
      hasModulo = true;
    }
  }
  if (whileLine == std::string::npos || !hasModulo) {
    if (reasonOut != nullptr) {
      *reasonOut = "function body does not match supported sqrt-loop pattern";
    }
    return false;
  }

  const int whileIndent = leadingSpaces(lines[whileLine]);
  lines[whileLine] = std::string(static_cast<std::size_t>(whileIndent), ' ') + replacement;
  *outSource = cliIntentJoinLines(lines, trailingNewline);
  return true;
}

static auto cliIntentApplyPlansToSource(
  const std::string &source,
  const std::vector<IntentEntry> &entries,
  const std::vector<IntentPlan> &plans,
  std::string *outSource,
  std::vector<std::string> *notesOut
) -> bool {
  if (outSource == nullptr) {
    return false;
  }
  *outSource = source;

  for (const auto &plan : plans) {
    if (plan.goal != "fibonacci_dp" && plan.goal != "tribonacci_dp" && plan.goal != "sqrt_bounded_loop") {
      continue;
    }
    if (!startsWith(plan.selectedRule, "rule.fibonacci_dp.")
        && !startsWith(plan.selectedRule, "rule.dp.trib.")
        && !startsWith(plan.selectedRule, "rule.sqrt_bounded_loop.")) {
      continue;
    }

    const auto entryIt = std::find_if(entries.begin(), entries.end(), [&](const IntentEntry &entry) {
      return entry.id == plan.intentId;
    });
    if (entryIt == entries.end()) {
      if (notesOut != nullptr) {
        notesOut->push_back("intent rewrite skipped: missing entry for " + plan.intentId);
      }
      continue;
    }

    std::string rewritten {};
    std::string reason {};
    bool rewrittenOk = false;
    if (plan.goal == "fibonacci_dp") {
      rewrittenOk = cliIntentRewriteFibonacciFunc(*outSource, *entryIt, &rewritten, &reason);
    } else if (plan.goal == "tribonacci_dp") {
      rewrittenOk = cliIntentRewriteTribonacciFunc(*outSource, *entryIt, &rewritten, &reason);
    } else if (plan.goal == "sqrt_bounded_loop") {
      rewrittenOk = cliIntentRewriteSqrtLoopFunc(*outSource, *entryIt, &rewritten, &reason);
    }
    if (rewrittenOk) {
      *outSource = std::move(rewritten);
      if (notesOut != nullptr) {
        notesOut->push_back(
          "intent rewrite applied for "
          + entryIt->targetName
          + " using "
          + plan.selectedRule
        );
      }
    } else if (notesOut != nullptr) {
      notesOut->push_back(
        "intent rewrite skipped for "
        + entryIt->id
        + " ("
        + reason
        + ")"
      );
    }
  }

  return true;
}

static auto cliIntentDoctor(const std::string &entryPath) -> int {
  std::printf("[intent] engine=ready\n");
  std::printf("[intent] determinism=enabled\n");
  std::printf("[intent] supported_goals=%s\n", cliIntentSupportedGoalsCsv().c_str());
  CliIntentRuleRegistry registry {};
  const bool hasRegistry = cliIntentLoadRuleRegistry(&registry);
  if (hasRegistry && registry.enabled) {
    const auto totalBudgetText = registry.totalBudget == std::numeric_limits<std::size_t>::max()
      ? std::string("unbounded")
      : std::to_string(registry.totalBudget);
    std::printf(
      "[intent] registry=enabled path=%s total_budget=%s rules=%d\n",
      registry.sourcePath.c_str(),
      totalBudgetText.c_str(),
      static_cast<int>(registry.allowedRules.size())
    );
  } else {
    std::printf("[intent] registry=disabled\n");
  }
  const auto clangPath = cliDetectClang();
  if (clangPath.empty()) {
    std::printf("[intent] toolchain=clang_missing\n");
  } else {
    std::printf("[intent] toolchain=clang_ok\n");
  }
  if (entryPath.empty()) {
    std::printf("[intent] source=not_provided\n");
    return 0;
  }
  const auto source = cliReadText(entryPath);
  if (source.empty()) {
    std::fprintf(stderr, "Error: Empty file or file not found.\n");
    return 1;
  }
  std::vector<IntentEntry> entries {};
  cliIntentParseEntries(source, entryPath, &entries);
  std::string autoGoalError {};
  if (!cliIntentResolveAutoGoals(source, &entries, &autoGoalError)) {
    std::fprintf(stderr, "Intent doctor failed: %s\n", autoGoalError.c_str());
    return 1;
  }
  std::string reason {};
  if (!cliIntentValidateEntries(entries, false, &reason)) {
    std::fprintf(stderr, "Intent doctor failed: %s\n", reason.c_str());
    return 1;
  }
  std::vector<IntentPlan> plans {};
  std::string planError {};
  if (!cliIntentBuildPlans(entries, "min", &plans, &planError)) {
    std::fprintf(stderr, "Intent doctor failed: %s\n", planError.c_str());
    return 1;
  }
  std::printf("[intent] source=%s entries=%d status=ok\n", entryPath.c_str(), static_cast<int>(entries.size()));
  return 0;
}

static auto cliIntentExplain(const std::string &entryPath, bool asJson, const std::string &mode) -> int {
  if (!cliIntentModeValid(mode) || mode == "off") {
    std::fprintf(stderr, "Error: intent explain mode must be min or max.\n");
    return 2;
  }
  const auto source = cliReadText(entryPath);
  if (source.empty()) {
    std::fprintf(stderr, "Error: Empty file or file not found.\n");
    return 1;
  }
  std::vector<IntentEntry> entries {};
  cliIntentParseEntries(source, entryPath, &entries);
  std::string autoGoalError {};
  if (!cliIntentResolveAutoGoals(source, &entries, &autoGoalError)) {
    std::fprintf(stderr, "Intent explain failed: %s\n", autoGoalError.c_str());
    return 1;
  }
  std::string reason {};
  if (!cliIntentValidateEntries(entries, false, &reason)) {
    std::fprintf(stderr, "Intent explain failed: %s\n", reason.c_str());
    return 1;
  }

  std::vector<IntentPlan> plans {};
  plans.reserve(entries.size());
  for (const auto &entry : entries) {
    IntentPlan plan {};
    std::string planError {};
    if (!cliIntentSelectPlanForEntry(entry, mode, &plan, &planError)) {
      plan.intentId = entry.id;
      plan.goal = entry.goal;
      plan.selectedRule = "rule.unsupported";
      plan.verified = false;
      plan.verifyReason = planError;
      plan.candidateCount = 0;
    }
    plans.push_back(std::move(plan));
  }

  if (!asJson) {
    std::printf("Intent explain: %s\n", entryPath.c_str());
    if (entries.empty()) {
      std::printf("  no intent markers found\n");
      return 0;
    }
    for (std::size_t i = 0; i < entries.size(); ++i) {
      const auto &entry = entries[i];
      const auto &plan = plans[i];
      const bool matched = !entry.intentEnabled || cliIntentGoalSupported(entry.goal);
      if (entry.strategy.empty()) {
        std::printf(
          "  - %s line=%d kind=%s goal=%s enabled=%s\n",
          entry.id.c_str(),
          entry.line,
          entry.kind.c_str(),
          entry.goal.c_str(),
          entry.intentEnabled ? "true" : "false"
        );
      } else {
        std::printf(
          "  - %s line=%d kind=%s goal=%s strategy=%s enabled=%s\n",
          entry.id.c_str(),
          entry.line,
          entry.kind.c_str(),
          entry.goal.c_str(),
          entry.strategy.c_str(),
          entry.intentEnabled ? "true" : "false"
        );
      }
      std::printf("    constraints=%d examples=%d matched=%s selected_rule=%s verify=%s\n",
        static_cast<int>(entry.constraints.size()),
        static_cast<int>(entry.examples.size()),
        matched ? "true" : "false",
        plan.selectedRule.c_str(),
        plan.verified ? "ok" : plan.verifyReason.c_str());
    }
    return 0;
  }

  std::string out {};
  out += "{\n";
  out += "  \"entry\": \"" + cliJsonEscape(entryPath) + "\",\n";
  out += "  \"mode\": \"" + cliJsonEscape(mode) + "\",\n";
  out += "  \"entries\": [\n";
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto &entry = entries[i];
    const auto &plan = plans[i];
    const bool matched = !entry.intentEnabled || cliIntentGoalSupported(entry.goal);
    out += "    {\n";
    out += "      \"intent_id\": \"" + cliJsonEscape(entry.id) + "\",\n";
    out += "      \"line\": " + std::to_string(entry.line) + ",\n";
    out += "      \"kind\": \"" + cliJsonEscape(entry.kind) + "\",\n";
    out += "      \"goal\": \"" + cliJsonEscape(entry.goal) + "\",\n";
    if (!entry.intentEnabled) {
      out += "      \"enabled\": false,\n";
    }
    if (!entry.strategy.empty()) {
      out += "      \"strategy\": \"" + cliJsonEscape(entry.strategy) + "\",\n";
    }
    out += "      \"matched\": " + std::string(matched ? "true" : "false") + ",\n";
    out += "      \"selected_rule\": \"" + cliJsonEscape(plan.selectedRule) + "\",\n";
    out += "      \"verified\": " + std::string(plan.verified ? "true" : "false") + ",\n";
    out += "      \"verify_reason\": \"" + cliJsonEscape(plan.verifyReason) + "\",\n";
    out += "      \"constraints\": [";
    for (std::size_t k = 0; k < entry.constraints.size(); ++k) {
      if (k > 0) {
        out += ", ";
      }
      out += "\"" + cliJsonEscape(entry.constraints[k]) + "\"";
    }
    out += "],\n";
    out += "      \"candidate_rules\": [";
    for (std::size_t k = 0; k < plan.candidateRules.size(); ++k) {
      if (k > 0) {
        out += ", ";
      }
      out += "\"" + cliJsonEscape(plan.candidateRules[k]) + "\"";
    }
    out += "]\n";
    out += "    }";
    if ((i + 1) < entries.size()) {
      out += ",";
    }
    out += "\n";
  }
  out += "  ]\n";
  out += "}\n";
  std::printf("%s", out.c_str());
  return 0;
}

static auto cliIntentWriteLock(
  const std::string &entryPath,
  const std::string &outputPath,
  const std::string &source,
  const std::vector<IntentPlan> &plans
) -> int {
  std::string out {};
  out += "{\n";
  out += "  \"schema_version\": 1,\n";
  out += "  \"thagore_version\": \"0.5.0\",\n";
  out += "  \"target\": \"" + cliJsonEscape(cliIntentTargetFingerprint()) + "\",\n";
  out += "  \"source_digest\": \"" + cliJsonEscape(cliIntentDigest(source)) + "\",\n";
  out += "  \"entries\": [\n";
  for (std::size_t i = 0; i < plans.size(); ++i) {
    const auto &plan = plans[i];
    out += "    {\n";
    out += "      \"intent_id\": \"" + cliJsonEscape(plan.intentId) + "\",\n";
    out += "      \"goal\": \"" + cliJsonEscape(plan.goal) + "\",\n";
    out += "      \"selected_rule\": \"" + cliJsonEscape(plan.selectedRule) + "\",\n";
    out += "      \"constraints_digest\": \"" + cliJsonEscape(plan.constraintsDigest) + "\",\n";
    out += "      \"verification_digest\": \"" + cliJsonEscape(plan.verificationDigest) + "\"\n";
    out += "    }";
    if ((i + 1) < plans.size()) {
      out += ",";
    }
    out += "\n";
  }
  out += "  ]\n";
  out += "}\n";

  if (!cliWriteText(outputPath, out)) {
    std::fprintf(stderr, "Error: cannot write intent lock file.\n");
    return 1;
  }
  std::printf("Generated intent lock:\n%s\n", outputPath.c_str());
  std::printf("Source:\n%s\n", entryPath.c_str());
  return 0;
}

static auto cliIntentLock(const std::string &entryPath, const std::string &outputOverride, const std::string &mode) -> int {
  if (!cliIntentModeValid(mode) || mode == "off") {
    std::fprintf(stderr, "Error: intent lock mode must be min or max.\n");
    return 2;
  }
  const auto source = cliReadText(entryPath);
  if (source.empty()) {
    std::fprintf(stderr, "Error: Empty file or file not found.\n");
    return 1;
  }
  std::vector<IntentEntry> entries {};
  cliIntentParseEntries(source, entryPath, &entries);
  std::string autoGoalError {};
  if (!cliIntentResolveAutoGoals(source, &entries, &autoGoalError)) {
    std::fprintf(stderr, "Intent lock failed: %s\n", autoGoalError.c_str());
    return 1;
  }
  std::string reason {};
  if (!cliIntentValidateEntries(entries, true, &reason)) {
    std::fprintf(stderr, "Intent lock failed: %s\n", reason.c_str());
    return 1;
  }
  std::vector<IntentPlan> plans {};
  std::string planError {};
  if (!cliIntentBuildPlans(entries, mode, &plans, &planError)) {
    std::fprintf(stderr, "Intent lock failed: %s\n", planError.c_str());
    return 1;
  }
  std::string outputPath = outputOverride;
  if (outputPath.empty()) {
    outputPath = "thagore.intent.lock";
  }
  return cliIntentWriteLock(entryPath, outputPath, source, plans);
}

static auto cliEmitLlvmInternal(const std::string &inputPath, const std::string &outputOverride) -> int {
  const auto source = cliReadText(inputPath);
  if (source.empty()) {
    std::fprintf(stderr, "Error: Empty file or file not found.\n");
    return 1;
  }
  std::string outputLl = outputOverride;
  if (outputLl.empty()) {
    outputLl = cliBasenameNoExt(inputPath) + ".ll";
  }
  const bool previousInternalMode = g_emitLlvmInternalMode;
  g_emitLlvmInternalMode = true;
  const char *ir = __thg_codegen_emit_llvm_from_source(source.c_str(), cliBasenameNoExt(inputPath).c_str());
  g_emitLlvmInternalMode = previousInternalMode;
  if (ir == nullptr || *ir == '\0') {
    std::fprintf(stderr, "Error: LLVM IR generation failed.\n");
    return 1;
  }
  if (!cliWriteText(outputLl, ir)) {
    std::fprintf(stderr, "Error: cannot write LLVM IR file.\n");
    return 1;
  }
  std::printf("Generated LLVM IR:\n%s\n", outputLl.c_str());
  return 0;
}

static auto cliBuildOrEmit(
  const std::string &inputPath,
  const std::string &outputOverride,
  bool emitLlvmOnly,
  const std::string &intentMode,
  const std::string &intentFallbackMode,
  bool strictLock,
  const std::string &intentLockPath
) -> int {
  const auto source = cliReadText(inputPath);
  if (source.empty()) {
    std::fprintf(stderr, "Error: Empty file or file not found.\n");
    return 1;
  }
  if (!cliIntentModeValid(intentMode)) {
    std::fprintf(stderr, "Error: invalid intent mode. Use off|min|max.\n");
    return 2;
  }
  if (!cliIntentFallbackValid(intentFallbackMode)) {
    std::fprintf(stderr, "Error: invalid intent fallback mode. Use deny|allow.\n");
    return 2;
  }
  if (strictLock && intentMode != "max") {
    std::fprintf(stderr, "Error: --strict-lock requires --intent=max.\n");
    return 2;
  }

  const bool allowFallback = intentFallbackMode == "allow";
  std::string sourceForCodegen = source;
  if (intentMode != "off") {
    std::vector<IntentEntry> entries {};
    cliIntentParseEntries(source, inputPath, &entries);
    std::string autoGoalError {};
    if (!cliIntentResolveAutoGoals(source, &entries, &autoGoalError)) {
      if (!allowFallback) {
        std::fprintf(stderr, "Intent auto goal resolution failed: %s\n", autoGoalError.c_str());
        return 1;
      }
      std::fprintf(stderr, "Warning: intent auto goal resolution failed, using fallback path (%s).\n", autoGoalError.c_str());
      entries.clear();
    }
    std::string reason {};
    if (!cliIntentValidateEntries(entries, false, &reason)) {
      if (!allowFallback) {
        std::fprintf(stderr, "Intent validation failed: %s\n", reason.c_str());
        return 1;
      }
      std::fprintf(stderr, "Warning: intent validation failed, using fallback path (%s).\n", reason.c_str());
    } else {
      if (!entries.empty()) {
        std::vector<IntentPlan> plans {};
        std::string planError {};
        if (!cliIntentBuildPlans(entries, intentMode, &plans, &planError)) {
          if (!allowFallback) {
            std::fprintf(stderr, "Intent planner failed: %s\n", planError.c_str());
            return 1;
          }
          std::fprintf(stderr, "Warning: intent planner failed, using fallback path (%s).\n", planError.c_str());
        } else {
          const std::string lockPath = intentLockPath.empty() ? "thagore.intent.lock" : intentLockPath;
          std::vector<std::string> warnings {};
          std::string lockError {};
          if (!cliIntentValidateLockAgainstPlans(
            lockPath,
            cliIntentDigest(source),
            plans,
            strictLock,
            &warnings,
            &lockError
          )) {
            std::fprintf(stderr, "Intent lock validation failed: %s\n", lockError.c_str());
            return 1;
          }
          for (const auto &warning : warnings) {
            std::fprintf(stderr, "Warning: %s\n", warning.c_str());
          }
          std::printf("[intent] mode=%s entries=%d\n", intentMode.c_str(), static_cast<int>(plans.size()));
          for (const auto &plan : plans) {
            std::printf("[intent] %s -> %s\n", plan.intentId.c_str(), plan.selectedRule.c_str());
          }
          std::vector<std::string> rewriteNotes {};
          std::string rewrittenSource {};
          if (cliIntentApplyPlansToSource(sourceForCodegen, entries, plans, &rewrittenSource, &rewriteNotes)) {
            sourceForCodegen = std::move(rewrittenSource);
          }
          for (const auto &note : rewriteNotes) {
            std::printf("[intent] %s\n", note.c_str());
          }
        }
      }
    }
  }
  const auto base = cliBasenameNoExt(inputPath);
  std::string outputLl = base + ".ll";
  std::string outputExe = outputOverride;
  if (emitLlvmOnly) {
    if (!outputOverride.empty()) {
      outputLl = outputOverride;
    }
  } else if (outputExe.empty()) {
#if defined(_WIN32)
    outputExe = base + ".exe";
#else
    outputExe = base;
#endif
  }

  const char *ir = __thg_codegen_emit_llvm_from_source(sourceForCodegen.c_str(), base.c_str());
  if (ir == nullptr || *ir == '\0') {
    std::fprintf(stderr, "Error: LLVM IR generation failed.\n");
    return 1;
  }
  if (!cliWriteText(outputLl, ir)) {
    std::fprintf(stderr, "Error: cannot write LLVM IR file.\n");
    return 1;
  }
  if (emitLlvmOnly) {
    std::printf("Generated LLVM IR:\n%s\n", outputLl.c_str());
    return 0;
  }

  const auto clangBin = cliDetectClang();
  if (clangBin.empty()) {
    std::fprintf(stderr, "CRITICAL: Clang not found via PATH or standard locations. Please install LLVM.\n");
    return 1;
  }
  const auto linkerBin = cliDetectLinker();
  if (linkerBin.empty()) {
    std::fprintf(stderr, "CRITICAL: Clang linker not found via PATH or standard locations. Please install LLVM.\n");
    return 1;
  }
  const auto runtimeLib = cliDetectRuntimeLib();
  if (runtimeLib.empty()) {
    std::fprintf(stderr, "CRITICAL: runtime library not found in standard locations (thag_runtime.lib/libthag_runtime.a).\n");
    return 1;
  }

  std::string linkerExec = linkerBin;
  if (cliHasSpace(linkerExec)) {
    linkerExec = quoteShellArg(linkerExec);
  }
  std::string cmd = linkerExec
    + " "
    + quoteShellArg(outputLl)
    + " "
    + quoteShellArg(runtimeLib)
    + " -o "
    + quoteShellArg(outputExe)
    + " -Wno-override-module";
  if (!cliIsWindows()) {
    if (cliIsMacos()) {
      cmd += " -lc++ -lc++abi";
    } else {
      cmd += " -lstdc++";
    }
  }

  const int code = std::system(cmd.c_str());
  if (code != 0) {
    std::fprintf(stderr, "Build failed. Command: %s\n", cmd.c_str());
    return code;
  }
  std::printf("Build success.\n%s\n", outputExe.c_str());
  return 0;
}

int __thg_cli_main_native() {
  const int argc = __thg_arg_count();
  if (argc < 2) {
    std::printf("Thagore Compiler CLI\n");
    std::printf("Usage:\n");
    std::printf("  thagore build <file.tg> [-o output] [--intent=off|min|max] [--intent-policy=safe|fast|debug] [--intent-fallback=deny|allow] [--intent-lock path] [--strict-lock|--no-strict-lock]\n");
    std::printf("  thagore --emit-llvm <file.tg> [-o output.ll]\n");
    std::printf("  thagore --emit-llvm-internal <file.tg> [-o output.ll]\n");
    std::printf("  thagore intent doctor [entry.tg]\n");
    std::printf("  thagore intent explain <entry.tg> [--json] [--mode min|max]\n");
    std::printf("  thagore intent lock <entry.tg> [-o thagore.intent.lock] [--mode min|max]\n");
    std::printf("  thagore --version\n");
    return 0;
  }

  const std::string arg1 = cstrOrEmpty(__thg_arg_get(1));
  if (arg1 == "--version" || arg1 == "-V") {
    std::printf("thagore 0.5.0\n");
    return 0;
  }
  if (arg1 == "--help" || arg1 == "-h") {
    std::printf("Thagore Compiler CLI\n");
    std::printf("Usage:\n");
    std::printf("  thagore build <file.tg> [-o output] [--intent=off|min|max] [--intent-policy=safe|fast|debug] [--intent-fallback=deny|allow] [--intent-lock path] [--strict-lock|--no-strict-lock]\n");
    std::printf("  thagore --emit-llvm <file.tg> [-o output.ll]\n");
    std::printf("  thagore --emit-llvm-internal <file.tg> [-o output.ll]\n");
    std::printf("  thagore intent doctor [entry.tg]\n");
    std::printf("  thagore intent explain <entry.tg> [--json] [--mode min|max]\n");
    std::printf("  thagore intent lock <entry.tg> [-o thagore.intent.lock] [--mode min|max]\n");
    return 0;
  }

  if (arg1 == "intent") {
    if (argc < 3) {
      std::fprintf(stderr, "Error: missing intent subcommand. Use doctor|explain|lock.\n");
      return 2;
    }
    const std::string sub = cstrOrEmpty(__thg_arg_get(2));
    if (sub == "doctor") {
      std::string entry {};
      if (argc >= 4) {
        entry = cstrOrEmpty(__thg_arg_get(3));
      }
      return cliIntentDoctor(entry);
    }
    if (sub == "explain") {
      if (argc < 4) {
        std::fprintf(stderr, "Error: missing input file.\n");
        return 2;
      }
      std::string entry = cstrOrEmpty(__thg_arg_get(3));
      bool asJson = false;
      std::string mode {"max"};
      for (int i = 4; i < argc; ++i) {
        const std::string arg = cstrOrEmpty(__thg_arg_get(i));
        if (arg == "--json") {
          asJson = true;
        } else if (arg == "--mode" && (i + 1) < argc) {
          mode = cstrOrEmpty(__thg_arg_get(i + 1));
          ++i;
        } else if (arg.rfind("--mode=", 0) == 0) {
          mode = arg.substr(7);
        }
      }
      return cliIntentExplain(entry, asJson, mode);
    }
    if (sub == "lock") {
      if (argc < 4) {
        std::fprintf(stderr, "Error: missing input file.\n");
        return 2;
      }
      std::string entry = cstrOrEmpty(__thg_arg_get(3));
      std::string output {};
      std::string mode {"max"};
      for (int i = 4; i < argc; ++i) {
        const std::string arg = cstrOrEmpty(__thg_arg_get(i));
        if (arg == "-o" && (i + 1) < argc) {
          output = cstrOrEmpty(__thg_arg_get(i + 1));
          ++i;
        } else if (arg == "--mode" && (i + 1) < argc) {
          mode = cstrOrEmpty(__thg_arg_get(i + 1));
          ++i;
        } else if (arg.rfind("--mode=", 0) == 0) {
          mode = arg.substr(7);
        }
      }
      return cliIntentLock(entry, output, mode);
    }
    std::fprintf(stderr, "Error: unknown intent subcommand `%s`.\n", sub.c_str());
    return 2;
  }

  if (arg1 == "--emit-llvm-internal") {
    if (argc < 3) {
      std::fprintf(stderr, "Error: missing input file.\n");
      return 2;
    }
    std::string output {};
    for (int i = 3; i < argc; ++i) {
      const std::string arg = cstrOrEmpty(__thg_arg_get(i));
      if (arg == "-o" && (i + 1) < argc) {
        output = cstrOrEmpty(__thg_arg_get(i + 1));
        ++i;
      }
    }
    return cliEmitLlvmInternal(cstrOrEmpty(__thg_arg_get(2)), output);
  }

  bool buildMode = false;
  bool llvmOnly = false;
  std::string intentMode {"off"};
  std::string intentFallbackMode {"deny"};
  std::string intentPolicy {};
  bool intentModeExplicit = false;
  bool intentFallbackExplicit = false;
  bool strictLockExplicit = false;
  std::string intentLockPath {};
  bool strictLock = false;
  int start = 1;
  if (arg1 == "build") {
    buildMode = true;
    start = 2;
  }

  if (const char *envPolicy = std::getenv("THAG_INTENT_POLICY"); envPolicy != nullptr && envPolicy[0] != '\0') {
    intentPolicy = cliIntentToLower(std::string(trim(std::string_view(envPolicy))));
  }

  std::string script {};
  std::string output {};
  for (int i = start; i < argc; ++i) {
    const std::string arg = cstrOrEmpty(__thg_arg_get(i));
    if (arg == "--emit-llvm" || arg == "-ll") {
      llvmOnly = true;
      continue;
    }
    if (arg == "--intent") {
      if ((i + 1) >= argc) {
        std::fprintf(stderr, "Error: missing value after --intent\n");
        return 2;
      }
      intentMode = cliIntentToLower(cstrOrEmpty(__thg_arg_get(i + 1)));
      intentModeExplicit = true;
      ++i;
      continue;
    }
    if (arg.rfind("--intent=", 0) == 0) {
      intentMode = cliIntentToLower(arg.substr(9));
      intentModeExplicit = true;
      continue;
    }
    if (arg == "--intent-policy") {
      if ((i + 1) >= argc) {
        std::fprintf(stderr, "Error: missing value after --intent-policy\n");
        return 2;
      }
      intentPolicy = cliIntentToLower(cstrOrEmpty(__thg_arg_get(i + 1)));
      ++i;
      continue;
    }
    if (arg.rfind("--intent-policy=", 0) == 0) {
      intentPolicy = cliIntentToLower(arg.substr(16));
      continue;
    }
    if (arg == "--intent-fallback") {
      if ((i + 1) >= argc) {
        std::fprintf(stderr, "Error: missing value after --intent-fallback\n");
        return 2;
      }
      intentFallbackMode = cliIntentToLower(cstrOrEmpty(__thg_arg_get(i + 1)));
      intentFallbackExplicit = true;
      ++i;
      continue;
    }
    if (arg.rfind("--intent-fallback=", 0) == 0) {
      intentFallbackMode = cliIntentToLower(arg.substr(18));
      intentFallbackExplicit = true;
      continue;
    }
    if (arg == "--intent-lock") {
      if ((i + 1) >= argc) {
        std::fprintf(stderr, "Error: missing path after --intent-lock\n");
        return 2;
      }
      intentLockPath = cstrOrEmpty(__thg_arg_get(i + 1));
      ++i;
      continue;
    }
    if (arg.rfind("--intent-lock=", 0) == 0) {
      intentLockPath = arg.substr(14);
      continue;
    }
    if (arg == "--strict-lock") {
      strictLock = true;
      strictLockExplicit = true;
      continue;
    }
    if (arg == "--no-strict-lock") {
      strictLock = false;
      strictLockExplicit = true;
      continue;
    }
    if (arg == "-o") {
      if ((i + 1) >= argc) {
        std::fprintf(stderr, "Error: missing output path after -o\n");
        return 2;
      }
      output = cstrOrEmpty(__thg_arg_get(i + 1));
      ++i;
      continue;
    }
    if (script.empty() && arg != "build") {
      script = arg;
    }
  }
  if (script.empty()) {
    std::fprintf(stderr, "Error: missing input file.\n");
    return 2;
  }
  if (!cliIntentPolicyValid(intentPolicy)) {
    std::fprintf(stderr, "Error: invalid intent policy. Use safe|fast|debug.\n");
    return 2;
  }
  cliIntentApplyPolicyDefaults(
    intentPolicy,
    intentModeExplicit,
    intentFallbackExplicit,
    strictLockExplicit,
    &intentMode,
    &intentFallbackMode,
    &strictLock
  );
  if (buildMode) {
    return cliBuildOrEmit(script, output, llvmOnly, intentMode, intentFallbackMode, strictLock, intentLockPath);
  }
  return cliBuildOrEmit(script, output, llvmOnly, intentMode, intentFallbackMode, strictLock, intentLockPath);
}

int __thg_forward_to_stage1(int argc, void *argvPtr) {
  const char **argv = static_cast<const char **>(argvPtr);
  std::filesystem::path stage1Path {};
#if defined(_WIN32)
  const std::vector<std::filesystem::path> candidates {
    std::filesystem::path {"stage1.exe"},
    std::filesystem::path {"thagore-stage1.exe"},
    std::filesystem::path {"thagore.exe"},
    std::filesystem::path {"thag.exe"},
  };
#else
  const std::vector<std::filesystem::path> candidates {
    std::filesystem::path {"./stage1"},
    std::filesystem::path {"stage1"},
    std::filesystem::path {"./thagore"},
    std::filesystem::path {"thagore"},
  };
#endif
  for (const auto &candidate : candidates) {
    if (std::filesystem::exists(candidate)) {
      stage1Path = candidate;
      break;
    }
  }
  if (stage1Path.empty()) {
    std::fprintf(stderr, "CRITICAL: stage1 bootstrap binary not found for proxy mode.\n");
    return 127;
  }
  std::string command {};
#if defined(_WIN32)
  command = stage1Path.string();
#else
  command = quoteShellArg(stage1Path.string());
#endif
  for (int i = 1; i < argc; ++i) {
    const char *arg = argv != nullptr ? argv[i] : "";
    command.push_back(' ');
#if defined(_WIN32)
    command += cstrOrEmpty(arg);
#else
    command += quoteShellArg(cstrOrEmpty(arg));
#endif
  }
  const int code = std::system(command.c_str());
  return code;
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
  const bool isMainWrapperLike =
    sourceText.find("func main") != std::string::npos
    && sourceText.find("import ") != std::string::npos
    && sourceText.find(".main()") != std::string::npos;
  if (isMainWrapperLike
      || (sourceText.find("func main") != std::string::npos
          && sourceText.find("__thg_cli_main_native") != std::string::npos)) {
    std::string out {};
    out += "declare void @__thg_init_env(i32, ptr)\n";
    out += "declare i32 @__thg_cli_main_native()\n\n";
    out += "define i32 @main(i32 %argc, ptr %argv) {\n";
    out += "entry:\n";
    out += "  call void @__thg_init_env(i32 %argc, ptr %argv)\n";
    out += "  %code = call i32 @__thg_cli_main_native()\n";
    out += "  ret i32 %code\n";
    out += "}\n";
    return makeManagedString(out);
  }

  // Internal emit mode must avoid helper recursion.
  // Keep parser fallback here as a guarded compatibility path until
  // runtime-native emitter can fully replace helper-based bootstrap.
  if (isInternalEmitMode()) {
    auto *fallback = parseProgramSource(source);
    if (!isFallbackProgramSupported(fallback)) {
      std::fprintf(
        stderr,
        "internal emitter fallback only supports simple print-only main(). Provide stage1/stage2 helper binary.\n"
      );
      return makeManagedCString("");
    }
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

  // Guard helper recursion on repeatedly-lowered wrapper names like
  // thagore_0_41_0_41_...; in this case, avoid spawning helper again.
  int loweredDepth = 0;
  std::size_t pos = 0;
  while ((pos = moduleNameText.find("_0_41", pos)) != std::string::npos) {
    ++loweredDepth;
    pos += 5;
  }
  if (loweredDepth >= 3) {
    auto *fallback = parseProgramSource(source);
    if (!isFallbackProgramSupported(fallback)) {
      std::fprintf(
        stderr,
        "codegen helper recursion guard hit unsupported source. Provide stage1/stage2 helper binary.\n"
      );
      return makeManagedCString("");
    }
    return __thg_codegen_emit_llvm(fallback);
  }

  std::string tempStem = moduleNameText;
  if (tempStem.size() > 48) {
    std::uint32_t hash = 2166136261u;
    for (unsigned char ch : moduleNameText) {
      hash ^= static_cast<std::uint32_t>(ch);
      hash *= 16777619u;
    }
    tempStem = moduleNameText.substr(0, 24) + "_" + std::to_string(hash);
  }
  const auto nonce = std::to_string(__time_now_ms()) + "_" + std::to_string(std::rand());
  const auto sourcePath = std::filesystem::path(tempStem + "_" + nonce + ".tg");
  const auto irPath = std::filesystem::path(tempStem + "_" + nonce + ".ll");

  {
    std::ofstream out(sourcePath, std::ios::binary);
    if (!out) {
      return makeManagedCString("");
    }
    out << sourceText;
  }

  const std::filesystem::path selfPath = resolveSelfExecutablePath();
  std::filesystem::path helperPath {};
  std::size_t dynamicCount = 0;
  auto parseEnvInt = [](const char *key, int fallback) -> int {
    const char *value = std::getenv(key);
    if (value == nullptr || value[0] == '\0') {
      return fallback;
    }
    int out = 0;
    std::size_t i = 0;
    while (value[i] != '\0') {
      const char ch = value[i];
      if (ch < '0' || ch > '9') {
        return fallback;
      }
      out = (out * 10) + static_cast<int>(ch - '0');
      if (out > 1000) {
        return fallback;
      }
      i = i + 1;
    }
    return out;
  };
  const int helperDepth = parseEnvInt("THAG_HELPER_DEPTH", 0);
  int helperTimeoutMs = parseEnvInt("THAG_HELPER_TIMEOUT_MS", 45000);
  if (helperTimeoutMs < 1000) {
    helperTimeoutMs = 1000;
  }

  if (helperDepth >= 4) {
    std::error_code rmErr {};
    std::filesystem::remove(sourcePath, rmErr);
    std::filesystem::remove(irPath, rmErr);
    std::fprintf(
      stderr,
      "codegen helper recursion depth exceeded (THAG_HELPER_DEPTH=%d).\n",
      helperDepth
    );
    return makeManagedCString("");
  }
  if (const char *configured = std::getenv("THAG_HELPER_BIN"); configured != nullptr && *configured != '\0') {
    helperPath = std::filesystem::path(configured);
  } else {
#if defined(_WIN32)
  const std::vector<std::filesystem::path> candidates {
    std::filesystem::path {".\\stage2.exe"},
    std::filesystem::path {".\\thagore.exe"},
    std::filesystem::path {"stage2.exe"},
    std::filesystem::path {"thagore.exe"},
    std::filesystem::path {"stage1.exe"},
    std::filesystem::path {"bin/stage2.exe"},
    std::filesystem::path {"bin/thagore.exe"},
    std::filesystem::path {"bin/stage1.exe"},
    std::filesystem::path {"build/stage2.exe"},
    std::filesystem::path {"build/thagore.exe"},
    std::filesystem::path {"build/stage1.exe"},
  };
#else
  const std::vector<std::filesystem::path> candidates {
    std::filesystem::path {"./stage2"},
    std::filesystem::path {"./thagore"},
    std::filesystem::path {"stage2.exe"},
    std::filesystem::path {"thagore.exe"},
    std::filesystem::path {"stage2"},
    std::filesystem::path {"thagore"},
    std::filesystem::path {"stage1"},
    std::filesystem::path {"build/stage2"},
    std::filesystem::path {"build/thagore"},
    std::filesystem::path {"build/stage1"},
    std::filesystem::path {"bin/stage2"},
    std::filesystem::path {"bin/thagore"},
    std::filesystem::path {"bin/stage1"},
    std::filesystem::path {"stage1.exe"},
  };
#endif
  auto samePath = [&](const std::filesystem::path &a, const std::filesystem::path &b) -> bool {
    if (a.empty() || b.empty()) {
      return false;
    }
    std::error_code ecA {};
    std::error_code ecB {};
    const auto ca = std::filesystem::weakly_canonical(a, ecA);
    const auto cb = std::filesystem::weakly_canonical(b, ecB);
    if (ecA || ecB) {
      return false;
    }
    return ca == cb;
  };

  const bool internalEmit = []() {
    const char *v = std::getenv("THAGORE_INTERNAL_EMIT");
    return v != nullptr && v[0] != '\0' && std::string(v) != "0";
  }();
  const bool allowSelfHelper = []() {
    const char *v = std::getenv("THAG_ALLOW_SELF_HELPER");
    return v != nullptr && v[0] != '\0' && std::string(v) != "0";
  }();
  std::vector<std::filesystem::path> dynamicCandidates {};
  if (!selfPath.empty()) {
    const auto selfDir = selfPath.parent_path();
    if (!selfDir.empty()) {
#if defined(_WIN32)
      dynamicCandidates.push_back(selfDir / "stage2.exe");
      dynamicCandidates.push_back(selfDir / "thagore.exe");
      dynamicCandidates.push_back(selfDir / "stage1.exe");
#else
      dynamicCandidates.push_back(selfDir / "stage2");
      dynamicCandidates.push_back(selfDir / "thagore");
      dynamicCandidates.push_back(selfDir / "stage1");
#endif
      dynamicCount = dynamicCandidates.size();
    }
  }

  if ((helperPath.empty() || !std::filesystem::exists(helperPath)) && !selfPath.empty()) {
    const auto selfFile = selfPath.filename().string();
    if (selfFile.rfind("stage3", 0) == 0) {
      std::string preferredName = "stage2" + selfFile.substr(6);
      const auto preferred = selfPath.parent_path() / preferredName;
      if (std::filesystem::exists(preferred) && !samePath(preferred, selfPath)) {
        helperPath = preferred;
      }
    }
  }

  if (helperPath.empty() || !std::filesystem::exists(helperPath)) {
    auto pickFrom = [&](const std::vector<std::filesystem::path> &pool) {
      for (const auto &candidate : pool) {
        if (std::filesystem::exists(candidate) && !samePath(candidate, selfPath)) {
          helperPath = candidate;
          return true;
        }
      }
      return false;
    };
    if (!pickFrom(dynamicCandidates)) {
      (void)pickFrom(candidates);
    }
  }

  if ((helperPath.empty() || !std::filesystem::exists(helperPath))
      && allowSelfHelper
      && !internalEmit
      && !selfPath.empty()
      && std::filesystem::exists(selfPath)) {
    helperPath = selfPath;
  }

  if (!allowSelfHelper && !helperPath.empty() && samePath(helperPath, selfPath)) {
    helperPath.clear();
  }

  if (internalEmit && !helperPath.empty() && samePath(helperPath, selfPath)) {
    std::error_code rmErr {};
    std::filesystem::remove(sourcePath, rmErr);
    std::filesystem::remove(irPath, rmErr);
    std::fprintf(
      stderr,
      "codegen helper recursion blocked: internal emit cannot re-enter same executable.\n"
    );
    return makeManagedCString("");
  }
  }

  if (helperPath.empty() || !std::filesystem::exists(helperPath)) {
    std::error_code rmErr {};
    std::filesystem::remove(sourcePath, rmErr);
    std::filesystem::remove(irPath, rmErr);
    std::fprintf(
      stderr,
      "codegen helper missing for import/use source. Set THAG_HELPER_BIN to stage1/stage2 binary (or THAG_ALLOW_SELF_HELPER=1 for debug fallback).\n"
    );
    return makeManagedCString("");
  }

  const bool traceHelper = []() {
    const char *v = std::getenv("THAG_TRACE_HELPER");
    return v != nullptr && v[0] != '\0' && std::string(v) != "0";
  }();
  const int nextHelperDepth = helperDepth + 1;
  const std::string helperDepthValue = std::to_string(nextHelperDepth);
  if (traceHelper) {
    std::fprintf(
      stderr,
      "[helper] self=%s selected=%s module=%s dynamic=%zu depth=%d\n",
      selfPath.string().c_str(),
      helperPath.string().c_str(),
      moduleNameText.c_str(),
      dynamicCount,
      helperDepth
    );
  }

  auto helperExec = formatExecPathForShell(helperPath);
  const auto sourceArg = quoteShellArg(sourcePath.string());
  const auto irArg = quoteShellArg(irPath.string());
  const auto sourceArgCompat = sourcePath.string();
  const auto irArgCompat = irPath.string();
  const bool helperIsSelf = [&]() {
    if (helperPath.empty() || selfPath.empty()) {
      return false;
    }
    std::error_code ecA {};
    std::error_code ecB {};
    const auto ca = std::filesystem::weakly_canonical(helperPath, ecA);
    const auto cb = std::filesystem::weakly_canonical(selfPath, ecB);
    return !ecA && !ecB && ca == cb;
  }();
#if defined(_WIN32)
  std::string helperExecWin = helperPath.string();
  if (helperExecWin.rfind("./", 0) == 0 || helperExecWin.rfind(".\\", 0) == 0) {
    helperExecWin = helperExecWin.substr(2);
  }
  helperExecWin = quoteShellArg(helperExecWin);
  std::vector<std::string> helperCommands {};
  if (helperIsSelf) {
    helperCommands.push_back(
      "set \"THAGORE_INTERNAL_EMIT=1\" && set \"THAG_HELPER_DEPTH=" + helperDepthValue + "\" && " + helperExecWin + " --emit-llvm-internal " + sourceArg + " -o " + irArg
    );
  } else {
    helperCommands.push_back(helperExecWin + " " + sourceArgCompat + " --emit-llvm -o " + irArgCompat);
    helperCommands.push_back(helperExecWin + " build " + sourceArgCompat + " --emit-llvm -o " + irArgCompat);
    helperCommands.push_back(
      "set \"THAGORE_INTERNAL_EMIT=1\" && set \"THAG_HELPER_DEPTH=" + helperDepthValue + "\" && " + helperExecWin + " --emit-llvm-internal " + sourceArg + " -o " + irArg
    );
  }
#else
  std::vector<std::string> helperCommands {};
  if (helperIsSelf) {
    helperCommands.push_back(
      "THAGORE_INTERNAL_EMIT=1 THAG_HELPER_DEPTH=" + helperDepthValue + " " + helperExec + " --emit-llvm-internal " + sourceArg + " -o " + irArg
    );
  } else {
    helperCommands.push_back(helperExec + " " + sourceArgCompat + " --emit-llvm -o " + irArgCompat);
    helperCommands.push_back(helperExec + " build " + sourceArgCompat + " --emit-llvm -o " + irArgCompat);
    helperCommands.push_back(
      "THAGORE_INTERNAL_EMIT=1 THAG_HELPER_DEPTH=" + helperDepthValue + " " + helperExec + " --emit-llvm-internal " + sourceArg + " -o " + irArg
    );
  }
#endif

  bool commandOk = false;
  std::string lastCommand {};
  for (const auto &command : helperCommands) {
    if (traceHelper) {
      std::fprintf(stderr, "[helper-cmd] %s\n", command.c_str());
    }
    lastCommand = command;
    const int code = runCommandMaybeTimed(command, helperTimeoutMs);
    if (code == 124) {
      std::fprintf(stderr, "codegen helper timeout (%d ms): %s\n", helperTimeoutMs, command.c_str());
    }
    if (code == 0 && std::filesystem::exists(irPath)) {
      commandOk = true;
      break;
    }
    std::error_code rmIrErr {};
    std::filesystem::remove(irPath, rmIrErr);
  }

  if (!commandOk) {
    std::fprintf(stderr, "codegen helper command failed: %s\n", lastCommand.c_str());
    std::error_code rmErr {};
    std::filesystem::remove(sourcePath, rmErr);
    std::filesystem::remove(irPath, rmErr);
    return makeManagedCString("");
  }

  std::ifstream in(irPath, std::ios::binary);
  if (!in) {
    std::error_code rmErr {};
    std::filesystem::remove(sourcePath, rmErr);
    std::filesystem::remove(irPath, rmErr);
    return makeManagedCString("");
  }
  std::string ir((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  {
    std::string normalized {};
    normalized.reserve(ir.size());
    for (std::size_t i = 0; i < ir.size(); ++i) {
      if (ir[i] == '\\' && (i + 1) < ir.size() && ir[i + 1] == 'n') {
        normalized.push_back('\n');
        ++i;
      } else {
        normalized.push_back(ir[i]);
      }
    }
    ir.swap(normalized);
  }

  std::error_code rmErr {};
  std::filesystem::remove(sourcePath, rmErr);
  std::filesystem::remove(irPath, rmErr);

  return makeManagedString(ir);
}

int __thg_eval_expr(void *nodePtr) {
  return evalExprWithEnv(static_cast<AstNode *>(nodePtr), nullptr);
}

void *__thg_interp_new() {
  auto *interp = new RuntimeInterpreter {};
  interp->strict = false;
  interp->hadError = false;
  return interp;
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
  if (interp != nullptr) {
    interp->hadError = false;
  }
  return evalExprWithEnv(static_cast<AstNode *>(nodePtr), interp);
}

int __thg_interp_exec_stmt(void *interpPtr, void *nodePtr) {
  auto *interp = static_cast<RuntimeInterpreter *>(interpPtr);
  if (interp != nullptr) {
    interp->hadError = false;
  }
  const int value = execStmtWithEnv(static_cast<AstNode *>(nodePtr), interp);
  if (interp != nullptr && interp->strict && interp->hadError) {
    return -1;
  }
  return value;
}

int __thg_interp_set_strict(void *interpPtr, int strict_mode) {
  if (interpPtr == nullptr) {
    return 0;
  }
  auto *interp = static_cast<RuntimeInterpreter *>(interpPtr);
  interp->strict = strict_mode != 0;
  interp->hadError = false;
  return 1;
}

}
