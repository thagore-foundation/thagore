#pragma once

#include <string>
#include <vector>

namespace thagc::domain {

struct SourceUnit {
  std::string path;
  std::string content;
};

struct BuildRequest {
  std::string input_path;
  std::string source_text;
  std::string output_path;
  bool emit_llvm = false;
  std::string llvm_ir_path;
};

struct BuildResult {
  bool success = false;
  std::vector<std::string> artifacts;
};

}  // namespace thagc::domain
