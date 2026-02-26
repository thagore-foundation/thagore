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

struct LinkPlan {
  std::string object_path;
  std::string output_path;
  std::vector<std::string> extra_args;
};

struct LinkResult {
  bool success = false;
  std::string command;
  int exit_code = 0;
  std::string error;
};

}  // namespace thagc::domain
