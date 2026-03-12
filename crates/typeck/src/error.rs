//! Type-checking diagnostics for the Thagore compiler.
//!
//! The type checker never panics for user-facing typing failures. Every error
//! is recorded as a structured [`TypeError`] value and checking continues with
//! sentinel types where possible.

use core::fmt;

use thagore_ast::{InternedStr, Span};

use crate::types::TypeId;

/// A structured type-checking diagnostic.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum TypeError {
    /// A parsed construct exists in syntax but is not implemented end to end.
    UnsupportedFeature {
        /// Human-readable feature name.
        feature: &'static str,
        /// Source location of the unsupported construct.
        span: Span,
    },
    /// Control-flow statement appeared outside a valid loop context.
    InvalidControlFlow {
        /// Human-readable explanation of the misuse.
        message: &'static str,
        /// Source location of the invalid statement.
        span: Span,
    },
    /// Assignment target is syntactically valid but not supported end to end.
    InvalidAssignmentTarget {
        /// Source location of the invalid assignment target.
        span: Span,
    },
    /// Top-level constant initializer is not a compile-time constant expression.
    InvalidConstInitializer {
        /// Source location of the invalid initializer.
        span: Span,
    },
    /// Two types were required to be equal but were not.
    TypeMismatch {
        /// Expected type.
        expected: TypeId,
        /// Found type.
        found: TypeId,
        /// Source location for the mismatch.
        span: Span,
    },
    /// A referenced identifier was not found in the active lexical scopes.
    UnknownIdentifier {
        /// Missing identifier symbol.
        name: InternedStr,
        /// Source location of the identifier use.
        span: Span,
    },
    /// A field access referenced a field that does not exist on the struct.
    UnknownField {
        /// Struct type name.
        struct_name: InternedStr,
        /// Missing field name.
        field: InternedStr,
        /// Source location of the field access.
        span: Span,
    },
    /// A field access targeted a value that cannot expose fields or methods.
    NotFieldAccessible {
        /// Type that was used as the field access base.
        found: TypeId,
        /// Source location of the field access.
        span: Span,
    },
    /// A call expression targeted a non-function type.
    NotCallable {
        /// Type that was used as the callee.
        found: TypeId,
        /// Source location of the call.
        span: Span,
    },
    /// An index expression targeted a non-indexable type.
    NotIndexable {
        /// Type that was used as the indexed object.
        found: TypeId,
        /// Source location of the index operation.
        span: Span,
    },
    /// A call expression supplied the wrong number of arguments.
    ArgumentCountMismatch {
        /// Number of parameters expected by the callee.
        expected: usize,
        /// Number of arguments that were supplied.
        found: usize,
        /// Source location of the call.
        span: Span,
    },
    /// A returned expression did not match the enclosing function return type.
    ReturnTypeMismatch {
        /// Declared return type of the enclosing function.
        expected: TypeId,
        /// Actual type of the returned expression.
        found: TypeId,
        /// Source location of the return statement.
        span: Span,
    },
    /// A condition expression did not evaluate to `bool`.
    ConditionNotBool {
        /// Actual condition type.
        found: TypeId,
        /// Source location of the condition expression.
        span: Span,
    },
    /// Local type inference could not determine a concrete type.
    InferenceFailure {
        /// Source location where inference failed.
        span: Span,
    },
    /// Sentinel error used to prevent cascades after an earlier failure.
    Unknown {
        /// Source location associated with the recovered failure.
        span: Span,
    },
}

impl TypeError {
    /// Creates a sentinel recovery error.
    #[must_use]
    pub const fn unknown(span: Span) -> Self {
        Self::Unknown { span }
    }

    /// Returns the source span associated with the diagnostic.
    #[must_use]
    pub const fn span(&self) -> Span {
        match self {
            Self::UnsupportedFeature { span, .. }
            | Self::InvalidControlFlow { span, .. }
            | Self::InvalidAssignmentTarget { span }
            | Self::InvalidConstInitializer { span }
            | Self::TypeMismatch { span, .. }
            | Self::UnknownIdentifier { span, .. }
            | Self::UnknownField { span, .. }
            | Self::NotFieldAccessible { span, .. }
            | Self::NotCallable { span, .. }
            | Self::NotIndexable { span, .. }
            | Self::ArgumentCountMismatch { span, .. }
            | Self::ReturnTypeMismatch { span, .. }
            | Self::ConditionNotBool { span, .. }
            | Self::InferenceFailure { span }
            | Self::Unknown { span } => *span,
        }
    }
}

impl fmt::Display for TypeError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::UnsupportedFeature { feature, span } => {
                write!(f, "unsupported feature '{feature}' at {span}")
            }
            Self::InvalidControlFlow { message, span } => {
                write!(f, "{message} at {span}")
            }
            Self::InvalidAssignmentTarget { span } => {
                write!(f, "invalid assignment target at {span}")
            }
            Self::InvalidConstInitializer { span } => {
                write!(
                    f,
                    "top-level const initializer must be compile-time constant at {span}"
                )
            }
            Self::TypeMismatch {
                expected,
                found,
                span,
            } => write!(
                f,
                "type mismatch: expected {expected}, found {found} at {span}"
            ),
            Self::UnknownIdentifier { name, span } => {
                write!(f, "unknown identifier {name:?} at {span}")
            }
            Self::UnknownField {
                struct_name,
                field,
                span,
            } => write!(
                f,
                "unknown field {field:?} on struct {struct_name:?} at {span}"
            ),
            Self::NotFieldAccessible { found, span } => {
                write!(f, "type {found} has no fields or methods at {span}")
            }
            Self::NotCallable { found, span } => {
                write!(f, "type {found} is not callable at {span}")
            }
            Self::NotIndexable { found, span } => {
                write!(f, "type {found} is not indexable at {span}")
            }
            Self::ArgumentCountMismatch {
                expected,
                found,
                span,
            } => write!(
                f,
                "argument count mismatch: expected {expected}, found {found} at {span}"
            ),
            Self::ReturnTypeMismatch {
                expected,
                found,
                span,
            } => write!(
                f,
                "return type mismatch: expected {expected}, found {found} at {span}"
            ),
            Self::ConditionNotBool { found, span } => {
                write!(f, "condition must be bool, found {found} at {span}")
            }
            Self::InferenceFailure { span } => {
                write!(f, "type inference failed at {span}")
            }
            Self::Unknown { span } => {
                write!(f, "type checking failed after an earlier error at {span}")
            }
        }
    }
}
