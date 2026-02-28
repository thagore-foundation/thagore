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
  std::string target_triple;
  std::vector<std::string> link_libs;
  std::vector<std::string> link_dirs;
  std::vector<std::string> link_args;
  bool emit_llvm = false;
  std::string autofix_mode;
  std::string autofix_lock_path;
  std::string autofix_exclude;
  int autofix_max_iterations = 0;
  int autofix_max_files = 0;
  bool autofix_lock_strict = false;
  bool autofix_locked = false;
  bool autofix_workspace = false;
  bool workspace = false;
  bool no_run = false;
  bool fail_fast = false;
  bool json_output = false;
  bool list_only = false;
  std::string package_name;
  std::string exclude_pattern;
  std::string exclude_package;
  std::string message_format;
};

}  // namespace thagc::driver
