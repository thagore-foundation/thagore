#include "thagc/application/build_usecase.hpp"

namespace thagc::application {

BuildUseCase::BuildUseCase(LexerPort& lexer, ParserPort& parser, TypeCheckerPort& checker, LoweringPort& lowerer,
                           CodegenPort& codegen, LinkerPort& linker)
    : lexer_(lexer), parser_(parser), checker_(checker), lowerer_(lowerer), codegen_(codegen), linker_(linker) {}

domain::BuildResult BuildUseCase::execute(const domain::BuildRequest& request, support::DiagnosticSink& diag) {
  domain::BuildResult result;
  const std::string source = request.source_text;
  const auto tokens = lexer_.tokenize(source);
  const auto ast = parser_.parse(tokens, source);
  if (!checker_.check(ast, diag)) {
    return result;
  }
  const auto core = lowerer_.lower(ast);
  if (request.emit_llvm) {
    if (!codegen_.emit_llvm_ir(core, "thagc_module", request.llvm_ir_path, diag)) {
      return result;
    }
    result.artifacts.push_back(request.llvm_ir_path);
    result.success = true;
    return result;
  }
  const std::string object_path = request.output_path + ".o";
  if (!codegen_.emit_object(core, "thagc_module", object_path, diag)) {
    return result;
  }
  domain::LinkPlan link_plan;
  link_plan.object_path = object_path;
  link_plan.output_path = request.output_path;
  const domain::LinkResult link_result = linker_.link_executable(link_plan, diag);
  if (!link_result.success) {
    const std::string detail = link_result.error.empty() ? "link step failed" : link_result.error;
    diag.error("E3200", detail + " (exit=" + std::to_string(link_result.exit_code) + ")");
    return result;
  }
  result.artifacts.push_back(object_path);
  result.artifacts.push_back(request.output_path);
  result.success = true;
  return result;
}

}  // namespace thagc::application
