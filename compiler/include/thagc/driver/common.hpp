#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "thagc/driver/command_types.hpp"
#include "thagc/driver/pipeline.hpp"
#include "thagc/shared/diag.hpp"

namespace thagc::driver {

struct TargetConfig {
  std::string triple;
  std::string cc;
  std::string cxx;
  std::string linker;
  std::string sysroot;
};

std::string arg_or_empty(const ParsedCommand& cmd, std::size_t index);
std::string choose_output_path(const ParsedCommand& cmd, const std::string& fallback_suffix);
std::string compiler_home_dir();
std::string toolchain_home_dir();
bool has_installed_toolchain(const std::string& home);
std::string resolve_update_state_home(bool* has_installed = nullptr);
std::vector<std::string> compose_link_extra_args(const ParsedCommand& cmd);
bool apply_target_config(BuildOptions& options, const std::string& triple, support::DiagnosticSink& diag);
bool load_target_config(const std::string& triple, TargetConfig& out);
std::string canonicalize_target_triple(const std::string& triple);
bool is_wasm_target(const std::string& triple);

}  // namespace thagc::driver
