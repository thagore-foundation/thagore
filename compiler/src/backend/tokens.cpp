#include "internal.hpp"

namespace thagc::codegen {

std::string trim(const std::string& text) {
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

std::string unescape_string_body(const std::string& body) {
  auto hex_value = [](char ch) -> int {
    if (ch >= '0' && ch <= '9') {
      return static_cast<int>(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
      return 10 + static_cast<int>(ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
      return 10 + static_cast<int>(ch - 'A');
    }
    return -1;
  };
  auto is_octal = [](char ch) -> bool { return ch >= '0' && ch <= '7'; };

  std::string out;
  out.reserve(body.size());
  for (std::size_t i = 0; i < body.size(); ++i) {
    const char ch = body[i];
    if (ch != '\\' || i + 1 >= body.size()) {
      out.push_back(ch);
      continue;
    }
    const char esc = body[++i];
    if (esc == 'n') {
      out.push_back('\n');
      continue;
    }
    if (esc == 'r') {
      out.push_back('\r');
      continue;
    }
    if (esc == 't') {
      out.push_back('\t');
      continue;
    }
    if (esc == '\\') {
      out.push_back('\\');
      continue;
    }
    if (esc == '"') {
      out.push_back('"');
      continue;
    }
    if (esc == 'e' || esc == 'E') {
      out.push_back(static_cast<char>(27));
      continue;
    }
    if (esc == 'x' || esc == 'X') {
      if (i + 2 < body.size()) {
        const int hi = hex_value(body[i + 1]);
        const int lo = hex_value(body[i + 2]);
        if (hi >= 0 && lo >= 0) {
          out.push_back(static_cast<char>((hi << 4) | lo));
          i += 2;
          continue;
        }
      }
      out.push_back('x');
      continue;
    }
    if (is_octal(esc)) {
      int value = static_cast<int>(esc - '0');
      int consumed = 0;
      while (consumed < 2 && i + 1 < body.size() && is_octal(body[i + 1])) {
        value = (value * 8) + static_cast<int>(body[i + 1] - '0');
        ++i;
        ++consumed;
      }
      out.push_back(static_cast<char>(value & 0xFF));
      continue;
    }
    out.push_back(esc);
  }
  return out;
}

bool starts_with(const std::string& text, std::string_view prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string& text, std::string_view suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool is_ident_start(char ch) {
  return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}

bool is_ident_body(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

bool is_integer_atom(const std::string& text) {
  if (text.empty()) {
    return false;
  }
  for (char ch : text) {
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return false;
    }
  }
  return true;
}

bool is_float_atom(const std::string& text) {
  if (text.empty()) {
    return false;
  }
  bool seen_dot = false;
  bool seen_digit = false;
  for (char ch : text) {
    if (std::isdigit(static_cast<unsigned char>(ch))) {
      seen_digit = true;
      continue;
    }
    if (ch == '.' && !seen_dot) {
      seen_dot = true;
      continue;
    }
    return false;
  }
  return seen_dot && seen_digit;
}

bool is_string_atom(const std::string& text) {
  return text.size() >= 2 && text.front() == '"' && text.back() == '"';
}

std::vector<ExprTok> tokenize_expression(const std::string& text, std::string& error) {
  std::vector<ExprTok> out;
  std::vector<std::string> closure_params;
  std::string closure_body;
  bool closure_block = false;
  if (parse_closure_literal(text, closure_params, closure_body, closure_block)) {
    out.push_back(ExprTok{ExprTokKind::Atom, trim(text)});
    out.push_back(ExprTok{ExprTokKind::End, ""});
    return out;
  }
  for (std::size_t i = 0; i < text.size();) {
    const char ch = text[i];
    if (std::isspace(static_cast<unsigned char>(ch))) {
      ++i;
      continue;
    }
    if (i + 1 < text.size()) {
      const std::string two = text.substr(i, 2);
      if (two == "==" || two == "!=" || two == "<=" || two == ">=") {
        out.push_back(ExprTok{ExprTokKind::Op, two});
        i += 2;
        continue;
      }
    }
    if (ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '<' || ch == '>') {
      out.push_back(ExprTok{ExprTokKind::Op, std::string(1, ch)});
      ++i;
      continue;
    }
    if (ch == '(') {
      out.push_back(ExprTok{ExprTokKind::LParen, "("});
      ++i;
      continue;
    }
    if (ch == ')') {
      out.push_back(ExprTok{ExprTokKind::RParen, ")"});
      ++i;
      continue;
    }
    if (ch == ',') {
      out.push_back(ExprTok{ExprTokKind::Comma, ","});
      ++i;
      continue;
    }
    if (ch == '[') {
      out.push_back(ExprTok{ExprTokKind::LParen, "["});
      ++i;
      continue;
    }
    if (ch == ']') {
      out.push_back(ExprTok{ExprTokKind::RParen, "]"});
      ++i;
      continue;
    }
    if (ch == '?') {
      out.push_back(ExprTok{ExprTokKind::Op, "?"});
      ++i;
      continue;
    }
    if (ch == '"') {
      const std::size_t start = i++;
      while (i < text.size() && text[i] != '"') {
        if (text[i] == '\\' && i + 1 < text.size()) {
          i += 2;
          continue;
        }
        ++i;
      }
      if (i >= text.size() || text[i] != '"') {
        error = "unterminated string literal in backend expression";
        return {};
      }
      ++i;
      out.push_back(ExprTok{ExprTokKind::Atom, text.substr(start, i - start)});
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(ch))) {
      const std::size_t start = i;
      bool has_dot = false;
      while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
        ++i;
      }
      if (i < text.size() && text[i] == '.') {
        has_dot = true;
        ++i;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
          ++i;
        }
      }
      if (has_dot && i == start + 1) {
        error = "invalid float literal";
        return {};
      }
      out.push_back(ExprTok{ExprTokKind::Atom, text.substr(start, i - start)});
      continue;
    }
    if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
      if (ch == 'v' && i + 1 < text.size() && text[i + 1] == '"') {
        const std::size_t start = i;
        i += 2;
        while (i < text.size() && text[i] != '"') {
          if (text[i] == '\\' && i + 1 < text.size()) {
            i += 2;
            continue;
          }
          ++i;
        }
        if (i >= text.size() || text[i] != '"') {
          error = "unterminated interpolated string literal in backend expression";
          return {};
        }
        ++i;
        out.push_back(ExprTok{ExprTokKind::Atom, text.substr(start, i - start)});
        continue;
      }
      const std::size_t start = i;
      while (i < text.size()) {
        if (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_') {
          ++i;
          continue;
        }
        if (text[i] == '.' && i + 1 < text.size() &&
            (std::isalpha(static_cast<unsigned char>(text[i + 1])) || text[i + 1] == '_' ||
             std::isdigit(static_cast<unsigned char>(text[i + 1])))) {
          ++i;
          continue;
        }
        break;
      }
      out.push_back(ExprTok{ExprTokKind::Atom, text.substr(start, i - start)});
      continue;
    }
    error = std::string("invalid expression token: '") + ch + "'";
    return {};
  }
  out.push_back(ExprTok{ExprTokKind::End, ""});
  return out;
}


}  // namespace thagc::codegen
