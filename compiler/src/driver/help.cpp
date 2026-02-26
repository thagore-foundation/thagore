#include "thagc/driver/command_handlers.hpp"

#include <iostream>

namespace thagc::driver {

int handle_help() {
  std::cout << "thagc 0.1.0\n";
  std::cout << "Usage:\n";
  std::cout << "  thagc --help\n";
  std::cout << "  thagc --version\n";
  std::cout << "  thagc build <input.tg> -o <output> [--emit-llvm]\n";
  std::cout << "  thagc run <input.tg> [-o <output>] [--emit-llvm]\n";
  std::cout << "  thagc test <input.tg> [-o <output>] [--emit-llvm]\n";
  std::cout << "  thagc fix|intent|state|install|target|update|flow ...\n";
  return 0;
}

int handle_version() {
  std::cout << "thagc 0.1.0\n";
  return 0;
}

}  // namespace thagc::driver

