#include "thagc/driver/command_handlers.hpp"

#include <iostream>

#include "thagc/driver/common.hpp"
#include "thagc/shared/filesystem.hpp"

namespace thagc::driver {

int handle_update(const ParsedCommand& cmd) {
  if (cmd.args.empty()) {
    std::cerr << "ERROR: update requires subcommand (check|apply|rollback)\n";
    return 1;
  }
  const std::string sub = cmd.args[0];
  if (sub != "check" && sub != "apply" && sub != "rollback") {
    std::cerr << "ERROR: unknown update subcommand: " << sub << "\n";
    return 1;
  }
  const std::string home = compiler_home_dir();
  if (sub == "check") {
    std::cout << "update check: no remote updater wired in this milestone\n";
    return 0;
  }
  if (sub == "apply") {
    support::write_text_file(home + "/update-state.txt", "applied\n");
    std::cout << "update apply: recorded local update state\n";
    return 0;
  }
  support::write_text_file(home + "/update-state.txt", "rollback\n");
  std::cout << "update rollback: recorded rollback state\n";
  return 0;
}

}  // namespace thagc::driver

