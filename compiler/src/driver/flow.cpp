#include "thagc/driver/command_handlers.hpp"

#include <iostream>

#include "thagc/driver/common.hpp"
#include "thagc/driver/pipeline.hpp"

namespace thagc::driver {

int handle_flow(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag) {
  if (cmd.args.empty()) {
    std::cerr << "ERROR: flow requires subcommand\n";
    return 1;
  }
  const std::string sub = cmd.args[0];
  if (sub != "doctor" && sub != "explain" && sub != "simulate" && sub != "recover") {
    std::cerr << "ERROR: unknown flow subcommand: " << sub << "\n";
    return 1;
  }
  if (sub == "recover") {
    std::cout << "flow recover: no journal in this milestone\n";
    return 0;
  }
  if (cmd.args.size() < 2) {
    std::cerr << "ERROR: flow " << sub << " requires entry path\n";
    return 1;
  }

  BuildOptions options;
  options.input_path = cmd.args[1];
  options.output_path = cmd.output_path.empty() ? (options.input_path + ".flow.bin") : cmd.output_path;
  options.emit_llvm = false;
  if (!pipeline.build(options, diag)) {
    for (const auto& d : diag.diagnostics()) {
      std::cerr << d.code << ": " << d.message << "\n";
    }
    return 1;
  }
  std::cout << "flow " << sub << ": OK (" << options.input_path << ")\n";
  return 0;
}

}  // namespace thagc::driver
