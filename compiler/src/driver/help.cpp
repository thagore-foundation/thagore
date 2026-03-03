#include "thagc/driver/command_handlers.hpp"

#include <cctype>
#include <iostream>
#include <string>

#include "thagc/shared/version.hpp"

namespace thagc::driver {

static std::string display_compiler_version() {
  // Version must reflect the running binary, not mutable updater state files.
  return std::string(support::kCompilerVersion);
}

int handle_help() {
  std::cout << "thagore " << display_compiler_version() << "\n";
  std::cout << "Usage:\n";
  std::cout << "  thagc --help\n";
  std::cout << "  thagc --version\n";
  std::cout << "  thagc <input.tg> [options]\n";
  std::cout << "  thagc build <input.tg> [-o <output>] [--target=<triple>] [--emit-llvm] "
               "[--link-lib=<name>] [--link-dir=<dir>] [--link-arg=<arg>] [--include-path=<dir>]\n";
  std::cout << "  thagc run <input.tg> [-o <output>] [--target=<triple>] [--emit-llvm] "
               "[--link-lib=<name>] [--link-dir=<dir>] [--link-arg=<arg>] [--include-path=<dir>]\n";
  std::cout << "  thagc check <input.tg> [--target=<triple>] [--include-path=<dir>]\n";
  std::cout << "  thagc fmt <input.tg>\n";
  std::cout << "  thagc fix <input.tg>  # apply safe autofixes for common syntax issues\n";
  std::cout << "  thagc repl            # interactive Thagore shell\n";
  std::cout << "  thagc lsp [--stdio]   # start language server (LSP)\n";
  std::cout << "  thagc target list|show <triple>|init <triple>\n";
  std::cout << "  thagc intent explain|doctor <input.tg> [--json]\n";
  std::cout << "  thagc state explain <input.tg> [--json] [--out <file>] | doctor <input.tg>\n";
  std::cout << "  thagc migrate [--force]  # convert legacy manifest to drago.toml\n";
  std::cout << "  Use drago for package/update flows: drago add/install/update/build/run/test\n";
  return 0;
}

int handle_version() {
  std::cout << "thagore " << display_compiler_version() << "\n";
  return 0;
}

}  // namespace thagc::driver
