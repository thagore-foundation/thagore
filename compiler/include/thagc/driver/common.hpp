#pragma once

#include <cstddef>
#include <string>

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
bool apply_target_config(BuildOptions& options, const std::string& triple, support::DiagnosticSink& diag);
bool load_target_config(const std::string& triple, TargetConfig& out);

}  // namespace thagc::driver
