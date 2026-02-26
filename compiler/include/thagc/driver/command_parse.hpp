#pragma once

#include "thagc/driver/command_types.hpp"

namespace thagc::driver {

ParsedCommand parse_command(int argc, char** argv);

}  // namespace thagc::driver

