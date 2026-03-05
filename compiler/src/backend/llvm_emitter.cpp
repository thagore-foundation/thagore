#include "internal.hpp"

namespace thagc::codegen {

static void run_coroutine_passes(llvm::Module& module) {
#if THAGC_LLVM_VERSION_MAJOR >= 21
  // LLVM 21 no longer exposes legacy addCoroutinePassesToExtensionPoints; run
  // the coroutine lowering pipeline explicitly with the new PM.
  llvm::LoopAnalysisManager lam;
  llvm::FunctionAnalysisManager fam;
  llvm::CGSCCAnalysisManager cgam;
  llvm::ModuleAnalysisManager mam;
  llvm::PassBuilder pb;
  pb.registerModuleAnalyses(mam);
  pb.registerCGSCCAnalyses(cgam);
  pb.registerFunctionAnalyses(fam);
  pb.registerLoopAnalyses(lam);
  pb.crossRegisterProxies(lam, fam, cgam, mam);

  llvm::ModulePassManager mpm;
  mpm.addPass(llvm::CoroEarlyPass());
  mpm.addPass(llvm::createModuleToPostOrderCGSCCPassAdaptor(llvm::CoroSplitPass()));
  llvm::FunctionPassManager fpm;
  fpm.addPass(llvm::CoroElidePass());
  mpm.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(fpm)));
  mpm.addPass(llvm::CoroCleanupPass());
  mpm.run(module, mam);
#else
  llvm::LoopAnalysisManager lam;
  llvm::FunctionAnalysisManager fam;
  llvm::CGSCCAnalysisManager cgam;
  llvm::ModuleAnalysisManager mam;
  llvm::PassBuilder pb;
  pb.registerModuleAnalyses(mam);
  pb.registerCGSCCAnalyses(cgam);
  pb.registerFunctionAnalyses(fam);
  pb.registerLoopAnalyses(lam);
  pb.crossRegisterProxies(lam, fam, cgam, mam);

  llvm::ModulePassManager mpm;
  llvm::FunctionPassManager fpm;
  fpm.addPass(llvm::CoroEarlyPass());
  fpm.addPass(llvm::CoroElidePass());
  fpm.addPass(llvm::CoroCleanupPass());
  mpm.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(fpm)));
  mpm.run(module, mam);
#endif
}

static void run_performance_passes(llvm::Module& module, llvm::TargetMachine* target_machine) {
  llvm::PipelineTuningOptions tuning;
  tuning.LoopInterleaving = true;
  tuning.LoopVectorization = true;
  tuning.SLPVectorization = true;
  tuning.LoopUnrolling = true;
  tuning.ForgetAllSCEVInLoopUnroll = true;

  llvm::LoopAnalysisManager lam;
  llvm::FunctionAnalysisManager fam;
  llvm::CGSCCAnalysisManager cgam;
  llvm::ModuleAnalysisManager mam;
  llvm::PassBuilder pb(target_machine, tuning);
  pb.registerModuleAnalyses(mam);
  pb.registerCGSCCAnalyses(cgam);
  pb.registerFunctionAnalyses(fam);
  pb.registerLoopAnalyses(lam);
  pb.crossRegisterProxies(lam, fam, cgam, mam);

  llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O3);
  mpm.run(module, mam);
}

static void set_module_target_triple(llvm::Module& module, const llvm::Triple& triple) {
#if THAGC_LLVM_VERSION_MAJOR >= 21
  module.setTargetTriple(triple);
#else
  module.setTargetTriple(triple.getTriple());
#endif
}

static std::unique_ptr<llvm::TargetMachine> create_target_machine_compat(const llvm::Target* target,
                                                                          const llvm::Triple& triple,
                                                                          const llvm::TargetOptions& options) {
#if THAGC_LLVM_VERSION_MAJOR >= 21
  return std::unique_ptr<llvm::TargetMachine>(
      target->createTargetMachine(triple, "generic", "", options, std::nullopt, std::nullopt,
                                  llvm::CodeGenOptLevel::Aggressive));
#else
  llvm::Optional<llvm::Reloc::Model> reloc;
  llvm::Optional<llvm::CodeModel::Model> code_model;
  return std::unique_ptr<llvm::TargetMachine>(
      target->createTargetMachine(triple.getTriple(), "generic", "", options, reloc, code_model,
                                  llvm::CodeGenOpt::Aggressive));
#endif
}

bool LlvmEmitter::emit_llvm_ir(const lowering::CoreProgram& core, const std::string& module_name,
                               const std::string& llvm_ir_path, const std::string& target_triple,
                               support::DiagnosticSink& diag) const {
  llvm::LLVMContext context;
  auto module = build_module(context, module_name, core, diag);
  if (!module) {
    return false;
  }
  if (!target_triple.empty()) {
    set_module_target_triple(*module, llvm::Triple(target_triple));
  }
  run_coroutine_passes(*module);
  run_performance_passes(*module, nullptr);
  std::string verify_error;
  llvm::raw_string_ostream verify_stream(verify_error);
  if (llvm::verifyModule(*module, &verify_stream)) {
    verify_stream.flush();
    diag.error("E2001", "LLVM module verification failed: " + trim(verify_error));
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
                              const std::string& object_path, const std::string& target_triple,
                              support::DiagnosticSink& diag) const {
  if (llvm::InitializeNativeTarget()) {
    diag.error("E2003", "cannot initialize native LLVM target");
    return false;
  }
  if (llvm::InitializeNativeTargetAsmParser()) {
    diag.error("E2003", "cannot initialize native LLVM asm parser");
    return false;
  }
  if (llvm::InitializeNativeTargetAsmPrinter()) {
    diag.error("E2003", "cannot initialize native LLVM asm printer");
    return false;
  }

  LLVMInitializeX86TargetInfo();
  LLVMInitializeX86Target();
  LLVMInitializeX86TargetMC();
  LLVMInitializeX86AsmParser();
  LLVMInitializeX86AsmPrinter();

  llvm::LLVMContext context;
  auto module = build_module(context, module_name, core, diag);
  if (!module) {
    return false;
  }

  std::string error;
  const std::string triple_value = target_triple.empty() ? llvm::sys::getDefaultTargetTriple() : target_triple;
  const llvm::Triple triple(triple_value);
  set_module_target_triple(*module, triple);

  const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple.getTriple(), error);
  if (!target) {
    diag.error("E2003", "cannot resolve LLVM target: " + error);
    return false;
  }

  llvm::TargetOptions options;
  std::unique_ptr<llvm::TargetMachine> target_machine(create_target_machine_compat(target, triple, options));
  if (!target_machine) {
    diag.error("E2004", "cannot create LLVM target machine");
    return false;
  }
  module->setDataLayout(target_machine->createDataLayout());
  run_coroutine_passes(*module);
  run_performance_passes(*module, target_machine.get());

  std::string verify_error;
  llvm::raw_string_ostream verify_stream(verify_error);
  if (llvm::verifyModule(*module, &verify_stream)) {
    verify_stream.flush();
    diag.error("E2001", "LLVM module verification failed: " + trim(verify_error));
    return false;
  }

  std::error_code ec;
  llvm::raw_fd_ostream obj_file(object_path, ec, llvm::sys::fs::OF_None);
  if (ec) {
    diag.error("E2005", "cannot open object output file: " + object_path);
    return false;
  }

  llvm::legacy::PassManager pass;
#if THAGC_LLVM_VERSION_MAJOR >= 21
  auto file_type = llvm::CodeGenFileType::ObjectFile;
#else
  auto file_type = llvm::CGFT_ObjectFile;
#endif
  if (target_machine->addPassesToEmitFile(pass, obj_file, nullptr, file_type)) {
    diag.error("E2006", "target does not support object emission");
    return false;
  }
  pass.run(*module);
  obj_file.flush();
  (void)core;
  return true;
}

}  // namespace thagc::codegen
