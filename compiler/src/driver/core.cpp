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
    case driver::CommandKind::Check:
      return driver::handle_check(cmd, pipeline, diag);
    case driver::CommandKind::Fmt:
      return driver::handle_fmt(cmd);
    case driver::CommandKind::Fix:
      return driver::handle_fix(cmd);
    case driver::CommandKind::Repl:
      return driver::handle_repl(cmd, pipeline, diag);
    case driver::CommandKind::Lsp:
      return driver::handle_lsp(cmd);
    case driver::CommandKind::Migrate:
      return driver::handle_migrate(cmd);
    case driver::CommandKind::Unknown:
      return driver::handle_unknown();
  }
  return 2;
}

}  // namespace thagc::cli
