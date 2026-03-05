#include "thagc/driver/command_handlers.hpp"

#include <iostream>
#include <string>
#include <unordered_map>

#include "thagc/shared/diag.hpp"

namespace thagc::driver {

namespace {

static const std::unordered_map<std::string, std::string> kDiagnosticExplanations = {
    {"E0001", "Empty source input. Provide non-empty source text or a valid input file path."},
    {"E0002", "Missing entrypoint. Define `func main():` or add valid top-level executable statements."},
    {"E0003", "Invalid function header syntax. Use `func name(args):` with a trailing colon."},
    {"E0011", "Expression parse/type failure in the current statement context."},
    {"E0014", "Invalid condition type. `if`/`while` conditions must evaluate to `bool`."},
    {"E0029", "Conflicting entry style. Choose either `func main()` or top-level executable statements."},
    {"E_MOD_002", "Import resolution failed. Verify module path, include paths, and manifest dependency declarations."},
    {"E_MOD_201", "Circular import detected in the module graph. Break cycles by extracting shared logic."},
    {"E_MOD_203", "Imported module contains top-level executable statements, which are disallowed for dependencies."},
    {"E_MOD_205", "Requested symbol is not exported by the target module."},
    {"E_MOD_206", "Import alias/prefix collision detected. Use explicit aliases to disambiguate modules."},
    {"E_MOD_208", "Attempted to use a private symbol from another module. Mark it `pub` in the exporting module."},
    {"E_SEND_SYNC_004", "Type fails Send/Sync safety at task/thread boundary; use thread-safe ownership types (for example `Arc`)."},
};

static std::string normalize_code(std::string code) {
  for (char& ch : code) {
    if (ch >= 'a' && ch <= 'z') {
      ch = static_cast<char>(ch - ('a' - 'A'));
    }
  }
  return code;
}

}  // namespace

int handle_explain(const ParsedCommand& cmd) {
  if (cmd.args.empty()) {
    std::cerr << "ERROR: missing error code. Usage: thagc --explain <ERROR_CODE>\n";
    return 1;
  }
  const std::string code = normalize_code(cmd.args.front());
  auto it = kDiagnosticExplanations.find(code);
  if (it == kDiagnosticExplanations.end()) {
    std::cout << code << "\n";
    std::cout << "No extended explanation registered yet.\n";
    support::Diagnostic d;
    d.code = code;
    const std::string hint = support::diagnostic_fix_suggestion(d);
    if (!hint.empty()) {
      std::cout << "help: " << hint << "\n";
    }
    return 0;
  }

  std::cout << code << "\n";
  std::cout << it->second << "\n";
  support::Diagnostic d;
  d.code = code;
  const std::string hint = support::diagnostic_fix_suggestion(d);
  if (!hint.empty()) {
    std::cout << "help: " << hint << "\n";
  }
  return 0;
}

}  // namespace thagc::driver
