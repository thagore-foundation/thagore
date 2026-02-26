#include "thagc/driver/command_handlers.hpp"

#include <filesystem>
#include <iostream>

#include "thagc/driver/common.hpp"
#include "thagc/shared/filesystem.hpp"

namespace thagc::driver {

static std::string targets_dir() {
  return compiler_home_dir() + "/targets";
}

int handle_target(const ParsedCommand& cmd) {
  if (cmd.args.empty()) {
    std::cerr << "ERROR: target requires subcommand\n";
    return 1;
  }
  std::filesystem::create_directories(targets_dir());
  const std::string sub = cmd.args[0];
  if (sub == "list" || sub == "installed") {
    for (const auto& it : std::filesystem::directory_iterator(targets_dir())) {
      if (it.is_directory()) {
        std::cout << it.path().filename().string() << "\n";
      }
    }
    return 0;
  }
  if (sub == "add" || sub == "ensure") {
    if (cmd.args.size() < 2) {
      std::cerr << "ERROR: target " << sub << " requires triple\n";
      return 1;
    }
    const std::string triple = cmd.args[1];
    std::filesystem::create_directories(targets_dir() + "/" + triple);
    std::cout << "target " << sub << ": " << triple << "\n";
    return 0;
  }
  if (sub == "remove") {
    if (cmd.args.size() < 2) {
      std::cerr << "ERROR: target remove requires triple\n";
      return 1;
    }
    const std::string triple = cmd.args[1];
    std::filesystem::remove_all(targets_dir() + "/" + triple);
    std::cout << "target remove: " << triple << "\n";
    return 0;
  }
  if (sub == "doctor") {
    std::cout << "target doctor: OK\n";
    return 0;
  }
  std::cerr << "ERROR: unknown target subcommand: " << sub << "\n";
  return 1;
}

}  // namespace thagc::driver

