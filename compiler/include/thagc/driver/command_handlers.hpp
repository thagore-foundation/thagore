#pragma once

#include "thagc/driver/command_types.hpp"
#include "thagc/shared/diag.hpp"

namespace thagc::driver {

class CompilerPipeline;

int handle_help();
int handle_version();
int handle_build(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag);
int handle_run(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag);
int handle_test(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag);
int handle_fix(const ParsedCommand& cmd);
int handle_intent(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag);
int handle_state(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag);
int handle_install(const ParsedCommand& cmd);
int handle_target(const ParsedCommand& cmd);
int handle_update(const ParsedCommand& cmd);
int handle_flow(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag);
int handle_unknown();

}  // namespace thagc::driver
