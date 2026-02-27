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
  KeywordElse,
  KeywordWhile,
  KeywordFor,
  KeywordIn,
  KeywordMatch,
  KeywordEnum,
  KeywordType,
  KeywordTrait,
  KeywordPub,
  KeywordUnsafe,
  KeywordDefer,
  KeywordComptime,
  KeywordStruct,
  KeywordImpl,
  KeywordImport,
  KeywordFrom,
  KeywordAs,
  KeywordExtern,
  KeywordFlow,
  KeywordIntent,
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
