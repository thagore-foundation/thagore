#include "thagc/driver/command_parse.hpp"

#include <string>

namespace thagc::driver {

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
  if (cmd == "flow") return CommandKind::Flow;
  return CommandKind::Unknown;
}

ParsedCommand parse_command(int argc, char** argv) {
  ParsedCommand out;
  if (argc <= 1) {
    out.kind = CommandKind::Help;
    return out;
  }
  out.kind = parse_kind(argv[1]);
  for (int i = 2; i < argc; ++i) {
    out.args.push_back(argv[i]);
  }
  if (out.kind == CommandKind::Build || out.kind == CommandKind::Run || out.kind == CommandKind::Test) {
    if (argc > 2) {
      out.input_path = argv[2];
    }
    for (int i = 3; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "-o" && i + 1 < argc) {
        out.output_path = argv[++i];
        continue;
      }
      if (arg == "--emit-llvm") {
        out.emit_llvm = true;
        continue;
      }
    }
  }
  return out;
}

}  // namespace thagc::driver
