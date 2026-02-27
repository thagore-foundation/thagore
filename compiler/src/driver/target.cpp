#include "thagc/driver/command_handlers.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "thagc/driver/common.hpp"
#include "thagc/shared/filesystem.hpp"

namespace thagc::driver {

static std::string targets_dir() {
  return compiler_home_dir() + "/targets";
}

static std::string target_dir(const std::string& triple) {
  return targets_dir() + "/" + triple;
}

static std::string target_manifest(const std::string& triple) {
  return target_dir(triple) + "/manifest.json";
}

static std::string arg_value(const std::vector<std::string>& args, const std::string& key, const std::string& fallback) {
  const std::string prefix = key + "=";
  for (const std::string& arg : args) {
    if (arg.rfind(prefix, 0) == 0) {
      return arg.substr(prefix.size());
    }
  }
  return fallback;
}

static bool file_executable(const std::string& path) {
  std::error_code ec;
  auto st = std::filesystem::status(path, ec);
  if (ec || !std::filesystem::exists(st)) {
    return false;
  }
  auto perm = st.permissions();
  return (perm & std::filesystem::perms::owner_exec) != std::filesystem::perms::none ||
         (perm & std::filesystem::perms::group_exec) != std::filesystem::perms::none ||
         (perm & std::filesystem::perms::others_exec) != std::filesystem::perms::none;
}

static std::string resolve_from_path(const std::string& tool) {
  if (tool.empty()) {
    return "";
  }
  std::error_code ec;
  std::filesystem::path p(tool);
  if (p.has_parent_path()) {
    if (file_executable(tool)) {
      return std::filesystem::absolute(tool, ec).string();
    }
    return "";
  }
  const char* path_env = std::getenv("PATH");
  if (path_env == nullptr) {
    return "";
  }
  std::stringstream ss(path_env);
  std::string segment;
  while (std::getline(ss, segment, ':')) {
    std::filesystem::path candidate = std::filesystem::path(segment) / tool;
    if (file_executable(candidate.string())) {
      return candidate.string();
    }
  }
  return "";
}

static std::string manifest_json(const std::string& triple, const std::string& cc, const std::string& cxx,
                                 const std::string& linker, const std::string& sysroot) {
  std::ostringstream out;
  out << "{\n"
      << "  \"triple\": \"" << triple << "\",\n"
      << "  \"cc\": \"" << cc << "\",\n"
      << "  \"cxx\": \"" << cxx << "\",\n"
      << "  \"linker\": \"" << linker << "\",\n"
      << "  \"sysroot\": \"" << sysroot << "\"\n"
      << "}\n";
  return out.str();
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
        const std::string triple = it.path().filename().string();
        const bool has_manifest = support::file_exists(target_manifest(triple));
        std::cout << triple << (has_manifest ? " [ready]" : " [missing-manifest]") << "\n";
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
    const std::string cc = resolve_from_path(arg_value(cmd.args, "--cc", "clang"));
    const std::string cxx = resolve_from_path(arg_value(cmd.args, "--cxx", "clang++"));
    const std::string linker = resolve_from_path(arg_value(cmd.args, "--linker", "clang"));
    const std::string sysroot = arg_value(cmd.args, "--sysroot", "");
    std::filesystem::create_directories(target_dir(triple));
    support::write_text_file(target_manifest(triple), manifest_json(triple, cc, cxx, linker, sysroot));
    std::cout << "target " << sub << ": " << triple << " (manifest updated)\n";
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
    std::vector<std::string> triples;
    if (cmd.args.size() >= 2) {
      triples.push_back(cmd.args[1]);
    } else {
      for (const auto& it : std::filesystem::directory_iterator(targets_dir())) {
        if (it.is_directory()) {
          triples.push_back(it.path().filename().string());
        }
      }
    }
    if (triples.empty()) {
      std::cout << "target doctor: no targets installed\n";
      return 0;
    }
    bool ok = true;
    for (const std::string& triple : triples) {
      const std::string manifest = target_manifest(triple);
      if (!support::file_exists(manifest)) {
        std::cout << "target doctor: " << triple << " FAIL missing manifest\n";
        ok = false;
        continue;
      }
      std::cout << "target doctor: " << triple << " OK\n";
    }
    return ok ? 0 : 1;
  }
  if (sub == "show") {
    if (cmd.args.size() < 2) {
      std::cerr << "ERROR: target show requires triple\n";
      return 1;
    }
    const std::string triple = cmd.args[1];
    const std::string manifest = target_manifest(triple);
    if (!support::file_exists(manifest)) {
      std::cerr << "ERROR: target not installed: " << triple << "\n";
      return 1;
    }
    std::cout << support::read_text_file(manifest);
    return 0;
  }
  std::cerr << "ERROR: unknown target subcommand: " << sub << "\n";
  return 1;
}

}  // namespace thagc::driver
