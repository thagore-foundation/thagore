#include <filesystem>
#include <regex>
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

bool apply_target_config(BuildOptions& options, const std::string& triple, support::DiagnosticSink& diag) {
  if (triple.empty()) {
    return true;
  }
  TargetConfig cfg;
  if (!load_target_config(triple, cfg)) {
    diag.error("E_TARGET_001", "target '" + triple + "' is not installed; run `thagore target add " + triple + "`");
    return false;
  }
  options.target_triple = cfg.triple;
  options.target_linker = cfg.linker;
  options.target_sysroot = cfg.sysroot;
  return true;
}

}  // namespace thagc::driver
