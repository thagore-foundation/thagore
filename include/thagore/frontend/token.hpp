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
  String,

  KwFunc,
  KwReturn,
  KwLet,
  KwAttempt,
  KwIf,
  KwLoop,

  LParen,
  RParen,
  Colon,
  Comma,
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
    case TokenKind::String: return "string";
    case TokenKind::KwFunc: return "func";
    case TokenKind::KwReturn: return "return";
    case TokenKind::KwLet: return "let";
    case TokenKind::KwAttempt: return "attempt";
    case TokenKind::KwIf: return "if";
    case TokenKind::KwLoop: return "loop";
    case TokenKind::LParen: return "(";
    case TokenKind::RParen: return ")";
    case TokenKind::Colon: return ":";
    case TokenKind::Comma: return ",";
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
