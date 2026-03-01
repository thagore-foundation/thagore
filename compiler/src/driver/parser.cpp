#include "thagc/driver/command_parse.hpp"

#include <filesystem>
#include <string_view>
#include <string>

namespace thagc::driver {

static bool starts_with(const std::string& value, std::string_view prefix) {
  return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

static bool looks_like_source_path(const std::string& value) {
  if (value.size() >= 3 && value.compare(value.size() - 3, 3, ".tg") == 0) {
    return true;
  }
  std::error_code ec;
  return std::filesystem::exists(value, ec);
}

static int parse_positive_int(const std::string& value) {
  if (value.empty()) {
    return 0;
  }
  for (char ch : value) {
    if (ch < '0' || ch > '9') {
      return 0;
    }
  }
  return std::stoi(value);
}

static CommandKind parse_kind(const std::string& cmd) {
  if (cmd == "--help" || cmd == "help") return CommandKind::Help;
  if (cmd == "--version" || cmd == "version") return CommandKind::Version;
  if (cmd == "build") return CommandKind::Build;
  if (cmd == "run") return CommandKind::Run;
  if (cmd == "test") return CommandKind::Test;
  if (cmd == "fix") return CommandKind::Fix;
  if (cmd == "intent") return CommandKind::Intent;
  if (cmd == "state") return CommandKind::State;
  if (cmd == "install") return CommandKind::Install;
  if (cmd == "target") return CommandKind::Target;
  if (cmd == "update") return CommandKind::Update;
  if (cmd == "migrate") return CommandKind::Migrate;
  if (cmd == "flow") return CommandKind::Flow;
  return CommandKind::Unknown;
}

static void parse_build_like_options(ParsedCommand& out, int argc, char** argv, int start_index) {
  for (int i = start_index; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-o" && i + 1 < argc) {
      out.output_path = argv[++i];
      continue;
    }
    if ((arg == "--target" || arg == "-t") && i + 1 < argc) {
      out.target_triple = argv[++i];
      continue;
    }
    if (starts_with(arg, "--target=")) {
      out.target_triple = arg.substr(std::string("--target=").size());
      continue;
    }
    if (arg == "--link-lib" && i + 1 < argc) {
      out.link_libs.push_back(argv[++i]);
      continue;
    }
    if (starts_with(arg, "--link-lib=")) {
      out.link_libs.push_back(arg.substr(std::string("--link-lib=").size()));
      continue;
    }
    if (arg == "--link-dir" && i + 1 < argc) {
      out.link_dirs.push_back(argv[++i]);
      continue;
    }
    if (starts_with(arg, "--link-dir=")) {
      out.link_dirs.push_back(arg.substr(std::string("--link-dir=").size()));
      continue;
    }
    if (arg == "--link-arg" && i + 1 < argc) {
      out.link_args.push_back(argv[++i]);
      continue;
    }
    if (starts_with(arg, "--link-arg=")) {
      out.link_args.push_back(arg.substr(std::string("--link-arg=").size()));
      continue;
    }
    if (arg == "-l" && i + 1 < argc) {
      out.link_libs.push_back(argv[++i]);
      continue;
    }
    if (arg.size() > 2 && starts_with(arg, "-l")) {
      out.link_libs.push_back(arg.substr(2));
      continue;
    }
    if (arg == "-L" && i + 1 < argc) {
      out.link_dirs.push_back(argv[++i]);
      continue;
    }
    if (arg.size() > 2 && starts_with(arg, "-L")) {
      out.link_dirs.push_back(arg.substr(2));
      continue;
    }
    if (starts_with(arg, "-Wl,")) {
      out.link_args.push_back(arg);
      continue;
    }
    if (arg == "--emit-llvm") {
      out.emit_llvm = true;
      continue;
    }
    if (arg == "--autofix-lock-strict") {
      out.autofix_lock_strict = true;
      continue;
    }
    if (arg == "--autofix-locked") {
      out.autofix_locked = true;
      continue;
    }
    if (arg == "--autofix-workspace" || arg == "--workspace" || arg == "--all") {
      out.autofix_workspace = true;
      out.workspace = true;
      continue;
    }
    if (arg == "--no-run") {
      out.no_run = true;
      continue;
    }
    if (arg == "--fail-fast") {
      out.fail_fast = true;
      continue;
    }
    if (arg == "--json") {
      out.json_output = true;
      continue;
    }
    if (arg == "--list") {
      out.list_only = true;
      continue;
    }
    if ((arg == "--package" || arg == "-p") && i + 1 < argc) {
      out.package_name = argv[++i];
      continue;
    }
    if (starts_with(arg, "--package=")) {
      out.package_name = arg.substr(std::string("--package=").size());
      continue;
    }
    if (starts_with(arg, "--exclude=")) {
      out.exclude_pattern = arg.substr(std::string("--exclude=").size());
      continue;
    }
    if (starts_with(arg, "--exclude-package=")) {
      out.exclude_package = arg.substr(std::string("--exclude-package=").size());
      continue;
    }
    if (starts_with(arg, "--message-format=")) {
      out.message_format = arg.substr(std::string("--message-format=").size());
      continue;
    }
    if (starts_with(arg, "--autofix=")) {
      out.autofix_mode = arg.substr(std::string("--autofix=").size());
      continue;
    }
    if (starts_with(arg, "--autofix-lock=")) {
      out.autofix_lock_path = arg.substr(std::string("--autofix-lock=").size());
      continue;
    }
    if (starts_with(arg, "--autofix-exclude=")) {
      out.autofix_exclude = arg.substr(std::string("--autofix-exclude=").size());
      continue;
    }
    if (starts_with(arg, "--autofix-max-iterations=")) {
      out.autofix_max_iterations =
          parse_positive_int(arg.substr(std::string("--autofix-max-iterations=").size()));
      continue;
    }
    if (starts_with(arg, "--autofix-max-files=")) {
      out.autofix_max_files = parse_positive_int(arg.substr(std::string("--autofix-max-files=").size()));
      continue;
    }
  }
}

ParsedCommand parse_command(int argc, char** argv) {
  ParsedCommand out;
  if (argc <= 1) {
    out.kind = CommandKind::Help;
    return out;
  }

  const std::string first = argv[1];
  out.kind = parse_kind(first);
  if (out.kind == CommandKind::Unknown && looks_like_source_path(first)) {
    out.kind = CommandKind::Build;
    out.input_path = first;
    parse_build_like_options(out, argc, argv, 2);
    return out;
  }

  for (int i = 2; i < argc; ++i) {
    out.args.push_back(argv[i]);
  }

  if (out.kind == CommandKind::Build || out.kind == CommandKind::Run || out.kind == CommandKind::Test) {
    if (argc > 2 && !starts_with(argv[2], "-") &&
        (out.kind != CommandKind::Test || looks_like_source_path(argv[2]))) {
      out.input_path = argv[2];
      parse_build_like_options(out, argc, argv, 3);
      return out;
    }
    if (out.kind == CommandKind::Test && argc > 2 && !starts_with(argv[2], "-")) {
      parse_build_like_options(out, argc, argv, 3);
      return out;
    }
    parse_build_like_options(out, argc, argv, 2);
  }
  return out;
}

}  // namespace thagc::driver
