#include "thagc/frontend/typechecker.hpp"

namespace thagc::semantics {

static bool is_supported_type(const std::string& type_name) {
  return type_name == "i32" || type_name == "string" || type_name == "void";
}

bool TypeChecker::check(const syntax::AstProgram& program, support::DiagnosticSink& diag) const {
  if (program.source.empty()) {
    diag.error("E0001", "source is empty");
    return false;
  }
  if (!program.parse_errors.empty()) {
    for (const std::string& message : program.parse_errors) {
      diag.error("E0010", "syntax error: " + message);
    }
    return false;
  }
  if (!program.has_main) {
    diag.error("E0002", "missing entry function: func main() -> i32");
    return false;
  }
  for (const auto& fn : program.functions) {
    if (fn.name.empty()) {
      diag.error("E0003", "invalid function header at line " + std::to_string(fn.header_line));
      return false;
    }
    if (fn.return_type.empty()) {
      diag.error("E0004", "missing return type for function '" + fn.name + "'");
      return false;
    }
    if (!is_supported_type(fn.return_type)) {
      diag.error("E0006", "unsupported return type '" + fn.return_type + "' in function '" + fn.name + "'");
      return false;
    }
  }
  for (const auto& fn : program.functions) {
    if (fn.name == "main" && fn.return_type != "i32") {
      diag.error("E0005", "main must return i32");
      return false;
    }
  }
  return true;
}

}  // namespace thagc::semantics
