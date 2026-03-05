#include "thagc/shared/diag.hpp"

namespace thagc::support {

void DiagnosticSink::error(std::string code, std::string message, std::string file, int line, int column, int end_line,
                           int end_column) {
  diagnostics_.push_back(
      Diagnostic{DiagnosticLevel::Error, std::move(code), std::move(message), std::move(file), line, column, end_line,
                 end_column});
}

void DiagnosticSink::warn(std::string code, std::string message, std::string file, int line, int column, int end_line,
                          int end_column) {
  diagnostics_.push_back(
      Diagnostic{DiagnosticLevel::Warning, std::move(code), std::move(message), std::move(file), line, column, end_line,
                 end_column});
}

bool DiagnosticSink::has_errors() const {
  for (const Diagnostic& d : diagnostics_) {
    if (d.level == DiagnosticLevel::Error) {
      return true;
    }
  }
  return false;
}

const std::vector<Diagnostic>& DiagnosticSink::diagnostics() const {
  return diagnostics_;
}

std::string diagnostic_fix_suggestion(const Diagnostic& diagnostic) {
  const std::string& code = diagnostic.code;
  if (code == "E0001") return "Provide a non-empty source file before compiling.";
  if (code == "E0002") return "Add top-level statements or define `func main():` as entrypoint.";
  if (code == "E0003") return "Fix the function header format: `func name(args):`.";
  if (code == "E0005" || code == "E0015") return "Make `main` and declared return annotations match the returned value type.";
  if (code == "E0006") return "Use a supported type annotation or add/import the missing type declaration.";
  if (code == "E0011") return "Fix the expression near the reported line and verify variable/function names.";
  if (code == "E0014") return "Ensure `if` and `while` conditions evaluate to `bool`.";
  if (code == "E0019") return "Place `else:` immediately after its `if` block at the same indentation.";
  if (code == "E0021" || code == "E0025" || code == "E0026") return "Declare the variable first and keep assignment types consistent.";
  if (code == "E0029") return "Choose one entry style: either `func main()` or top-level executable statements.";
  if (code == "E0036") return "Use a valid enum variant declared in the matched enum.";
  if (code == "E_SEND_SYNC_004") return "Replace `Rc<T>` with `Arc<T>` for values crossing task/thread boundaries.";
  if (code == "E_TYPESTATE_001" || code == "E_TYPESTATE_002") return "Call `open(x)` before `read(x)`/`write(x)` and close only opened resources.";
  if (code == "E_STATE_UNKNOWN_SET") return "Declare the state set first, for example: `state Session: Init | Ready | Closed`.";
  if (code == "E_STATE_UNKNOWN_VARIANT") return "Use one of the declared variants in the corresponding `state` declaration.";
  if (code == "E_STATE_MISMATCH_ARG") return "Pass a variable currently in the state required by the function parameter.";
  if (code == "E_STATE_MISMATCH_RETURN") return "Return a value in the state declared by the function return annotation.";
  if (code == "E_STATE_INVALID_TRANSITION") return "Update transition order so state flows through allowed variants only.";
  if (code == "E_STATE_AMBIGUOUS" || code == "W_STATE_AMBIGUOUS")
    return "Normalize control-flow so the variable has one deterministic state before use.";
  if (code == "E_TYPE_INTENT_GOAL")
    return "Use a supported `goal:` value (for example `reduce_sum`, `auto_plan`, or `off`).";
  if (code == "E_TYPE_INTENT_STRATEGY")
    return "Use dotted strategy ids like `family.plan.v1`, or remove strategy when `goal: off`.";
  if (code == "E_INTENT_001" || code == "E_INTENT_002" || code == "E_INTENT_003" || code == "E_INTENT_004" ||
      code == "E_INTENT_005")
    return "Run `thagc intent explain <file.tg>` to inspect parsed intent blocks, then fix goal/strategy syntax.";
  if (code == "E_MOD_110" || code == "E_MOD_111")
    return "Rename the package using only letters, digits, `_`, or `-` (e.g. `zalo-tg` or `zalo`). Dots are reserved as module path separators.";
  if (code.rfind("E_MOD_", 0) == 0) return "Verify import paths/aliases and exported symbols in dependent modules.";
  return "Check the reported line/column, then rerun `thagc check <file.tg>` for a focused diagnostic pass.";
}

}  // namespace thagc::support
