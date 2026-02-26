#pragma once

#include "thagc/application/ports.hpp"
#include "thagc/domain/model.hpp"
#include "thagc/shared/diag.hpp"

namespace thagc::application {

class BuildUseCase {
 public:
  BuildUseCase(LexerPort& lexer, ParserPort& parser, TypeCheckerPort& checker, LoweringPort& lowerer,
               CodegenPort& codegen, LinkerPort& linker);
  domain::BuildResult execute(const domain::BuildRequest& request, support::DiagnosticSink& diag);

 private:
  LexerPort& lexer_;
  ParserPort& parser_;
  TypeCheckerPort& checker_;
  LoweringPort& lowerer_;
  CodegenPort& codegen_;
  LinkerPort& linker_;
};

}  // namespace thagc::application

