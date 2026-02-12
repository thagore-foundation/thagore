#include "thagore/common/diagnostics.hpp"

#include <iostream>
#if __has_include(<print>)
#include <print>
#endif

namespace thagore {

auto formatDiagnostic(const Diagnostic &diag) -> std::string {
  return std::format(
    "{}:{}:{}: {}",
    diag.span.file.empty() ? "<unknown>" : diag.span.file,
    diag.span.begin.line,
    diag.span.begin.column,
    diag.message
  );
}

void printDiagnostic(const Diagnostic &diag) {
  const auto line = formatDiagnostic(diag);
#if defined(__cpp_lib_print) && __cpp_lib_print >= 202207L
  std::print(stderr, "{}\n", line);
#else
  std::cerr << line << '\n';
#endif
}

} // namespace thagore
