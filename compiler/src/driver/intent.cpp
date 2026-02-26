#include "thagc/driver/command_handlers.hpp"

#include <iostream>

#include "thagc/driver/common.hpp"
#include "thagc/driver/pipeline.hpp"

namespace thagc::driver {

int handle_intent(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag) {
  if (cmd.args.size() < 2) {
    std::cerr << "ERROR: intent requires subcommand and entry path\n";
    return 1;
  }
  const std::string sub = cmd.args[0];
  const std::string entry = cmd.args[1];
  if (sub != "doctor" && sub != "explain" && sub != "lock") {
    std::cerr << "ERROR: unknown intent subcommand: " << sub << "\n";
    return 1;
  }

  BuildOptions options;
  options.input_path = entry;
  options.output_path = cmd.output_path.empty() ? (entry + ".intent.bin") : cmd.output_path;
  options.emit_llvm = false;
  if (!pipeline.build(options, diag)) {
    for (const auto& d : diag.diagnostics()) {
      std::cerr << d.code << ": " << d.message << "\n";
    }
    return 1;
  }
  std::cout << "intent " << sub << ": OK (" << entry << ")\n";
  return 0;
}

}  // namespace thagc::driver
