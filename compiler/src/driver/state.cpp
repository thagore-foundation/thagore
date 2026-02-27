#include "thagc/driver/command_handlers.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>

#include "thagc/driver/common.hpp"
#include "thagc/frontend/lexer.hpp"
#include "thagc/frontend/parser.hpp"
#include "thagc/frontend/typechecker.hpp"
#include "thagc/shared/filesystem.hpp"

namespace thagc::driver {

static std::string trim(const std::string& text) {
  std::size_t left = 0;
  while (left < text.size() && std::isspace(static_cast<unsigned char>(text[left]))) {
    ++left;
  }
  std::size_t right = text.size();
  while (right > left && std::isspace(static_cast<unsigned char>(text[right - 1]))) {
    --right;
  }
  return text.substr(left, right - left);
}

int handle_state(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag) {
  (void)pipeline;
  if (cmd.args.size() < 2) {
    std::cerr << "ERROR: state requires subcommand and entry path\n";
    return 1;
  }
  const std::string sub = cmd.args[0];
  const std::string entry = cmd.args[1];
  if (sub != "doctor" && sub != "explain") {
    std::cerr << "ERROR: unknown state subcommand: " << sub << "\n";
    return 1;
  }

  const std::string source = support::read_text_file(entry);
  syntax::Lexer lexer;
  syntax::Parser parser;
  semantics::TypeChecker checker;
  const auto tokens = lexer.tokenize(source);
  const auto program = parser.parse(tokens, source);

  if (sub == "doctor") {
    if (!checker.check(program, diag)) {
      for (const auto& d : diag.diagnostics()) {
        std::cerr << d.code << ": " << d.message << "\n";
      }
      return 1;
    }
    std::cout << "state doctor: OK (" << entry << ")\n";
    return 0;
  }

  support::DiagnosticSink explain_diag;
  const bool type_ok = checker.check(program, explain_diag);
  std::cout << "state explain: " << entry << "\n";
  std::cout << "  functions=" << program.functions.size() << ", structs=" << program.structs.size()
            << ", enums=" << program.enums.size() << ", traits=" << program.traits.size() << "\n";
  std::cout << "  features: match=" << program.match_count << ", range_loop=" << program.range_loop_count
            << ", comptime=" << program.comptime_count << ", visibility=" << program.visibility_count << "\n";
  std::cout << "  typecheck=" << (type_ok ? "ok" : "failed") << "\n";
  if (!type_ok) {
    for (const auto& d : explain_diag.diagnostics()) {
      std::cout << "  diag: " << d.code << " " << trim(d.message) << "\n";
    }
  }
  if (!diag.diagnostics().empty()) {
    for (const auto& d : diag.diagnostics()) {
      std::cerr << d.code << ": " << d.message << "\n";
    }
  }
  return 0;
}

}  // namespace thagc::driver
