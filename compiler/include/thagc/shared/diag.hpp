#pragma once

#include <string>
#include <vector>

namespace thagc::support {

struct Diagnostic {
  std::string code;
  std::string message;
  std::string file;
  int line = 0;
  int column = 0;
};

class DiagnosticSink {
 public:
  void error(std::string code, std::string message, std::string file = "", int line = 0, int column = 0);
  bool has_errors() const;
  const std::vector<Diagnostic>& diagnostics() const;

 private:
  std::vector<Diagnostic> diagnostics_;
};

}  // namespace thagc::support

