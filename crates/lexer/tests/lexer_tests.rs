use bumpalo::Bump;
use thagore_lexer::{
    INVALID_INDENTATION_ERROR, Interner, Lexer, SliceRef, TAB_INDENTATION_ERROR, Token, TokenData,
    TokenKind, UNTERMINATED_STRING_ERROR,
};

fn collect_tokens(source: &str) -> Vec<Token> {
    let mut lexer = Lexer::new(source);
    let mut tokens = Vec::new();

    loop {
        let token = lexer.next_token();
        let done = token.kind == TokenKind::Eof;
        tokens.push(token);
        if done {
            break;
        }
    }

    tokens
}

fn kinds(source: &str) -> Vec<TokenKind> {
    collect_tokens(source)
        .into_iter()
        .map(|token| token.kind)
        .collect()
}

fn slice_text<'a>(source: &'a str, slice: SliceRef) -> &'a str {
    &source[slice.offset as usize..slice.end() as usize]
}

#[test]
fn lexes_all_keywords() {
    let source =
        "let func if else while for return from import include extern struct impl intent flow i32 f32 bool str";
    let expected = vec![
        TokenKind::Let,
        TokenKind::Func,
        TokenKind::If,
        TokenKind::Else,
        TokenKind::While,
        TokenKind::For,
        TokenKind::Return,
        TokenKind::From,
        TokenKind::Import,
        TokenKind::Include,
        TokenKind::Extern,
        TokenKind::Struct,
        TokenKind::Impl,
        TokenKind::Intent,
        TokenKind::Flow,
        TokenKind::I32,
        TokenKind::F32,
        TokenKind::Bool,
        TokenKind::Str,
        TokenKind::Eof,
    ];

    assert_eq!(kinds(source), expected);
}

#[test]
fn promotes_from_and_include_to_keywords() {
    let source = "from math import sqrt\nimport math include all\n";
    let expected = vec![
        TokenKind::From,
        TokenKind::Identifier,
        TokenKind::Import,
        TokenKind::Identifier,
        TokenKind::Newline,
        TokenKind::Import,
        TokenKind::Identifier,
        TokenKind::Include,
        TokenKind::Identifier,
        TokenKind::Newline,
        TokenKind::Eof,
    ];

    assert_eq!(kinds(source), expected);
}

#[test]
fn lexes_all_operators() {
    let source = "+ - * / % == != < > <= >= = -> : , . ( ) [ ]";
    let expected = vec![
        TokenKind::Plus,
        TokenKind::Minus,
        TokenKind::Star,
        TokenKind::Slash,
        TokenKind::Percent,
        TokenKind::EqEq,
        TokenKind::BangEq,
        TokenKind::Lt,
        TokenKind::Gt,
        TokenKind::LtEq,
        TokenKind::GtEq,
        TokenKind::Assign,
        TokenKind::Arrow,
        TokenKind::Colon,
        TokenKind::Comma,
        TokenKind::Dot,
        TokenKind::LParen,
        TokenKind::RParen,
        TokenKind::LBracket,
        TokenKind::RBracket,
        TokenKind::Eof,
    ];

    assert_eq!(kinds(source), expected);
}

#[test]
fn handles_indent_and_dedent_tokens() {
    let source = "func main:\n  let x = 1\n  if x:\n    return x\n  return 0\n";
    let expected = vec![
        TokenKind::Func,
        TokenKind::Identifier,
        TokenKind::Colon,
        TokenKind::Newline,
        TokenKind::Indent,
        TokenKind::Let,
        TokenKind::Identifier,
        TokenKind::Assign,
        TokenKind::Integer,
        TokenKind::Newline,
        TokenKind::If,
        TokenKind::Identifier,
        TokenKind::Colon,
        TokenKind::Newline,
        TokenKind::Indent,
        TokenKind::Return,
        TokenKind::Identifier,
        TokenKind::Newline,
        TokenKind::Dedent,
        TokenKind::Return,
        TokenKind::Integer,
        TokenKind::Newline,
        TokenKind::Dedent,
        TokenKind::Eof,
    ];

    assert_eq!(kinds(source), expected);
}

#[test]
fn ignores_blank_lines_and_comment_only_lines_for_indentation() {
    let source = "if cond:\n  let x = 1\n  # comment only\n\n  return x\n";
    let expected = vec![
        TokenKind::If,
        TokenKind::Identifier,
        TokenKind::Colon,
        TokenKind::Newline,
        TokenKind::Indent,
        TokenKind::Let,
        TokenKind::Identifier,
        TokenKind::Assign,
        TokenKind::Integer,
        TokenKind::Newline,
        TokenKind::Return,
        TokenKind::Identifier,
        TokenKind::Newline,
        TokenKind::Dedent,
        TokenKind::Eof,
    ];

    assert_eq!(kinds(source), expected);
}

#[test]
fn emits_error_for_tabs() {
    let tokens = collect_tokens("let\tname = 1\n");
    let error = tokens
        .into_iter()
        .find(|token| token.kind == TokenKind::Error)
        .expect("missing error token");

    assert_eq!(error.error_message(), Some(TAB_INDENTATION_ERROR));
}

#[test]
fn emits_error_for_invalid_indentation_width() {
    let tokens = collect_tokens("if ok:\n   return ok\n");
    let error = tokens
        .iter()
        .find(|token| token.kind == TokenKind::Error)
        .copied()
        .expect("missing error token");

    assert_eq!(error.error_message(), Some(INVALID_INDENTATION_ERROR));
}

#[test]
fn emits_error_for_unterminated_strings() {
    let tokens = collect_tokens("\"unterminated\n");
    let error = tokens
        .iter()
        .find(|token| token.kind == TokenKind::Error)
        .copied()
        .expect("missing error token");

    assert_eq!(error.error_message(), Some(UNTERMINATED_STRING_ERROR));
}

#[test]
fn supports_unicode_identifiers() {
    let source = "let π = δοκιμή\n";
    let tokens = collect_tokens(source);
    let identifiers: Vec<_> = tokens
        .iter()
        .filter(|token| token.kind == TokenKind::Identifier)
        .filter_map(|token| token.slice())
        .map(|slice| slice_text(source, slice).to_owned())
        .collect();

    assert_eq!(identifiers, vec!["π".to_owned(), "δοκιμή".to_owned()]);
}

#[test]
fn flushes_dedents_at_eof() {
    let expected = vec![
        TokenKind::If,
        TokenKind::Identifier,
        TokenKind::Colon,
        TokenKind::Newline,
        TokenKind::Indent,
        TokenKind::If,
        TokenKind::Identifier,
        TokenKind::Colon,
        TokenKind::Newline,
        TokenKind::Indent,
        TokenKind::Return,
        TokenKind::Identifier,
        TokenKind::Dedent,
        TokenKind::Dedent,
        TokenKind::Eof,
    ];

    assert_eq!(kinds("if a:\n  if b:\n    return b"), expected);
}

#[test]
fn uses_zero_copy_interner() {
    let source = "alpha beta";
    let interner = Interner::new(source);
    let symbol = interner.intern_range(0, 5);

    assert_eq!(interner.resolve(symbol), Some("alpha"));
}

#[test]
fn allocates_token_stream_in_bump_arena() {
    let source = "let answer = 42\n";
    let bump = Bump::new();
    let mut lexer = Lexer::new(source);
    let stream = lexer.lex_all_in(&bump);

    assert_eq!(stream.last().map(|token| token.kind), Some(TokenKind::Eof));
    assert!(stream.len() >= 5);
}

#[test]
fn keeps_string_payload_without_quotes() {
    let source = "\"hello\"";
    let token = collect_tokens(source)
        .into_iter()
        .find(|token| token.kind == TokenKind::String)
        .expect("missing string token");

    let TokenData::Slice(slice) = token.data else {
        panic!("expected slice payload");
    };

    assert_eq!(slice_text(source, slice), "hello");
}

#[test]
fn keeps_escaped_quote_payload_inside_strings() {
    let source = r#""\"quoted\"""#;
    let token = collect_tokens(source)
        .into_iter()
        .find(|token| token.kind == TokenKind::String)
        .expect("missing string token");

    let TokenData::Slice(slice) = token.data else {
        panic!("expected slice payload");
    };

    assert_eq!(slice_text(source, slice), r#"\"quoted\""#);
}
