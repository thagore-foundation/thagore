#![no_std]
//! Production lexer crate for the Thagore programming language.

extern crate alloc;

pub mod error;
pub mod intern;
pub mod lexer;
pub mod table;
pub mod token;

pub use crate::error::{
    INCONSISTENT_DEDENT_ERROR, INVALID_BANG_ERROR, INVALID_INDENTATION_ERROR, INVALID_TOKEN_ERROR,
    LexError, RecoveryKind, RecoveryPoint, TAB_INDENTATION_ERROR, UNTERMINATED_STRING_ERROR,
};
pub use crate::intern::{Interner, Symbol};
pub use crate::lexer::{Lexer, TokenStream};
pub use crate::table::{ByteClass, ClassifiedByte, DfaState, TokenAction, dispatch, transition};
pub use crate::token::{SliceRef, Span, Token, TokenData, TokenKind};
