#include "thagc/driver/command_handlers.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "thagc/driver/common.hpp"
#include "thagc/shared/filesystem.hpp"

namespace thagc::driver {

static std::string resolve_path_tool(const std::string& tool) {
  const char* path_env = std::getenv("PATH");
  if (path_env == nullptr) {
    return "";
  }
  std::string path = path_env;
  std::size_t from = 0;
  while (from < path.size()) {
    const std::size_t colon = path.find(':', from);
    const std::string part = colon == std::string::npos ? path.substr(from) : path.substr(from, colon - from);
    std::filesystem::path candidate = std::filesystem::path(part) / tool;
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec)) {
      return candidate.string();
    }
    if (colon == std::string::npos) {
      break;
    }
    from = colon + 1;
  }
  return "";
}

int handle_install(const ParsedCommand& cmd) {
  if (cmd.args.empty() || cmd.args[0] != "toolchain") {
    std::cerr << "ERROR: install supports: install toolchain [--yes]\n";
    return 1;
  }
  const std::string home = compiler_home_dir();
  const std::string toolchain_root = home + "/toolchains/stable";
  const std::string target_root = home + "/targets";
  std::filesystem::create_directories(toolchain_root + "/bin");
  std::filesystem::create_directories(target_root + "/x86_64-unknown-linux-gnu");

  const std::string cc = resolve_path_tool("clang");
  const std::string cxx = resolve_path_tool("clang++");
  const std::string linker = resolve_path_tool("clang");
  const std::string manifest =
      "{\n"
      "  \"triple\": \"x86_64-unknown-linux-gnu\",\n"
      "  \"cc\": \"" +
      cc +
      "\",\n"
      "  \"cxx\": \"" +
      cxx +
      "\",\n"
      "  \"linker\": \"" +
      linker +
      "\",\n"
      "  \"sysroot\": \"\"\n"
      "}\n";
  support::write_text_file(target_root + "/x86_64-unknown-linux-gnu/manifest.json", manifest);
  support::write_text_file(home + "/toolchain-installed.txt",
                           "toolchain=stable\n"
                           "target=x86_64-unknown-linux-gnu\n");
  std::cout << "install toolchain: ready at " << toolchain_root << "\n";
  return 0;
}

}  // namespace thagc::driver
