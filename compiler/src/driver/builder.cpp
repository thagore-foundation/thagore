#include "thagc/driver/pipeline.hpp"

#include <stdexcept>

#include "thagc/application/build_usecase.hpp"
#include "thagc/domain/model.hpp"
#include "thagc/infra/adapters.hpp"
#include "thagc/shared/filesystem.hpp"

namespace thagc::driver {

bool CompilerPipeline::build(const BuildOptions& options, support::DiagnosticSink& diag) const {
  try {
    infra::LexerAdapter lexer;
    infra::ParserAdapter parser;
    infra::TypeCheckerAdapter checker;
    infra::LoweringAdapter lowerer;
    infra::LlvmCodegenAdapter codegen;
    infra::ClangLinkerAdapter linker;

    application::BuildUseCase usecase(lexer, parser, checker, lowerer, codegen, linker);
    domain::BuildRequest request;
    request.input_path = options.input_path;
    request.source_text = support::read_text_file(options.input_path);
    request.output_path = options.output_path;
    request.target_triple = options.target_triple;
    request.target_linker = options.target_linker;
    request.target_sysroot = options.target_sysroot;
    request.emit_llvm = options.emit_llvm;
    request.llvm_ir_path = options.llvm_ir_path;

    return usecase.execute(request, diag).success;
  } catch (const std::exception& ex) {
    diag.error("E3999", ex.what());
    return false;
  }
}

}  // namespace thagc::driver
