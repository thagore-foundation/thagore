#pragma once

#include <string>
#include <vector>

#include "thagc/middleend/core_ir.hpp"
#include "thagc/domain/model.hpp"
#include "thagc/shared/diag.hpp"
#include "thagc/frontend/ast.hpp"
#include "thagc/frontend/token.hpp"

namespace thagc::application {

class LexerPort {
 public:
  virtual ~LexerPort() = default;
  virtual std::vector<syntax::Token> tokenize(const std::string& source) = 0;
};

class ParserPort {
 public:
  virtual ~ParserPort() = default;
  virtual syntax::AstProgram parse(const std::vector<syntax::Token>& tokens, const std::string& source) = 0;
};

class TypeCheckerPort {
 public:
  virtual ~TypeCheckerPort() = default;
  virtual bool check(const syntax::AstProgram& program, support::DiagnosticSink& diag) = 0;
};

class LoweringPort {
 public:
  virtual ~LoweringPort() = default;
  virtual lowering::CoreProgram lower(const syntax::AstProgram& program) = 0;
};

class CodegenPort {
 public:
  virtual ~CodegenPort() = default;
  virtual bool emit_object(const lowering::CoreProgram& core, const std::string& module_name,
                           const std::string& object_path, const std::string& target_triple, int opt_level,
                           support::DiagnosticSink& diag) = 0;
  virtual bool emit_llvm_ir(const lowering::CoreProgram& core, const std::string& module_name,
                            const std::string& llvm_ir_path, const std::string& target_triple, int opt_level,
                            support::DiagnosticSink& diag) = 0;
};

class LinkerPort {
 public:
  virtual ~LinkerPort() = default;
  virtual domain::LinkResult link_executable(const domain::LinkPlan& plan, support::DiagnosticSink& diag) = 0;
};

}  // namespace thagc::application
