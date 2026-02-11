#pragma once

#include "thagore/common/diagnostics.hpp"
#include "thagore/common/result.hpp"
#include "thagore/frontend/ast.hpp"

#include <memory>
#include <string>

namespace llvm {
class LLVMContext;
class Module;
class TargetMachine;
} // namespace llvm

namespace thagore {

struct TargetConfig {
  std::string triple {};
  std::string cpu {"generic"};
  std::string features {};
  int optLevel {2};
};

class IRGenerator {
public:
  explicit IRGenerator(llvm::LLVMContext &context);
  auto lower(const TypedModule &typed, const std::string &moduleName) -> Result<std::unique_ptr<llvm::Module>, Diagnostic>;

private:
  llvm::LLVMContext &context;
};

class BackendPipeline {
public:
  static auto optimizeModule(llvm::Module &module, int optLevel) -> Result<void, Diagnostic>;
  static auto emitIR(const llvm::Module &module, const std::string &outPath) -> Result<void, Diagnostic>;
  static auto emitObject(llvm::Module &module, llvm::TargetMachine &targetMachine, const std::string &outPath)
    -> Result<void, Diagnostic>;
};

} // namespace thagore
