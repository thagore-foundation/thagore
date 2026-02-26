#include <filesystem>
#include <string>
#include <vector>

#include "thagc/driver/command_types.hpp"

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

}  // namespace thagc::driver

