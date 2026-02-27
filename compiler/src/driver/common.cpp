#include <filesystem>
#include <regex>
#include <sstream>
#include <cstdlib>
#include <string>
#include <vector>

#include "thagc/shared/filesystem.hpp"
#include "thagc/driver/common.hpp"

namespace thagc::driver {

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

static std::string extract_json_string(const std::string& json, const std::string& key) {
  std::smatch m;
  const std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
  if (std::regex_search(json, m, re) && m.size() >= 2) {
    return m[1].str();
  }
  return "";
}

bool load_target_config(const std::string& triple, TargetConfig& out) {
  const std::string manifest = compiler_home_dir() + "/targets/" + triple + "/manifest.json";
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
  auto perm = st.permissions();
  return (perm & std::filesystem::perms::owner_exec) != std::filesystem::perms::none ||
         (perm & std::filesystem::perms::group_exec) != std::filesystem::perms::none ||
         (perm & std::filesystem::perms::others_exec) != std::filesystem::perms::none;
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
  const std::string dir = compiler_home_dir() + "/targets/" + triple;
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
  support::write_text_file(manifest, target_manifest_json(triple, cc, cxx, linker, ""));
  return true;
}

bool apply_target_config(BuildOptions& options, const std::string& triple, support::DiagnosticSink& diag) {
  if (triple.empty()) {
    return true;
  }
  TargetConfig cfg;
  if (!load_target_config(triple, cfg) && !ensure_target_config(triple)) {
    diag.error("E_TARGET_001", "target '" + triple + "' cannot be auto-initialized");
    return false;
  }
  if (!load_target_config(triple, cfg)) {
    diag.error("E_TARGET_001", "target '" + triple + "' is not installed and auto-init failed");
    return false;
  }
  options.target_triple = cfg.triple;
  options.target_linker = cfg.linker.empty() ? "clang" : cfg.linker;
  options.target_sysroot = cfg.sysroot;
  return true;
}

}  // namespace thagc::driver
