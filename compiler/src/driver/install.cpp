#include "thagc/driver/command_handlers.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "thagc/driver/common.hpp"
#include "thagc/driver/resolver.hpp"
#include "thagc/shared/filesystem.hpp"
#include "thagc/shared/process.hpp"

namespace thagc::driver {

namespace {

static std::string resolve_path_tool(const std::string& tool) {
  const char* path_env = std::getenv("PATH");
  if (path_env == nullptr) {
    return "";
  }
  std::string path = path_env;
  std::size_t from = 0;
  while (from < path.size()) {
    const std::size_t colon = path.find(':', from);
    const std::string part = colon == std::string::npos ? path.substr(from) : path.substr(from, colon - from);
    std::filesystem::path candidate = std::filesystem::path(part) / tool;
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec)) {
      return candidate.string();
    }
    if (colon == std::string::npos) {
      break;
    }
    from = colon + 1;
  }
  return "";
}

static std::string trim_copy(std::string value) {
  while (!value.empty() &&
         (value.back() == '\n' || value.back() == '\r' || value.back() == ' ' || value.back() == '\t')) {
    value.pop_back();
  }
  std::size_t i = 0;
  while (i < value.size() && (value[i] == ' ' || value[i] == '\t')) {
    ++i;
  }
  return value.substr(i);
}

static bool starts_with(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

static std::string detect_host_triple() {
  int rc = 0;
  std::string triple = trim_copy(support::run_process_capture({"llvm-config", "--host-target"}, &rc));
  if (rc == 0 && !triple.empty()) {
    return triple;
  }
  triple = trim_copy(support::run_process_capture({"clang", "-dumpmachine"}, &rc));
  if (rc == 0 && !triple.empty()) {
    return triple;
  }
  return "x86_64-unknown-linux-gnu";
}

static std::filesystem::path home_dir() {
  const char* home = std::getenv("HOME");
  if (home != nullptr && *home != '\0') {
    return std::filesystem::path(home);
  }
  return std::filesystem::current_path();
}

static std::filesystem::path resolve_dependency_source_path(const std::string& value, const std::string& project_root) {
  std::string path = trim_copy(value);
  if (starts_with(path, "path:")) {
    path = trim_copy(path.substr(5));
  } else if (starts_with(path, "file:")) {
    path = trim_copy(path.substr(5));
  }
  if (path.empty()) {
    return {};
  }
  std::filesystem::path source(path);
  if (source.is_relative()) {
    source = std::filesystem::path(project_root) / source;
  }
  std::error_code ec;
  if (!std::filesystem::exists(source, ec) || ec) {
    return {};
  }
  return std::filesystem::weakly_canonical(source);
}

static std::string read_package_name(const std::filesystem::path& package_dir) {
  const std::filesystem::path manifest = package_dir / "thagore.toml";
  std::error_code ec;
  if (!std::filesystem::exists(manifest, ec) || ec) {
    return package_dir.filename().string();
  }
  std::string content;
  try {
    content = support::read_text_file(manifest.string());
  } catch (...) {
    return package_dir.filename().string();
  }
  bool in_package = false;
  std::size_t offset = 0;
  while (offset <= content.size()) {
    std::size_t nl = content.find('\n', offset);
    if (nl == std::string::npos) {
      nl = content.size();
    }
    std::string line = trim_copy(content.substr(offset, nl - offset));
    offset = nl + 1;
    if (line.empty() || line[0] == '#') {
      if (nl >= content.size()) {
        break;
      }
      continue;
    }
    if (line == "[package]") {
      in_package = true;
      if (nl >= content.size()) {
        break;
      }
      continue;
    }
    if (!line.empty() && line.front() == '[' && line.back() == ']') {
      in_package = false;
      if (nl >= content.size()) {
        break;
      }
      continue;
    }
    if (in_package && starts_with(line, "name")) {
      const std::size_t eq = line.find('=');
      if (eq != std::string::npos) {
        std::string value = trim_copy(line.substr(eq + 1));
        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) {
          value = value.substr(1, value.size() - 2);
        }
        if (!value.empty()) {
          return value;
        }
      }
    }
    if (nl >= content.size()) {
      break;
    }
  }
  return package_dir.filename().string();
}

static bool copy_package_to_cache(const std::string& package_name, const std::filesystem::path& source,
                                  std::string& error) {
  std::error_code ec;
  if (!std::filesystem::exists(source, ec) || ec || !std::filesystem::is_directory(source, ec)) {
    error = "package source directory does not exist: " + source.string();
    return false;
  }
  const std::filesystem::path cache_root = home_dir() / ".thagore" / "packages";
  const std::filesystem::path dest = cache_root / package_name;
  std::filesystem::create_directories(cache_root, ec);
  if (ec) {
    error = "cannot create package cache directory: " + cache_root.string();
    return false;
  }
  std::filesystem::remove_all(dest, ec);
  ec.clear();
  std::filesystem::copy(source, dest,
                        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    error = "cannot copy package to cache: " + ec.message();
    return false;
  }
  return true;
}

static bool print_install_diags(const support::DiagnosticSink& diag) {
  if (!diag.has_errors()) {
    return true;
  }
  for (const auto& d : diag.diagnostics()) {
    if (d.file.empty()) {
      std::cerr << d.code << ": " << d.message << "\n";
      continue;
    }
    const int line = d.line > 0 ? d.line : 1;
    const int col = d.column > 0 ? d.column : 1;
    std::cerr << d.file << ":" << line << ":" << col << ": " << d.code << ": " << d.message << "\n";
  }
  return false;
}

static bool install_from_dependency(const std::string& name, const std::string& value, const std::string& project_root) {
  const std::filesystem::path source = resolve_dependency_source_path(value, project_root);
  if (source.empty()) {
    std::cerr << "ERROR: dependency '" << name
              << "' does not point to a local package path (v0.4 supports local directory packages only)\n";
    return false;
  }
  std::string error;
  if (!copy_package_to_cache(name, source, error)) {
    std::cerr << "ERROR: " << error << "\n";
    return false;
  }
  std::cout << "installed package '" << name << "' to " << (home_dir() / ".thagore" / "packages" / name).string() << "\n";
  return true;
}

}  // namespace

int handle_install(const ParsedCommand& cmd) {
  if (!cmd.args.empty() && cmd.args[0] == "toolchain") {
    const std::string home = compiler_home_dir();
    const std::string toolchain_root = home + "/toolchains/stable";
    const std::string target_root = home + "/targets";
    const std::string host_triple = detect_host_triple();
    std::filesystem::create_directories(toolchain_root + "/bin");
    std::filesystem::create_directories(target_root + "/" + host_triple);

    const std::string cc = resolve_path_tool("clang");
    const std::string cxx = resolve_path_tool("clang++");
    const std::string linker = resolve_path_tool("clang");
    const std::string manifest =
        "{\n"
        "  \"triple\": \"" +
        host_triple +
        "\",\n"
        "  \"cc\": \"" +
        cc +
        "\",\n"
        "  \"cxx\": \"" +
        cxx +
        "\",\n"
        "  \"linker\": \"" +
        linker +
        "\",\n"
        "  \"sysroot\": \"\"\n"
        "}\n";
    support::write_text_file(target_root + "/" + host_triple + "/manifest.json", manifest);
    support::write_text_file(home + "/toolchain-installed.txt",
                             "toolchain=stable\n"
                             "target=" +
                                 host_triple +
                                 "\n");
    std::cout << "install toolchain: ready at " << toolchain_root << "\n";
    return 0;
  }

  ModuleResolver resolver;
  ProjectManifest manifest;
  support::DiagnosticSink diag;
  const std::string cwd = std::filesystem::current_path().string();
  if (!resolver.load_project_manifest(cwd, manifest, diag)) {
    print_install_diags(diag);
    return 1;
  }

  if (cmd.args.empty()) {
    if (!manifest.found) {
      std::cerr << "ERROR: thagore.toml not found; cannot install dependencies\n";
      return 1;
    }
    std::vector<std::string> deps;
    deps.reserve(manifest.dependencies.size());
    for (const auto& [name, _] : manifest.dependencies) {
      deps.push_back(name);
    }
    std::sort(deps.begin(), deps.end());
    for (const std::string& name : deps) {
      auto it = manifest.dependencies.find(name);
      if (it == manifest.dependencies.end()) {
        continue;
      }
      if (!install_from_dependency(name, it->second, manifest.root_path)) {
        return 1;
      }
    }
    if (!resolver.write_lock_file(manifest, diag)) {
      print_install_diags(diag);
      return 1;
    }
    return 0;
  }

  const std::string package_arg = cmd.args[0];
  std::error_code ec;
  const std::filesystem::path arg_path(package_arg);
  if (std::filesystem::exists(arg_path, ec) && !ec && std::filesystem::is_directory(arg_path, ec)) {
    const std::filesystem::path source = std::filesystem::weakly_canonical(arg_path);
    const std::string package_name = read_package_name(source);
    std::string error;
    if (!copy_package_to_cache(package_name, source, error)) {
      std::cerr << "ERROR: " << error << "\n";
      return 1;
    }
    std::cout << "installed package '" << package_name << "' to "
              << (home_dir() / ".thagore" / "packages" / package_name).string() << "\n";
    return 0;
  }

  if (!manifest.found) {
    std::cerr << "ERROR: thagore.toml not found; cannot resolve package '" << package_arg << "'\n";
    return 1;
  }
  auto dep = manifest.dependencies.find(package_arg);
  if (dep == manifest.dependencies.end()) {
    std::cerr << "ERROR: package `" << package_arg << "` not found in dependencies, run: thagc install " << package_arg
              << "\n";
    return 1;
  }
  if (!install_from_dependency(package_arg, dep->second, manifest.root_path)) {
    return 1;
  }
  if (!resolver.write_lock_file(manifest, diag)) {
    print_install_diags(diag);
    return 1;
  }
  return 0;
}

}  // namespace thagc::driver
