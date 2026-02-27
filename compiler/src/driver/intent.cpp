#include "thagc/driver/command_handlers.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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

static std::vector<std::string> collect_intent_goals(const std::string& source) {
  std::vector<std::string> goals;
  std::istringstream in(source);
  std::string line;
  while (std::getline(in, line)) {
    const std::string clean = trim(line);
    const std::size_t pos = clean.find("goal:");
    if (pos == std::string::npos) {
      continue;
    }
    goals.push_back(trim(clean.substr(pos + 5)));
  }
  return goals;
}

static bool is_supported_goal(const std::string& goal) {
  return goal == "auto_plan" || goal == "reduce_sum" || goal == "off";
}

int handle_intent(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag) {
  (void)pipeline;
  if (cmd.args.size() < 2) {
    std::cerr << "ERROR: intent requires subcommand and entry path\n";
    return 1;
  }
  const std::string sub = cmd.args[0];
  const std::string entry = cmd.args[1];
  if (sub != "doctor" && sub != "explain" && sub != "lock") {
    std::cerr << "ERROR: unknown intent subcommand: " << sub << "\n";
    return 1;
  }

  const std::string source = support::read_text_file(entry);
  const std::vector<std::string> goals = collect_intent_goals(source);

  syntax::Lexer lexer;
  syntax::Parser parser;
  semantics::TypeChecker checker;
  const auto tokens = lexer.tokenize(source);
  const auto program = parser.parse(tokens, source);
  if (!checker.check(program, diag)) {
    for (const auto& d : diag.diagnostics()) {
      std::cerr << d.code << ": " << d.message << "\n";
    }
    return 1;
  }

  if (sub == "doctor") {
    for (const auto& goal : goals) {
      if (!is_supported_goal(goal)) {
        std::cerr << "ERROR: unsupported intent goal '" << goal << "'\n";
        return 1;
      }
    }
    std::cout << "intent doctor: OK (" << entry << ")\n";
    return 0;
  }

  if (sub == "explain") {
    std::cout << "intent explain: " << entry << "\n";
    if (goals.empty()) {
      std::cout << "  no intent goals found\n";
    } else {
      for (const auto& goal : goals) {
        std::cout << "  goal: " << goal << (is_supported_goal(goal) ? " [supported]" : " [unsupported]") << "\n";
      }
    }
    return 0;
  }

  const std::string lock_path = cmd.output_path.empty() ? (entry + ".intent.lock") : cmd.output_path;
  std::string content = "entry=" + entry + "\n";
  for (const auto& goal : goals) {
    content += "goal=" + goal + "\n";
  }
  support::write_text_file(lock_path, content);
  std::cout << "intent lock: wrote " << lock_path << "\n";
  return 0;
}

}  // namespace thagc::driver
