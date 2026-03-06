//! Structured diagnostics for the Thagore LLVM code generator.
//!
//! Code generation must never abort on the first failure. Instead, it records
//! [`CodegenError`] values, emits recovery IR where possible, and continues so
//! later phases can surface additional issues in the same run.

extern crate alloc;

use alloc::string::String;
use core::fmt;

use thagore_ast::{InternedStr, Span};
use thagore_ir::{BlockId, Value};
use thagore_typeck::TypeId;

/// A structured diagnostic emitted by the LLVM code generation pipeline.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CodegenError {
    /// Type lowering requested a `TypeId` that does not have a corresponding
    /// LLVM type mapping.
    MissingType {
        /// Unmapped type identifier.
        ty: TypeId,
        /// Source location associated with the failed lowering.
        span: Option<Span>,
    },
    /// A referenced SSA value does not exist in the current function's value
    /// map.
    UnknownValue {
        /// Missing IR value identifier.
        value: Value,
        /// Function symbol where lookup failed.
        function: InternedStr,
    },
    /// A referenced basic block does not exist in the current function's block
    /// map.
    UnknownBlock {
        /// Missing block identifier.
        block: BlockId,
        /// Function symbol where lookup failed.
        function: InternedStr,
    },
    /// A referenced function declaration is missing from the LLVM module.
    UnknownFunction {
        /// Missing function symbol.
        name: InternedStr,
        /// Source location associated with the reference, when available.
        span: Option<Span>,
    },
    /// A referenced struct declaration is missing from the LLVM module.
    UnknownStruct {
        /// Missing struct symbol.
        name: InternedStr,
        /// Source location associated with the reference, when available.
        span: Option<Span>,
    },
    /// A field access targeted a field that does not exist in the lowered LLVM
    /// struct layout.
    UnknownField {
        /// Struct symbol used as the base aggregate type.
        struct_name: InternedStr,
        /// Missing field symbol.
        field: InternedStr,
        /// Source location of the field operation, when available.
        span: Option<Span>,
    },
    /// An IR instruction required a concrete runtime value but the source IR
    /// value was `unit`.
    ValueHasNoRuntimeRepresentation {
        /// IR value that has no LLVM runtime representation.
        value: Value,
        /// Source location associated with the use, when available.
        span: Option<Span>,
    },
    /// An instruction or terminator used operands of an incompatible type for
    /// the LLVM builder operation being emitted.
    InvalidOperandType {
        /// Operand value that failed type validation.
        value: Value,
        /// Expected high-level type description.
        expected: &'static str,
        /// Actual lowered `TypeId`.
        found: TypeId,
        /// Source location associated with the use, when available.
        span: Option<Span>,
    },
    /// A terminator attempted to branch on a non-boolean condition.
    InvalidBranchCondition {
        /// IR value used as the branch condition.
        value: Value,
        /// Actual lowered `TypeId`.
        found: TypeId,
        /// Source location associated with the branch, when available.
        span: Option<Span>,
    },
    /// The requested LLVM intrinsic could not be declared or looked up.
    MissingIntrinsic {
        /// Intrinsic symbol name.
        name: &'static str,
    },
    /// The generated LLVM module failed verification.
    ModuleVerificationFailed {
        /// Verifier message returned by LLVM.
        message: String,
    },
    /// LLVM rejected an optimization pipeline stage.
    OptimizationFailed {
        /// Pass pipeline stage that failed.
        pass: &'static str,
        /// Optional LLVM diagnostic text.
        message: Option<String>,
    },
    /// Emitting a requested output artifact failed.
    OutputFailed {
        /// Path or artifact label that failed to emit.
        artifact: String,
        /// Failure detail.
        message: String,
    },
    /// Invoking the system linker failed.
    LinkFailed {
        /// Linker executable name or command label.
        linker: String,
        /// Failure detail.
        message: String,
    },
    /// Debug information emission failed.
    DebugInfoFailed {
        /// Failure detail.
        message: String,
    },
    /// Sentinel recovery error used after an earlier failure to keep codegen
    /// progressing without panicking.
    Unknown {
        /// Source location associated with the recovered failure.
        span: Option<Span>,
    },
}

impl CodegenError {
    /// Creates a sentinel recovery diagnostic.
    #[must_use]
    pub const fn unknown(span: Option<Span>) -> Self {
        Self::Unknown { span }
    }

    /// Returns the source span associated with the diagnostic, when one exists.
    #[must_use]
    pub const fn span(&self) -> Option<Span> {
        match self {
            Self::MissingType { span, .. }
            | Self::UnknownFunction { span, .. }
            | Self::UnknownStruct { span, .. }
            | Self::UnknownField { span, .. }
            | Self::ValueHasNoRuntimeRepresentation { span, .. }
            | Self::InvalidOperandType { span, .. }
            | Self::InvalidBranchCondition { span, .. }
            | Self::Unknown { span } => *span,
            Self::UnknownValue { .. }
            | Self::UnknownBlock { .. }
            | Self::MissingIntrinsic { .. }
            | Self::ModuleVerificationFailed { .. }
            | Self::OptimizationFailed { .. }
            | Self::OutputFailed { .. }
            | Self::LinkFailed { .. }
            | Self::DebugInfoFailed { .. } => None,
        }
    }
}

impl fmt::Display for CodegenError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::MissingType { ty, span } => {
                write_optional_span(f, *span, format_args!("missing LLVM type mapping for {ty}"))
            }
            Self::UnknownValue { value, function } => {
                write!(f, "unknown IR value {value} in function {function:?}")
            }
            Self::UnknownBlock { block, function } => {
                write!(f, "unknown IR block {block} in function {function:?}")
            }
            Self::UnknownFunction { name, span } => write_optional_span(
                f,
                *span,
                format_args!("unknown function {name:?} during codegen"),
            ),
            Self::UnknownStruct { name, span } => write_optional_span(
                f,
                *span,
                format_args!("unknown struct {name:?} during codegen"),
            ),
            Self::UnknownField {
                struct_name,
                field,
                span,
            } => write_optional_span(
                f,
                *span,
                format_args!("unknown field {field:?} on struct {struct_name:?} during codegen"),
            ),
            Self::ValueHasNoRuntimeRepresentation { value, span } => write_optional_span(
                f,
                *span,
                format_args!("value {value} has no LLVM runtime representation"),
            ),
            Self::InvalidOperandType {
                value,
                expected,
                found,
                span,
            } => write_optional_span(
                f,
                *span,
                format_args!(
                    "invalid operand type for {value}: expected {expected}, found {found}"
                ),
            ),
            Self::InvalidBranchCondition { value, found, span } => write_optional_span(
                f,
                *span,
                format_args!("invalid branch condition {value}: expected bool, found {found}"),
            ),
            Self::MissingIntrinsic { name } => {
                write!(f, "missing LLVM intrinsic {name}")
            }
            Self::ModuleVerificationFailed { message } => {
                write!(f, "LLVM module verification failed: {message}")
            }
            Self::OptimizationFailed { pass, message } => {
                if let Some(message) = message {
                    write!(f, "LLVM optimization pass {pass} failed: {message}")
                } else {
                    write!(f, "LLVM optimization pass {pass} failed")
                }
            }
            Self::OutputFailed { artifact, message } => {
                write!(f, "failed to emit {artifact}: {message}")
            }
            Self::LinkFailed { linker, message } => {
                write!(f, "linker {linker} failed: {message}")
            }
            Self::DebugInfoFailed { message } => {
                write!(f, "debug info emission failed: {message}")
            }
            Self::Unknown { span } => write_optional_span(
                f,
                *span,
                format_args!("code generation failed after an earlier error"),
            ),
        }
    }
}

fn write_optional_span(
    f: &mut fmt::Formatter<'_>,
    span: Option<Span>,
    message: fmt::Arguments<'_>,
) -> fmt::Result {
    match span {
        Some(span) => write!(f, "{message} at {span}"),
        None => write!(f, "{message}"),
    }
}
