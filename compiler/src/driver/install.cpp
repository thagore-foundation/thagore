#include "thagc/driver/command_handlers.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "thagc/driver/common.hpp"
#include "thagc/shared/filesystem.hpp"
#include "thagc/shared/process.hpp"

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

static std::string trim(std::string value) {
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ' || value.back() == '\t')) {
    value.pop_back();
  }
  std::size_t i = 0;
  while (i < value.size() && (value[i] == ' ' || value[i] == '\t')) {
    ++i;
  }
  return value.substr(i);
}

static std::string detect_host_triple() {
  int rc = 0;
  std::string triple = trim(support::run_process_capture({"llvm-config", "--host-target"}, &rc));
  if (rc == 0 && !triple.empty()) {
    return triple;
  }
  triple = trim(support::run_process_capture({"clang", "-dumpmachine"}, &rc));
  if (rc == 0 && !triple.empty()) {
    return triple;
  }
  return "x86_64-unknown-linux-gnu";
}

int handle_install(const ParsedCommand& cmd) {
  if (cmd.args.empty() || cmd.args[0] != "toolchain") {
    std::cerr << "ERROR: install supports: install toolchain [--yes]\n";
    return 1;
  }
  const std::string home = compiler_home_dir();
  const std::string toolchain_root = home + "/toolchains/stable";
  const std::string target_root = home + "/targets";
  const std::string host_triple = detect_host_triple();
  std::filesystem::create_directories(toolchain_root + "/bin");
  std::filesystem::create_directories(target_root + "/" + host_triple);

  const std::string cc = resolve_path_tool("clang");
  const std::string cxx = resolve_path_tool("clang++");
  const std::string linker = resolve_path_tool("clang");
  const std::string manifest =
      "{\n"
      "  \"triple\": \"" +
      host_triple +
      "\",\n"
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
  support::write_text_file(target_root + "/" + host_triple + "/manifest.json", manifest);
  support::write_text_file(home + "/toolchain-installed.txt",
                           "toolchain=stable\n"
                           "target=" +
                               host_triple +
                               "\n");
  std::cout << "install toolchain: ready at " << toolchain_root << "\n";
  return 0;
}

}  // namespace thagc::driver
