#include "thagc/frontend/parser.hpp"

#include <sstream>

namespace thagc::syntax {

AstProgram Parser::parse(const std::vector<Token>& tokens, const std::string& source) const {
  AstProgram program;
  program.source = source;
  std::stringstream current_line;
  for (const Token& token : tokens) {
    if (token.kind == TokenKind::EndOfFile) {
      break;
    }
    if (token.kind == TokenKind::Newline) {
      const std::string line = current_line.str();
      if (!line.empty()) {
        program.top_level_lines.push_back(line);
      }
      current_line.str("");
      current_line.clear();
      continue;
    }
    if (!current_line.str().empty()) {
      current_line << ' ';
    }
    current_line << token.lexeme;
  }
  const std::string tail = current_line.str();
  if (!tail.empty()) {
    program.top_level_lines.push_back(tail);
  }
  return program;
}

}  // namespace thagc::syntax

