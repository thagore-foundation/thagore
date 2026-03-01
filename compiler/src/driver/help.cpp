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
  std::cout << "  thagc --help\n";
  std::cout << "  thagc --version\n";
  std::cout << "  thagc <input.tg> [options]\n";
  std::cout << "  thagc build <input.tg> [-o <output>] [--target=<triple>] [--emit-llvm] "
               "[--link-lib=<name>] [--link-dir=<dir>] [--link-arg=<arg>] [--include-path=<dir>]\n";
  std::cout << "  thagc run <input.tg> [-o <output>] [--target=<triple>] [--emit-llvm] "
               "[--link-lib=<name>] [--link-dir=<dir>] [--link-arg=<arg>] [--include-path=<dir>]\n";
  std::cout << "  thagc check <input.tg> [--target=<triple>] [--include-path=<dir>]\n";
  std::cout << "  thagc fmt <input.tg>\n";
  std::cout << "  thagc migrate [--force]  # convert legacy manifest to drago.toml\n";
  std::cout << "  Use drago for package/update flows: drago add/install/update/build/run/test\n";
  return 0;
}

int handle_version() {
  std::cout << "thagore " << display_compiler_version() << "\n";
  return 0;
}

}  // namespace thagc::driver
