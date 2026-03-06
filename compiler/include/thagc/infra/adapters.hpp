#pragma once

#include "thagc/application/ports.hpp"
#include "thagc/backend/llvm_emitter.hpp"
#include "thagc/frontend/typechecker.hpp"
#include "thagc/shared/process.hpp"
#include "thagc/frontend/lexer.hpp"
#include "thagc/frontend/parser.hpp"

namespace thagc::infra {

class LexerAdapter final : public application::LexerPort {
 public:
  std::vector<syntax::Token> tokenize(const std::string& source) override;
};

class ParserAdapter final : public application::ParserPort {
 public:
  syntax::AstProgram parse(const std::vector<syntax::Token>& tokens, const std::string& source) override;
};

class TypeCheckerAdapter final : public application::TypeCheckerPort {
 public:
  bool check(const syntax::AstProgram& program, support::DiagnosticSink& diag) override;
};

class LoweringAdapter final : public application::LoweringPort {
 public:
  lowering::CoreProgram lower(const syntax::AstProgram& program) override;
};

class LlvmCodegenAdapter final : public application::CodegenPort {
 public:
  bool emit_object(const lowering::CoreProgram& core, const std::string& module_name, const std::string& object_path,
                   const std::string& target_triple, int opt_level, support::DiagnosticSink& diag) override;
  bool emit_llvm_ir(const lowering::CoreProgram& core, const std::string& module_name, const std::string& llvm_ir_path,
                    const std::string& target_triple, int opt_level, support::DiagnosticSink& diag) override;
};

class ClangLinkerAdapter final : public application::LinkerPort {
 public:
  domain::LinkResult link_executable(const domain::LinkPlan& plan, support::DiagnosticSink& diag) override;
};

}  // namespace thagc::infra
