#pragma once

#include <string>
#include <vector>

namespace thagc::driver {

enum class CommandKind {
  Help,
  Version,
  Build,
  Run,
  Check,
  Fmt,
  Fix,
  Repl,
  Lsp,
  Migrate,
  Unknown,
};

struct ParsedCommand {
  CommandKind kind = CommandKind::Unknown;
  std::vector<std::string> args;
  std::string input_path;
  std::string output_path;
  std::string target_triple;
  std::vector<std::string> include_paths;
  std::vector<std::string> link_libs;
  std::vector<std::string> link_dirs;
  std::vector<std::string> link_args;
  bool emit_llvm = false;
};

}  // namespace thagc::driver
