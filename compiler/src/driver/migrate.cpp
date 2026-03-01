#include "thagc/driver/command_handlers.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#include "thagc/shared/filesystem.hpp"

namespace thagc::driver {

namespace {

static bool has_flag(const ParsedCommand& cmd, const std::string& flag) {
  for (const std::string& arg : cmd.args) {
    if (arg == flag) {
      return true;
    }
  }
  return false;
}

static bool copy_if_exists(const std::filesystem::path& src, const std::filesystem::path& dst, std::string& error) {
  std::error_code ec;
  if (!std::filesystem::exists(src, ec) || ec) {
    return false;
  }
  try {
    support::write_text_file(dst.string(), support::read_text_file(src.string()));
  } catch (const std::exception& ex) {
    error = ex.what();
    return false;
  }
  return true;
}

}  // namespace

int handle_migrate(const ParsedCommand& cmd) {
  const std::filesystem::path cwd = std::filesystem::current_path();
  const std::filesystem::path legacy_manifest = cwd / "thagore.toml";
  const std::filesystem::path new_manifest = cwd / "drago.toml";
  const std::filesystem::path legacy_lock = cwd / "thagore.lock";
  const std::filesystem::path new_lock = cwd / "drago.lock";
  const bool force = has_flag(cmd, "--force");

  std::error_code ec;
  if (!std::filesystem::exists(legacy_manifest, ec) || ec) {
    std::cerr << "ERROR: thagore.toml not found in current directory\n";
    return 1;
  }
  ec.clear();
  if (!force && std::filesystem::exists(new_manifest, ec) && !ec) {
    std::cerr << "ERROR: drago.toml already exists; rerun with --force to overwrite\n";
    return 1;
  }

  try {
    support::write_text_file(new_manifest.string(), support::read_text_file(legacy_manifest.string()));
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: cannot write drago.toml: " << ex.what() << "\n";
    return 1;
  }

  std::string lock_error;
  const bool lock_copied = copy_if_exists(legacy_lock, new_lock, lock_error);
  if (!lock_error.empty()) {
    std::cerr << "ERROR: cannot write drago.lock: " << lock_error << "\n";
    return 1;
  }

  std::cout << "migrate: wrote " << new_manifest.string() << "\n";
  if (lock_copied) {
    std::cout << "migrate: wrote " << new_lock.string() << "\n";
  }
  std::cout << "migrate: next step use drago build/run/test\n";
  return 0;
}

}  // namespace thagc::driver

