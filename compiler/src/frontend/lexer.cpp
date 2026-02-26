#include "thagc/frontend/lexer.hpp"

#include <cctype>

namespace thagc::syntax {

static TokenKind keyword_kind(const std::string& text) {
  if (text == "func") {
    return TokenKind::KeywordFunc;
  }
  if (text == "let") {
    return TokenKind::KeywordLet;
  }
  if (text == "return") {
    return TokenKind::KeywordReturn;
  }
  if (text == "if") {
    return TokenKind::KeywordIf;
  }
  if (text == "while") {
    return TokenKind::KeywordWhile;
  }
  return TokenKind::Identifier;
}

std::vector<Token> Lexer::tokenize(const std::string& source) const {
  std::vector<Token> tokens;
  int line = 1;
  int column = 1;
  for (std::size_t i = 0; i < source.size();) {
    const char ch = source[i];
    if (ch == '\r') {
      ++i;
      continue;
    }
    if (ch == '\n') {
      tokens.push_back(Token{TokenKind::Newline, "\n", line, column});
      ++line;
      column = 1;
      ++i;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(ch))) {
      ++column;
      ++i;
      continue;
    }
    if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
      const std::size_t start = i;
      const int start_col = column;
      while (i < source.size() &&
             (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_')) {
        ++i;
        ++column;
      }
      std::string word = source.substr(start, i - start);
      tokens.push_back(Token{keyword_kind(word), std::move(word), line, start_col});
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(ch))) {
      const std::size_t start = i;
      const int start_col = column;
      while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) {
        ++i;
        ++column;
      }
      tokens.push_back(Token{TokenKind::Number, source.substr(start, i - start), line, start_col});
      continue;
    }
    if (ch == '"') {
      const int start_col = column;
      const std::size_t start = i++;
      ++column;
      while (i < source.size() && source[i] != '"') {
        if (source[i] == '\n') {
          ++line;
          column = 1;
        } else {
          ++column;
        }
        ++i;
      }
      if (i < source.size()) {
        ++i;
        ++column;
      }
      tokens.push_back(Token{TokenKind::String, source.substr(start, i - start), line, start_col});
      continue;
    }

    TokenKind kind = TokenKind::Unknown;
    if (ch == ':') kind = TokenKind::Colon;
    if (ch == '(') kind = TokenKind::LParen;
    if (ch == ')') kind = TokenKind::RParen;
    if (ch == ',') kind = TokenKind::Comma;
    if (ch == '=') kind = TokenKind::Equal;
    if (ch == '+') kind = TokenKind::Plus;
    if (ch == '*') kind = TokenKind::Star;
    if (ch == '/') kind = TokenKind::Slash;
    if (ch == '-') {
      if (i + 1 < source.size() && source[i + 1] == '>') {
        tokens.push_back(Token{TokenKind::Arrow, "->", line, column});
        i += 2;
        column += 2;
        continue;
      }
      kind = TokenKind::Minus;
    }
    tokens.push_back(Token{kind, std::string(1, ch), line, column});
    ++i;
    ++column;
  }
  tokens.push_back(Token{TokenKind::EndOfFile, "", line, column});
  return tokens;
}

}  // namespace thagc::syntax

