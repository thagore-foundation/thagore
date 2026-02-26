#include "thagc/support/diag.hpp"

namespace thagc::support {

void DiagnosticSink::error(std::string code, std::string message, std::string file, int line, int column) {
  diagnostics_.push_back(Diagnostic{std::move(code), std::move(message), std::move(file), line, column});
}

bool DiagnosticSink::has_errors() const {
  return !diagnostics_.empty();
}

const std::vector<Diagnostic>& DiagnosticSink::diagnostics() const {
  return diagnostics_;
}

}  // namespace thagc::support

