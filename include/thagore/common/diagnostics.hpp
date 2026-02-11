#pragma once

#include "thagore/common/source.hpp"

#include <format>
#include <string>
#include <string_view>

namespace thagore {

enum class ErrorCode {
  InvalidCli,
  IoError,
  LexError,
  ParseError,
  SemanticError,
  CodegenError,
};

struct Diagnostic {
  ErrorCode code {};
  std::string message {};
  SourceSpan span {};
};

auto formatDiagnostic(const Diagnostic &diag) -> std::string;
void printDiagnostic(const Diagnostic &diag);

} // namespace thagore
