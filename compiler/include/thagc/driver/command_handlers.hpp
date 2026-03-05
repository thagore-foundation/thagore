#pragma once

#include "thagc/driver/command_types.hpp"
#include "thagc/shared/diag.hpp"

namespace thagc::driver {

class CompilerPipeline;

int handle_help();
int handle_version();
int handle_explain(const ParsedCommand& cmd);
int handle_build(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag);
int handle_run(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag);
int handle_check(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag);
int handle_fmt(const ParsedCommand& cmd);
int handle_fix(const ParsedCommand& cmd);
int handle_repl(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag);
int handle_lsp(const ParsedCommand& cmd);
int handle_target(const ParsedCommand& cmd, support::DiagnosticSink& diag);
int handle_intent(const ParsedCommand& cmd);
int handle_state(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag);
int handle_migrate(const ParsedCommand& cmd);
int handle_unknown();

}  // namespace thagc::driver
