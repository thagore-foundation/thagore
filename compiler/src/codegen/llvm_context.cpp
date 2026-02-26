#include "thagc/codegen/llvm_emitter.hpp"

#include <memory>
#include <optional>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/TargetSelect.h>

namespace thagc::codegen {

static std::unique_ptr<llvm::Module> build_module(llvm::LLVMContext& context, const std::string& module_name) {
  auto module = std::make_unique<llvm::Module>(module_name, context);
  llvm::IRBuilder<> builder(context);

  auto* fn_type = llvm::FunctionType::get(builder.getInt32Ty(), false);
  auto* fn = llvm::Function::Create(fn_type, llvm::GlobalValue::ExternalLinkage, "main", module.get());
  auto* entry = llvm::BasicBlock::Create(context, "entry", fn);
  builder.SetInsertPoint(entry);
  builder.CreateRet(builder.getInt32(0));

  return module;
}

bool LlvmEmitter::emit_llvm_ir(const lowering::CoreProgram& core, const std::string& module_name,
                               const std::string& llvm_ir_path, support::DiagnosticSink& diag) const {
  llvm::LLVMContext context;
  auto module = build_module(context, module_name);
  if (llvm::verifyModule(*module, &llvm::errs())) {
    diag.error("E2001", "LLVM module verification failed");
    return false;
  }
  std::error_code ec;
  llvm::raw_fd_ostream out(llvm_ir_path, ec, llvm::sys::fs::OF_Text);
  if (ec) {
    diag.error("E2002", "cannot open LLVM IR output file: " + llvm_ir_path);
    return false;
  }
  module->print(out, nullptr);
  (void)core;
  return true;
}

bool LlvmEmitter::emit_object(const lowering::CoreProgram& core, const std::string& module_name,
                              const std::string& object_path, support::DiagnosticSink& diag) const {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  llvm::LLVMContext context;
  auto module = build_module(context, module_name);

  std::string error;
  const std::string target_triple = llvm::sys::getDefaultTargetTriple();
  module->setTargetTriple(target_triple);

  const llvm::Target* target = llvm::TargetRegistry::lookupTarget(target_triple, error);
  if (!target) {
    diag.error("E2003", "cannot resolve LLVM target: " + error);
    return false;
  }

  llvm::TargetOptions options;
  std::unique_ptr<llvm::TargetMachine> target_machine(
      target->createTargetMachine(target_triple, "generic", "", options, std::nullopt));
  if (!target_machine) {
    diag.error("E2004", "cannot create LLVM target machine");
    return false;
  }
  module->setDataLayout(target_machine->createDataLayout());

  std::error_code ec;
  llvm::raw_fd_ostream obj_file(object_path, ec, llvm::sys::fs::OF_None);
  if (ec) {
    diag.error("E2005", "cannot open object output file: " + object_path);
    return false;
  }

  llvm::legacy::PassManager pass;
  if (target_machine->addPassesToEmitFile(pass, obj_file, nullptr, llvm::CodeGenFileType::ObjectFile)) {
    diag.error("E2006", "target does not support object emission");
    return false;
  }
  pass.run(*module);
  obj_file.flush();
  (void)core;
  return true;
}

}  // namespace thagc::codegen
