#pragma once

#include <string>
#include <vector>

namespace thagc::support {

enum class DiagnosticLevel {
  Error,
  Warning,
};

struct Diagnostic {
  DiagnosticLevel level = DiagnosticLevel::Error;
  std::string code;
  std::string message;
  std::string file;
  int line = 0;
  int column = 0;
};

class DiagnosticSink {
 public:
  void error(std::string code, std::string message, std::string file = "", int line = 0, int column = 0);
  void warn(std::string code, std::string message, std::string file = "", int line = 0, int column = 0);
  bool has_errors() const;
  const std::vector<Diagnostic>& diagnostics() const;

 private:
  std::vector<Diagnostic> diagnostics_;
};

std::string diagnostic_fix_suggestion(const Diagnostic& diagnostic);

}  // namespace thagc::support
