#pragma once

#include <cstddef>
#include <string>

#include "thagc/driver/command_types.hpp"

namespace thagc::driver {

std::string arg_or_empty(const ParsedCommand& cmd, std::size_t index);
std::string choose_output_path(const ParsedCommand& cmd, const std::string& fallback_suffix);
std::string compiler_home_dir();

}  // namespace thagc::driver
