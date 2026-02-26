#include "thagc/driver/command_handlers.hpp"

#include <iostream>

#include "thagc/driver/common.hpp"
#include "thagc/shared/filesystem.hpp"

namespace thagc::driver {

int handle_fix(const ParsedCommand& cmd) {
  if (cmd.args.empty()) {
    std::cerr << "ERROR: fix requires subcommand (doctor|dry-run|apply|explain|rollback)\n";
    return 1;
  }
  const std::string sub = cmd.args[0];
  if (sub == "doctor") {
    std::cout << "fix doctor: OK\n";
    return 0;
  }
  if (sub == "dry-run") {
    std::cout << "fix dry-run: no changes planned\n";
    return 0;
  }
  if (sub == "apply") {
    if (cmd.args.size() < 2) {
      std::cerr << "ERROR: fix apply requires input path\n";
      return 1;
    }
    const std::string src = cmd.args[1];
    const std::string dst = src + ".fixed";
    const std::string content = support::read_text_file(src);
    support::write_text_file(dst, content);
    std::cout << "fix apply: wrote " << dst << "\n";
    return 0;
  }
  if (sub == "explain") {
    std::cout << "fix explain: rewrite lane active, ruleset in progress\n";
    return 0;
  }
  if (sub == "rollback") {
    std::cout << "fix rollback: no-op in current rewrite milestone\n";
    return 0;
  }
  std::cerr << "ERROR: unknown fix subcommand: " << sub << "\n";
  return 1;
}

}  // namespace thagc::driver

