#pragma once

#include <string>

namespace thagc::syntax {

enum class TokenKind {
  Identifier,
  Number,
  String,
  KeywordFunc,
  KeywordLet,
  KeywordReturn,
  KeywordIf,
  KeywordWhile,
  KeywordStruct,
  KeywordImpl,
  KeywordImport,
  KeywordExtern,
  Colon,
  LParen,
  RParen,
  Comma,
  Arrow,
  Equal,
  EqualEqual,
  BangEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  Plus,
  Minus,
  Star,
  Slash,
  Newline,
  EndOfFile,
  Unknown,
};

struct Token {
  TokenKind kind = TokenKind::Unknown;
  std::string lexeme;
  int line = 1;
  int column = 1;
};

}  // namespace thagc::syntax
