#include "thagc/driver/command_handlers.hpp"

#include <iostream>

namespace thagc::driver {

int handle_help() {
  std::cout << "thagore 0.1.0\n";
  std::cout << "Usage:\n";
  std::cout << "  thagore --help\n";
  std::cout << "  thagore --version\n";
  std::cout << "  thagore <input.tg> [options]\n";
  std::cout << "  thagore build <input.tg> [-o <output>] [--emit-llvm]\n";
  std::cout << "  thagore run <input.tg> [-o <output>] [--emit-llvm]\n";
  std::cout << "  thagore test [filter] [--workspace] [--list] [--json] [--no-run] [--fail-fast]\n";
  std::cout << "  thagore fix apply <entry.tg>|--workspace [--level=safe] [--json]\n";
  std::cout << "  thagore target add <triple> [--cc=clang] [--cxx=clang++] [--linker=clang] [--sysroot=...]\n";
  std::cout << "  thagore target doctor [triple] | target show <triple>\n";
  std::cout << "  thagore update check | update apply [--yes] [--dry-run] | update rollback [--yes]\n";
  std::cout << "  thagore fix|intent|state|install|target|update|flow ...\n";
  return 0;
}

int handle_version() {
  std::cout << "thagore 0.1.0\n";
  return 0;
}

}  // namespace thagc::driver
