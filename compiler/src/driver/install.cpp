#include "thagc/driver/command_handlers.hpp"

#include <iostream>

#include "thagc/driver/common.hpp"
#include "thagc/shared/filesystem.hpp"

namespace thagc::driver {

int handle_install(const ParsedCommand& cmd) {
  if (cmd.args.empty() || cmd.args[0] != "toolchain") {
    std::cerr << "ERROR: install supports: install toolchain [--yes]\n";
    return 1;
  }
  const std::string home = compiler_home_dir();
  const std::string marker = home + "/toolchain-installed.txt";
  support::write_text_file(marker, "thagc rewrite toolchain installed\n");
  std::cout << "install toolchain: wrote " << marker << "\n";
  return 0;
}

}  // namespace thagc::driver

