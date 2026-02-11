#include "thagore/backend/ir_generator.hpp"
#include "thagore/frontend/lexer.hpp"
#include "thagore/frontend/parser.hpp"
#include "thagore/frontend/semantic.hpp"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace thagore;

namespace {

void testIndentLexer() {
  constexpr std::string_view source = R"(func main():
  let x = 10
  if x > 0:
    return x
)";
  Lexer lexer {};
  auto tokens = lexer.tokenize(source, "test.thg");
  assert(tokens.has_value());

  bool hasIndent = false;
  bool hasDedent = false;
  for (const auto &tok : *tokens) {
    hasIndent = hasIndent || tok.kind == TokenKind::Indent;
    hasDedent = hasDedent || tok.kind == TokenKind::Dedent;
  }
  assert(hasIndent);
  assert(hasDedent);
}

void testPipelineIR() {
  constexpr std::string_view source = R"(func main():
  let x = 41
  return x + 1
)";
  Lexer lexer {};
  auto tokens = lexer.tokenize(source, "pipeline.thg");
  assert(tokens.has_value());

  Parser parser {};
  auto ast = parser.parseModule(*tokens);
  assert(ast.has_value());

  SemanticAnalyzer semantic {};
  auto typed = semantic.analyze(std::move(*ast));
  assert(typed.has_value());

  llvm::LLVMContext context;
  IRGenerator gen {context};
  auto mod = gen.lower(*typed, "pipeline");
  assert(mod.has_value());
}

} // namespace

int main() {
  testIndentLexer();
  testPipelineIR();
  std::cout << "All tests passed\n";
  return 0;
}
