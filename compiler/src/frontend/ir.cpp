#include "thagc/frontend/parser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace thagc::syntax {

struct SourceLine {
  int number = 0;
  int indent = 0;
  std::string clean;
};

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

static bool ends_with(const std::string& text, const std::string& suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static int leading_indent(const std::string& line) {
  int indent = 0;
  for (char ch : line) {
    if (ch == ' ') {
      ++indent;
      continue;
    }
    if (ch == '\t') {
      indent += 2;
      continue;
    }
    break;
  }
  return indent;
}

static std::string strip_comments(const std::string& line) {
  std::size_t cut = line.size();
  const std::size_t hash = line.find('#');
  if (hash != std::string::npos) {
    cut = std::min(cut, hash);
  }
  const std::size_t slash = line.find("//");
  if (slash != std::string::npos) {
    cut = std::min(cut, slash);
  }
  return line.substr(0, cut);
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

static void add_parse_error(AstProgram& program, int line, const std::string& message) {
  program.parse_errors.push_back("line " + std::to_string(line) + ": " + message);
}

static bool valid_control_header(const std::string& keyword, const std::string& line) {
  if (!starts_with(line, keyword + " ")) {
    return false;
  }
  if (!ends_with(line, ":")) {
    return false;
  }
  const std::string head = trim(line.substr(0, line.size() - 1));
  const std::size_t lparen = head.find('(');
  const std::size_t rparen = head.rfind(')');
  if (lparen == std::string::npos || rparen == std::string::npos || lparen > rparen) {
    return false;
  }
  return true;
}

AstProgram Parser::parse(const std::vector<Token>& tokens, const std::string& source) const {
  (void)tokens;
  AstProgram program;
  program.source = source;

  std::vector<SourceLine> lines;
  std::istringstream in(source);
  std::string raw_line;
  int line_no = 0;
  while (std::getline(in, raw_line)) {
    ++line_no;
    const std::string stripped = strip_comments(raw_line);
    const std::string clean = trim(stripped);
    if (clean.empty()) {
      continue;
    }
    program.top_level_lines.push_back(clean);
    lines.push_back(SourceLine{line_no, leading_indent(raw_line), clean});
  }

  std::size_t i = 0;
  while (i < lines.size()) {
    const SourceLine& line = lines[i];
    if (starts_with(line.clean, "func ")) {
      AstFunction fn;
      fn.name = function_name_from_header(line.clean);
      fn.return_type = return_type_from_header(line.clean);
      fn.header_line = line.number;
      fn.header_indent = line.indent;

      if (!ends_with(line.clean, ":")) {
        add_parse_error(program, line.number, "function header must be colon-terminated");
      }
      if (fn.name.empty()) {
        add_parse_error(program, line.number, "invalid function header");
      }
      if (fn.return_type.empty()) {
        add_parse_error(program, line.number, "function header requires explicit return type");
      }

      if (fn.name == "main") {
        program.has_main = true;
      }

      ++i;
      if (i >= lines.size() || lines[i].indent <= fn.header_indent) {
        add_parse_error(program, line.number, "function body must be indentation-scoped");
      }

      while (i < lines.size() && lines[i].indent > fn.header_indent) {
        const SourceLine& body = lines[i];
        AstStatement st;
        st.text = body.clean;
        st.line = body.number;
        if (starts_with(body.clean, "if ")) {
          st.kind = StatementKind::If;
          if (!valid_control_header("if", body.clean)) {
            add_parse_error(program, body.number, "if requires parentheses and trailing ':'");
          }
        } else if (starts_with(body.clean, "while ")) {
          st.kind = StatementKind::While;
          if (!valid_control_header("while", body.clean)) {
            add_parse_error(program, body.number, "while requires parentheses and trailing ':'");
          }
        } else if (starts_with(body.clean, "return ")) {
          st.kind = StatementKind::Return;
          if (fn.name == "main") {
            program.main_return_literal = parse_return_literal(body.clean);
          }
        } else if (starts_with(body.clean, "let ")) {
          st.kind = StatementKind::Let;
        } else {
          st.kind = StatementKind::Expr;
        }
        fn.body.push_back(st);
        ++i;
      }

      program.functions.push_back(std::move(fn));
      continue;
    }

    if (starts_with(line.clean, "import ")) {
      program.imports.push_back(line.clean);
      ++i;
      continue;
    }
    if (starts_with(line.clean, "extern ")) {
      program.extern_decls.push_back(line.clean);
      ++i;
      continue;
    }
    if (starts_with(line.clean, "struct ")) {
      if (!ends_with(line.clean, ":")) {
        add_parse_error(program, line.number, "struct header must be colon-terminated");
      }
      program.structs.push_back(line.clean);
      ++i;
      continue;
    }
    if (starts_with(line.clean, "impl ")) {
      if (!ends_with(line.clean, ":")) {
        add_parse_error(program, line.number, "impl header must be colon-terminated");
      }
      program.impls.push_back(line.clean);
      ++i;
      continue;
    }

    ++i;
  }

  return program;
}

}  // namespace thagc::syntax
