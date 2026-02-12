#include "thagore/frontend/lexer.hpp"

#include <cctype>
#include <format>
#include <string>
#include <unordered_map>
#include <vector>

namespace thagore {
namespace {

struct Cursor {
  std::size_t index {0};
  std::size_t line {1};
  std::size_t column {1};
};

auto makeSpan(const Cursor &begin, const Cursor &end, const std::string &file) -> SourceSpan {
  return SourceSpan {
    .begin = SourceLocation {.line = begin.line, .column = begin.column, .offset = begin.index},
    .end = SourceLocation {.line = end.line, .column = end.column, .offset = end.index},
    .file = file,
  };
}

auto isIdentifierHead(char ch) -> bool {
  return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

auto isIdentifierBody(char ch) -> bool {
  return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

auto keywordKind(std::string_view text) -> TokenKind {
  static const std::unordered_map<std::string_view, TokenKind> kKeywords {
    {"func", TokenKind::KwFunc},
    {"return", TokenKind::KwReturn},
    {"let", TokenKind::KwLet},
    {"attempt", TokenKind::KwAttempt},
    {"if", TokenKind::KwIf},
    {"else", TokenKind::KwElse},
    {"while", TokenKind::KwWhile},
    {"loop", TokenKind::KwLoop},
    {"struct", TokenKind::KwStruct},
  };
  const auto it = kKeywords.find(text);
  if (it == kKeywords.end()) {
    return TokenKind::Identifier;
  }
  return it->second;
}

} // namespace

auto Lexer::tokenize(std::string_view source, std::string file) -> Result<std::vector<Token>, Diagnostic> {
  std::vector<Token> tokens {};
  std::vector<std::size_t> indents {0};
  Cursor cursor {};

  auto atEnd = [&]() { return cursor.index >= source.size(); };

  auto peek = [&](std::size_t lookahead = 0U) -> char {
    if (cursor.index + lookahead >= source.size()) {
      return '\0';
    }
    return source[cursor.index + lookahead];
  };

  auto advance = [&]() -> char {
    const char ch = peek();
    if (ch == '\0') {
      return ch;
    }
    cursor.index += 1;
    if (ch == '\n') {
      cursor.line += 1;
      cursor.column = 1;
    } else {
      cursor.column += 1;
    }
    return ch;
  };

  auto pushToken = [&](TokenKind kind, Cursor begin, Cursor end, std::string lexeme = {}) {
    tokens.push_back(Token {
      .kind = kind,
      .lexeme = std::move(lexeme),
      .span = makeSpan(begin, end, file),
    });
  };

  auto error = [&](std::string message, Cursor begin, Cursor end) -> Result<std::vector<Token>, Diagnostic> {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::LexError,
      .message = std::move(message),
      .span = makeSpan(begin, end, file),
    });
  };

  auto scanIndentation = [&]() -> Result<void, Diagnostic> {
    std::size_t spaces = 0;
    Cursor begin = cursor;
    while (!atEnd()) {
      const char ch = peek();
      if (ch == ' ') {
        spaces += 1;
        advance();
        continue;
      }
      if (ch == '\t') {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::LexError,
          .message = "Tabs are not allowed in indentation.",
          .span = makeSpan(begin, cursor, file),
        });
      }
      break;
    }

    const auto current = indents.back();
    if (spaces > current) {
      indents.push_back(spaces);
      pushToken(TokenKind::Indent, begin, cursor);
      return {};
    }

    if (spaces == current) {
      return {};
    }

    while (indents.size() > 1 && spaces < indents.back()) {
      const auto old = cursor;
      indents.pop_back();
      pushToken(TokenKind::Dedent, old, old);
    }

    if (indents.back() != spaces) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::LexError,
        .message = std::format("Invalid dedent level {}.", spaces),
        .span = makeSpan(begin, cursor, file),
      });
    }
    return {};
  };

  bool atLineStart = true;

  while (!atEnd()) {
    if (atLineStart) {
      if (peek() == '\n') {
        Cursor b = cursor;
        advance();
        pushToken(TokenKind::Newline, b, cursor);
        continue;
      }
      auto indentResult = scanIndentation();
      if (!indentResult) {
        return std::unexpected(indentResult.error());
      }
      atLineStart = false;
      if (peek() == '\n') {
        Cursor b = cursor;
        advance();
        pushToken(TokenKind::Newline, b, cursor);
        atLineStart = true;
        continue;
      }
    }

    const char ch = peek();
    if (ch == '\0') {
      break;
    }

    if (ch == '#') {
      while (!atEnd() && peek() != '\n') {
        advance();
      }
      continue;
    }

    if (ch == ' ' || ch == '\r' || ch == '\t') {
      advance();
      continue;
    }

    if (ch == '\n') {
      Cursor b = cursor;
      advance();
      pushToken(TokenKind::Newline, b, cursor);
      atLineStart = true;
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      Cursor begin = cursor;
      std::string text {};
      while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
        text.push_back(advance());
      }
      pushToken(TokenKind::Integer, begin, cursor, text);
      continue;
    }

    if (isIdentifierHead(ch)) {
      Cursor begin = cursor;
      std::string text {};
      while (!atEnd() && isIdentifierBody(peek())) {
        text.push_back(advance());
      }
      pushToken(keywordKind(text), begin, cursor, text);
      continue;
    }

    if (ch == '"') {
      Cursor begin = cursor;
      advance();
      std::string text {};
      while (!atEnd() && peek() != '"') {
        if (peek() == '\n') {
          return error("Unterminated string literal.", begin, cursor);
        }
        text.push_back(advance());
      }
      if (atEnd()) {
        return error("Unterminated string literal.", begin, cursor);
      }
      advance();
      pushToken(TokenKind::String, begin, cursor, text);
      continue;
    }

    Cursor begin = cursor;
    const char current = advance();
    switch (current) {
      case '(':
        pushToken(TokenKind::LParen, begin, cursor);
        break;
      case ')':
        pushToken(TokenKind::RParen, begin, cursor);
        break;
      case ':':
        pushToken(TokenKind::Colon, begin, cursor);
        break;
      case ',':
        pushToken(TokenKind::Comma, begin, cursor);
        break;
      case '.':
        pushToken(TokenKind::Dot, begin, cursor);
        break;
      case '+':
        pushToken(TokenKind::Plus, begin, cursor);
        break;
      case '-':
        if (peek() == '>') {
          advance();
          pushToken(TokenKind::Arrow, begin, cursor);
        } else {
          pushToken(TokenKind::Minus, begin, cursor);
        }
        break;
      case '*':
        pushToken(TokenKind::Star, begin, cursor);
        break;
      case '/':
        pushToken(TokenKind::Slash, begin, cursor);
        break;
      case '=':
        if (peek() == '=') {
          advance();
          pushToken(TokenKind::EqEq, begin, cursor);
        } else {
          pushToken(TokenKind::Equal, begin, cursor);
        }
        break;
      case '!':
        if (peek() == '=') {
          advance();
          pushToken(TokenKind::NotEq, begin, cursor);
          break;
        }
        return error("Unexpected '!'.", begin, cursor);
      case '<':
        if (peek() == '=') {
          advance();
          pushToken(TokenKind::LessEq, begin, cursor);
        } else {
          pushToken(TokenKind::Less, begin, cursor);
        }
        break;
      case '>':
        if (peek() == '=') {
          advance();
          pushToken(TokenKind::GreaterEq, begin, cursor);
        } else {
          pushToken(TokenKind::Greater, begin, cursor);
        }
        break;
      default:
        return error(std::format("Unexpected character '{}'.", current), begin, cursor);
    }
  }

  while (indents.size() > 1) {
    indents.pop_back();
    pushToken(TokenKind::Dedent, cursor, cursor);
  }

  pushToken(TokenKind::Eof, cursor, cursor);
  return tokens;
}

} // namespace thagore
