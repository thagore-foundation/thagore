#pragma once

#include <string>
#include <vector>

namespace thagc::driver {

enum class CommandKind {
  Help,
  Version,
  Build,
  Run,
  Test,
  Fix,
  Intent,
  State,
  Install,
  Target,
  Update,
  Flow,
  Unknown,
};

struct ParsedCommand {
  CommandKind kind = CommandKind::Unknown;
  std::vector<std::string> args;
  std::string input_path;
  std::string output_path;
  bool emit_llvm = false;
};

}  // namespace thagc::driver

