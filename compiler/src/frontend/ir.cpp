#include "thagc/frontend/parser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace thagc::syntax {

static std::string trim(const std::string& text) {
  std::size_t left = 0;
  while (left < text.size() && std::isspace(static_cast<unsigned char>(text[left]))) {
    ++left;
  }
  std::size_t right = text.size();
  while (right > left && std::isspace(static_cast<unsigned char>(text[right - 1]))) {
    --right;
  }
  return text.substr(left, right - left);
}

static bool starts_with(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

static std::string function_name_from_header(const std::string& line) {
  const std::size_t func_pos = line.find("func ");
  if (func_pos == std::string::npos) {
    return "";
  }
  std::size_t start = func_pos + 5;
  while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) {
    ++start;
  }
  std::size_t end = start;
  while (end < line.size() && (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '_')) {
    ++end;
  }
  return line.substr(start, end - start);
}

static std::string return_type_from_header(const std::string& line) {
  const std::size_t arrow = line.find("->");
  if (arrow == std::string::npos) {
    return "";
  }
  std::size_t start = arrow + 2;
  while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) {
    ++start;
  }
  std::size_t end = line.find(':', start);
  if (end == std::string::npos) {
    end = line.size();
  }
  return trim(line.substr(start, end - start));
}

static int parse_return_literal(const std::string& line) {
  std::size_t pos = line.find("return ");
  if (pos == std::string::npos) {
    return 0;
  }
  pos += 7;
  std::size_t end = pos;
  bool neg = false;
  if (end < line.size() && line[end] == '-') {
    neg = true;
    ++end;
  }
  std::size_t digits_start = end;
  while (end < line.size() && std::isdigit(static_cast<unsigned char>(line[end]))) {
    ++end;
  }
  if (digits_start == end) {
    return 0;
  }
  int value = std::stoi(line.substr(digits_start, end - digits_start));
  return neg ? -value : value;
}

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

  std::istringstream in(source);
  std::string line;
  int line_no = 0;
  AstFunction* current = nullptr;
  while (std::getline(in, line)) {
    ++line_no;
    const std::string clean = trim(line);
    if (clean.empty()) {
      continue;
    }
    if (starts_with(clean, "func ")) {
      AstFunction fn;
      fn.name = function_name_from_header(clean);
      fn.return_type = return_type_from_header(clean);
      fn.header_line = line_no;
      program.functions.push_back(fn);
      current = &program.functions.back();
      if (current->name == "main") {
        program.has_main = true;
      }
      continue;
    }
    if (!current) {
      continue;
    }
    AstStatement st;
    st.text = clean;
    st.line = line_no;
    if (starts_with(clean, "return ")) {
      st.kind = StatementKind::Return;
      if (current->name == "main") {
        program.main_return_literal = parse_return_literal(clean);
      }
    } else if (starts_with(clean, "let ")) {
      st.kind = StatementKind::Let;
    } else {
      st.kind = StatementKind::Expr;
    }
    current->body.push_back(st);
  }
  return program;
}

}  // namespace thagc::syntax
