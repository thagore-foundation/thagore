#include <filesystem>
#include <regex>
#include <sstream>
#include <cstdlib>
#include <string>
#include <vector>

#include "thagc/shared/filesystem.hpp"
#include "thagc/driver/common.hpp"

namespace thagc::driver {

std::string canonicalize_target_triple(const std::string& triple) {
  if (triple.empty()) {
    return "";
  }
  if (triple == "wasm" || triple == "wasm32") {
    return "wasm32-unknown-unknown";
  }
  if (triple == "wasm64") {
    return "wasm64-unknown-unknown";
  }
  return triple;
}

bool is_wasm_target(const std::string& triple) {
  const std::string normalized = canonicalize_target_triple(triple);
  return normalized.find("wasm32") != std::string::npos || normalized.find("wasm64") != std::string::npos;
}

std::string arg_or_empty(const ParsedCommand& cmd, std::size_t index) {
  if (index >= cmd.args.size()) {
    return "";
  }
  return cmd.args[index];
}

std::string choose_output_path(const ParsedCommand& cmd, const std::string& fallback_suffix) {
  if (!cmd.output_path.empty()) {
    return cmd.output_path;
  }
  if (cmd.input_path.empty()) {
    return "";
  }
  return cmd.input_path + fallback_suffix;
}

std::string compiler_home_dir() {
  std::filesystem::path root = std::filesystem::current_path() / ".thagc";
  std::filesystem::create_directories(root);
  return root.string();
}

std::string toolchain_home_dir() {
  const char* explicit_home = std::getenv("THAGORE_HOME");
  if (explicit_home != nullptr && *explicit_home != '\0') {
    std::filesystem::path root(explicit_home);
    std::filesystem::create_directories(root);
    return root.string();
  }
#if defined(_WIN32)
  const char* user_profile = std::getenv("USERPROFILE");
  if (user_profile != nullptr && *user_profile != '\0') {
    std::filesystem::path root = std::filesystem::path(user_profile) / ".thagore";
    std::filesystem::create_directories(root);
    return root.string();
  }
#else
  const char* home = std::getenv("HOME");
  if (home != nullptr && *home != '\0') {
    std::filesystem::path root = std::filesystem::path(home) / ".thagore";
    std::filesystem::create_directories(root);
    return root.string();
  }
#endif
  std::filesystem::path fallback = std::filesystem::current_path() / ".thagore";
  std::filesystem::create_directories(fallback);
  return fallback.string();
}

bool has_installed_toolchain(const std::string& home) {
  std::filesystem::path toolchain_bin =
      std::filesystem::path(home) / "toolchains" / "stable" / "bin"
#if defined(_WIN32)
      / "thagc.exe";
#else
      / "thagc";
#endif
  std::error_code ec;
  return std::filesystem::exists(toolchain_bin, ec) && !ec;
}

std::string resolve_update_state_home(bool* has_installed) {
  const std::string global_home = toolchain_home_dir();
  const bool installed = has_installed_toolchain(global_home);
  if (has_installed != nullptr) {
    *has_installed = installed;
  }
  if (installed) {
    return global_home;
  }
  return compiler_home_dir();
}

std::vector<std::string> compose_link_extra_args(const ParsedCommand& cmd) {
  std::vector<std::string> args;
  args.reserve(cmd.link_dirs.size() + cmd.link_libs.size() + cmd.link_args.size());
  for (const std::string& dir : cmd.link_dirs) {
    if (!dir.empty()) {
      args.push_back("-L" + dir);
    }
  }
  for (const std::string& lib : cmd.link_libs) {
    if (lib.empty()) {
      continue;
    }
    if (lib.rfind("-l", 0) == 0) {
      args.push_back(lib);
    } else {
      args.push_back("-l" + lib);
    }
  }
  for (const std::string& arg : cmd.link_args) {
    if (!arg.empty()) {
      args.push_back(arg);
    }
  }
  return args;
}

static std::string extract_json_string(const std::string& json, const std::string& key) {
  std::smatch m;
  const std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
  if (std::regex_search(json, m, re) && m.size() >= 2) {
    return m[1].str();
  }
  return "";
}

bool load_target_config(const std::string& triple, TargetConfig& out) {
  const std::string normalized = canonicalize_target_triple(triple);
  const std::string manifest = compiler_home_dir() + "/targets/" + normalized + "/manifest.json";
  if (!support::file_exists(manifest)) {
    return false;
  }
  const std::string json = support::read_text_file(manifest);
  out.triple = extract_json_string(json, "triple");
  out.cc = extract_json_string(json, "cc");
  out.cxx = extract_json_string(json, "cxx");
  out.linker = extract_json_string(json, "linker");
  out.sysroot = extract_json_string(json, "sysroot");
  return !out.triple.empty();
}

static bool file_executable(const std::string& path) {
  std::error_code ec;
  auto st = std::filesystem::status(path, ec);
  if (ec || !std::filesystem::exists(st)) {
    return false;
  }
#if defined(_WIN32)
  // On Windows, POSIX exec bits are not meaningful.
  // Check that it is a regular file with a known executable extension.
  if (!std::filesystem::is_regular_file(st)) {
    return false;
  }
  const std::string ext = std::filesystem::path(path).extension().string();
  return ext == ".exe" || ext == ".cmd" || ext == ".bat" || ext == ".com" || ext.empty();
#else
  auto perm = st.permissions();
  return (perm & std::filesystem::perms::owner_exec) != std::filesystem::perms::none ||
         (perm & std::filesystem::perms::group_exec) != std::filesystem::perms::none ||
         (perm & std::filesystem::perms::others_exec) != std::filesystem::perms::none;
#endif
}

static std::string resolve_from_path(const std::string& tool) {
  if (tool.empty()) {
    return "";
  }
  std::filesystem::path p(tool);
  if (p.has_parent_path()) {
    return file_executable(tool) ? std::filesystem::absolute(tool).string() : "";
  }
  const char* path_env = std::getenv("PATH");
  if (path_env == nullptr) {
    return "";
  }
#if defined(_WIN32)
  constexpr char path_separator = ';';
#else
  constexpr char path_separator = ':';
#endif
  std::stringstream ss(path_env);
  std::string segment;
  while (std::getline(ss, segment, path_separator)) {
    std::filesystem::path candidate = std::filesystem::path(segment) / tool;
    if (file_executable(candidate.string())) {
      return candidate.string();
    }
#if defined(_WIN32)
    // On Windows, also try with .exe extension appended.
    if (std::filesystem::path(tool).extension().empty()) {
      std::filesystem::path exe_candidate = std::filesystem::path(segment) / (tool + ".exe");
      if (file_executable(exe_candidate.string())) {
        return exe_candidate.string();
      }
    }
#endif
  }
  return "";
}

static std::string target_manifest_json(const std::string& triple, const std::string& cc, const std::string& cxx,
                                        const std::string& linker, const std::string& sysroot) {
  return "{\n"
         "  \"triple\": \"" +
         triple + "\",\n"
                  "  \"cc\": \"" +
         cc + "\",\n"
              "  \"cxx\": \"" +
         cxx + "\",\n"
               "  \"linker\": \"" +
         linker + "\",\n"
                  "  \"sysroot\": \"" +
         sysroot + "\"\n"
                   "}\n";
}

static bool ensure_target_config(const std::string& triple) {
  const std::string normalized = canonicalize_target_triple(triple);
  const std::string dir = compiler_home_dir() + "/targets/" + normalized;
  std::filesystem::create_directories(dir);
  const std::string manifest = dir + "/manifest.json";
  if (support::file_exists(manifest)) {
    return true;
  }
  std::string cc = resolve_from_path("clang");
  std::string cxx = resolve_from_path("clang++");
  if (cc.empty()) {
    cc = resolve_from_path("gcc");
  }
  if (cxx.empty()) {
    cxx = resolve_from_path("g++");
  }
  std::string linker = !cc.empty() ? cc : resolve_from_path("clang");
  if (linker.empty()) {
    linker = resolve_from_path("gcc");
  }
  support::write_text_file(manifest, target_manifest_json(normalized, cc, cxx, linker, ""));
  return true;
}

bool apply_target_config(BuildOptions& options, const std::string& triple, support::DiagnosticSink& diag) {
  if (triple.empty()) {
    return true;
  }
  const std::string normalized = canonicalize_target_triple(triple);
  TargetConfig cfg;
  if (!load_target_config(normalized, cfg) && !ensure_target_config(normalized)) {
    diag.error("E_TARGET_001", "target '" + normalized + "' cannot be auto-initialized");
    return false;
  }
  if (!load_target_config(normalized, cfg)) {
    diag.error("E_TARGET_001", "target '" + normalized + "' is not installed and auto-init failed");
    return false;
  }
  options.target_triple = cfg.triple;
  options.target_linker = cfg.linker.empty() ? "clang" : cfg.linker;
  options.target_sysroot = cfg.sysroot;
  return true;
}

}  // namespace thagc::driver
