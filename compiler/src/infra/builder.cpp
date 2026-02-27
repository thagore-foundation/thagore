#include "thagc/infra/adapters.hpp"

#include <string>
#include <vector>

namespace thagc::infra {

std::vector<syntax::Token> LexerAdapter::tokenize(const std::string& source) {
  return syntax::Lexer().tokenize(source);
}

syntax::AstProgram ParserAdapter::parse(const std::vector<syntax::Token>& tokens, const std::string& source) {
  return syntax::Parser().parse(tokens, source);
}

bool TypeCheckerAdapter::check(const syntax::AstProgram& program, support::DiagnosticSink& diag) {
  return semantics::TypeChecker().check(program, diag);
}

lowering::CoreProgram LoweringAdapter::lower(const syntax::AstProgram& program) {
  return lowering::lower_to_core(program);
}

bool LlvmCodegenAdapter::emit_object(const lowering::CoreProgram& core, const std::string& module_name,
                                     const std::string& object_path, const std::string& target_triple,
                                     support::DiagnosticSink& diag) {
  return codegen::LlvmEmitter().emit_object(core, module_name, object_path, target_triple, diag);
}

bool LlvmCodegenAdapter::emit_llvm_ir(const lowering::CoreProgram& core, const std::string& module_name,
                                      const std::string& llvm_ir_path, const std::string& target_triple,
                                      support::DiagnosticSink& diag) {
  return codegen::LlvmEmitter().emit_llvm_ir(core, module_name, llvm_ir_path, target_triple, diag);
}

domain::LinkResult ClangLinkerAdapter::link_executable(const domain::LinkPlan& plan,
                                                       support::DiagnosticSink& diag) {
  const std::string linker = plan.linker_path.empty() ? "clang" : plan.linker_path;
  std::vector<std::string> clang_link = {linker, plan.object_path, "-o", plan.output_path};
#if defined(__linux__)
  clang_link.push_back("-no-pie");
#endif
  if (!plan.target_triple.empty()) {
    clang_link.push_back("--target=" + plan.target_triple);
  }
  if (!plan.sysroot.empty()) {
    clang_link.push_back("--sysroot=" + plan.sysroot);
  }
  clang_link.insert(clang_link.end(), plan.extra_args.begin(), plan.extra_args.end());
  const int rc = support::run_process(clang_link);

  domain::LinkResult out;
  out.exit_code = rc;
  out.command = linker + " " + plan.object_path + " -o " + plan.output_path;
  if (rc != 0) {
    out.success = false;
    out.error = "clang link failed";
    diag.error("E3001", out.error + " with exit code: " + std::to_string(rc));
    return out;
  }
  out.success = true;
  return out;
}

}  // namespace thagc::infra
