#include "thagc/driver/command_router.hpp"

#include "thagc/driver/command_handlers.hpp"
#include "thagc/driver/command_parse.hpp"
#include "thagc/driver/pipeline.hpp"
#include "thagc/shared/diag.hpp"

namespace thagc::cli {

int CommandRouter::run(int argc, char** argv) const {
  const driver::ParsedCommand cmd = driver::parse_command(argc, argv);
  const driver::CompilerPipeline pipeline;
  support::DiagnosticSink diag;

  switch (cmd.kind) {
    case driver::CommandKind::Help:
      return driver::handle_help();
    case driver::CommandKind::Version:
      return driver::handle_version();
    case driver::CommandKind::Build:
      return driver::handle_build(cmd, pipeline, diag);
    case driver::CommandKind::Run:
      return driver::handle_run(cmd, pipeline, diag);
    case driver::CommandKind::Test:
      return driver::handle_test(cmd, pipeline, diag);
    case driver::CommandKind::Fix:
    case driver::CommandKind::Intent:
    case driver::CommandKind::State:
    case driver::CommandKind::Install:
    case driver::CommandKind::Target:
    case driver::CommandKind::Update:
    case driver::CommandKind::Flow:
      return driver::handle_not_implemented(cmd.kind);
    case driver::CommandKind::Unknown:
      return driver::handle_not_implemented(cmd.kind);
  }
  return 2;
}

}  // namespace thagc::cli
