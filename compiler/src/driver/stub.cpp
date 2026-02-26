#include "thagc/driver/command_handlers.hpp"

#include <iostream>

namespace thagc::driver {

static const char* command_name(CommandKind kind) {
  switch (kind) {
    case CommandKind::Fix:
      return "fix";
    case CommandKind::Intent:
      return "intent";
    case CommandKind::State:
      return "state";
    case CommandKind::Install:
      return "install";
    case CommandKind::Target:
      return "target";
    case CommandKind::Update:
      return "update";
    case CommandKind::Flow:
      return "flow";
    default:
      return "unknown";
  }
}

int handle_not_implemented(CommandKind kind) {
  std::cerr << "ERROR: command '" << command_name(kind)
            << "' is reserved in full rewrite scope and not implemented in current milestone\n";
  return 2;
}

}  // namespace thagc::driver

