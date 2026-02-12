#pragma once

#include "thagore/common/source.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace thagore {

enum class TokenKind : std::uint16_t {
  Eof,
  Newline,
  Indent,
  Dedent,

  Identifier,
  Integer,
  Float,
  String,
  InterpolatedString,

  KwFunc,
  KwReturn,
  KwLet,
  KwAttempt,
  KwIf,
  KwElse,
  KwWhile,
  KwLoop,
  KwStruct,
  KwImpl,
  KwExtern,
  KwImport,
  KwAs,

  LParen,
  RParen,
  LBracket,
  RBracket,
  Colon,
  Semicolon,
  Comma,
  Dot,
  Arrow,
  Equal,
  Plus,
  Minus,
  Star,
  Slash,
  EqEq,
  NotEq,
  Less,
  LessEq,
  Greater,
  GreaterEq,
};

struct Token {
  TokenKind kind {};
  std::string lexeme {};
  SourceSpan span {};
};

constexpr auto tokenKindName(TokenKind kind) -> std::string_view {
  switch (kind) {
    case TokenKind::Eof: return "eof";
    case TokenKind::Newline: return "newline";
    case TokenKind::Indent: return "indent";
    case TokenKind::Dedent: return "dedent";
    case TokenKind::Identifier: return "identifier";
    case TokenKind::Integer: return "integer";
    case TokenKind::Float: return "float";
    case TokenKind::String: return "string";
    case TokenKind::InterpolatedString: return "interpolated_string";
    case TokenKind::KwFunc: return "func";
    case TokenKind::KwReturn: return "return";
    case TokenKind::KwLet: return "let";
    case TokenKind::KwAttempt: return "attempt";
    case TokenKind::KwIf: return "if";
    case TokenKind::KwElse: return "else";
    case TokenKind::KwWhile: return "while";
    case TokenKind::KwLoop: return "loop";
    case TokenKind::KwStruct: return "struct";
    case TokenKind::KwImpl: return "impl";
    case TokenKind::KwExtern: return "extern";
    case TokenKind::KwImport: return "import";
    case TokenKind::KwAs: return "as";
    case TokenKind::LParen: return "(";
    case TokenKind::RParen: return ")";
    case TokenKind::LBracket: return "[";
    case TokenKind::RBracket: return "]";
    case TokenKind::Colon: return ":";
    case TokenKind::Semicolon: return ";";
    case TokenKind::Comma: return ",";
    case TokenKind::Dot: return ".";
    case TokenKind::Arrow: return "->";
    case TokenKind::Equal: return "=";
    case TokenKind::Plus: return "+";
    case TokenKind::Minus: return "-";
    case TokenKind::Star: return "*";
    case TokenKind::Slash: return "/";
    case TokenKind::EqEq: return "==";
    case TokenKind::NotEq: return "!=";
    case TokenKind::Less: return "<";
    case TokenKind::LessEq: return "<=";
    case TokenKind::Greater: return ">";
    case TokenKind::GreaterEq: return ">=";
  }
  return "unknown";
}

} // namespace thagore
