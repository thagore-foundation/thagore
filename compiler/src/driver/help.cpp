#include "thagc/driver/command_handlers.hpp"

#include <cctype>
#include <filesystem>
#include <iostream>
#include <string>

#include "thagc/driver/common.hpp"
#include "thagc/shared/filesystem.hpp"
#include "thagc/shared/version.hpp"

namespace thagc::driver {

static std::string trim_copy(const std::string& text) {
  std::size_t begin = 0;
  while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    --end;
  }
  return text.substr(begin, end - begin);
}

static std::string display_compiler_version() {
  bool has_installed = false;
  const std::string primary_home = resolve_update_state_home(&has_installed);
  const std::string fallback_home = has_installed ? compiler_home_dir() : toolchain_home_dir();
  const std::string primary_version_file = (std::filesystem::path(primary_home) / "current-version.txt").string();
  if (support::file_exists(primary_version_file)) {
    const std::string current = trim_copy(support::read_text_file(primary_version_file));
    if (!current.empty()) {
      return current;
    }
  }

  const std::string fallback_version_file = (std::filesystem::path(fallback_home) / "current-version.txt").string();
  if (support::file_exists(fallback_version_file)) {
    const std::string current = trim_copy(support::read_text_file(fallback_version_file));
    if (!current.empty()) {
      return current;
    }
  }
  return std::string(support::kCompilerVersion);
}

int handle_help() {
  std::cout << "thagore " << display_compiler_version() << "\n";
  std::cout << "Usage:\n";
  std::cout << "  thagore --help\n";
  std::cout << "  thagore --version\n";
  std::cout << "  thagore <input.tg> [options]\n";
  std::cout << "  thagore build <input.tg> [-o <output>] [--target=<triple>] [--emit-llvm] "
               "[--link-lib=<name>] [--link-dir=<dir>] [--link-arg=<arg>]\n";
  std::cout << "  thagore run <input.tg> [-o <output>] [--target=<triple>] [--emit-llvm] "
               "[--link-lib=<name>] [--link-dir=<dir>] [--link-arg=<arg>]\n";
  std::cout << "  thagore test [filter] [--workspace] [--list] [--json] [--no-run] [--fail-fast]\n";
  std::cout << "  thagore fix apply <entry.tg>|--workspace [--level=safe] [--json]\n";
  std::cout << "  thagore target add <triple> [--cc=clang] [--cxx=clang++] [--linker=clang] [--sysroot=...]\n";
  std::cout << "  thagore target doctor [triple] | target show <triple>\n";
  std::cout << "  thagore update check | update apply [--yes] [--dry-run] | update rollback [--yes]\n";
  std::cout << "  thagore migrate [--force]  # convert thagore.toml -> drago.toml\n";
  std::cout << "  thagore fix|intent|state|install|target|update|migrate|flow ...\n";
  return 0;
}

int handle_version() {
  std::cout << "thagore " << display_compiler_version() << "\n";
  return 0;
}

}  // namespace thagc::driver
