#![no_std]
//! Recursive-descent parser for the Thagore programming language.

extern crate alloc;

pub mod decl;
pub mod error;
pub mod expr;
pub mod parser;
pub mod stmt;
pub mod types;

pub use crate::error::{
    is_declaration_start, is_expr_terminator, is_lexer_error_token, is_statement_boundary,
    is_statement_start, is_sync_point, span_from_lexer, ConditionDelimiter, ErrorKind, Expectation,
    ParseError,
};
pub use crate::parser::Parser;
