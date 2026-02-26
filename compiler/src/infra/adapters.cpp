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
                                     const std::string& object_path, support::DiagnosticSink& diag) {
  return codegen::LlvmEmitter().emit_object(core, module_name, object_path, diag);
}

bool LlvmCodegenAdapter::emit_llvm_ir(const lowering::CoreProgram& core, const std::string& module_name,
                                      const std::string& llvm_ir_path, support::DiagnosticSink& diag) {
  return codegen::LlvmEmitter().emit_llvm_ir(core, module_name, llvm_ir_path, diag);
}

bool ClangLinkerAdapter::link_executable(const std::string& object_path, const std::string& output_path,
                                         support::DiagnosticSink& diag) {
  const std::vector<std::string> clang_link = {
      "clang", object_path, "-o", output_path, "-lstdc++", runtime::runtime_library_name(),
  };
  const int rc = support::run_process(clang_link);
  if (rc != 0) {
    diag.error("E3001", "clang link failed with exit code: " + std::to_string(rc));
    return false;
  }
  return true;
}

}  // namespace thagc::infra
