//! Diagnostics produced while lowering Thagore ASTs into IR and validating the
//! resulting control-flow graph.
//!
//! The IR pipeline never panics for user-facing failures. Lowering records
//! structured [`LoweringError`] values, emits recovery IR where possible, and
//! continues so later phases can report additional issues in the same run.

use core::fmt;

use thagore_ast::{InternedStr, NodeId, Span};
use thagore_typeck::TypeId;

/// A structured diagnostic emitted by the IR lowering and validation passes.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum LoweringError {
    /// A referenced local, parameter, or function symbol could not be resolved.
    UnknownIdentifier {
        /// Missing symbol.
        name: InternedStr,
        /// Source location of the unresolved reference.
        span: Span,
    },
    /// No type information was available for an AST node during lowering.
    MissingType {
        /// AST node missing a type entry.
        node: NodeId,
        /// Source location of the node.
        span: Span,
    },
    /// A nominal struct symbol required by lowering could not be resolved.
    UnknownStruct {
        /// Missing struct symbol.
        name: InternedStr,
        /// Source location associated with the struct use.
        span: Span,
    },
    /// A field access or write targeted a field that does not exist.
    UnknownField {
        /// Struct symbol used as the base type.
        struct_name: InternedStr,
        /// Missing field symbol.
        field: InternedStr,
        /// Source location of the field operation.
        span: Span,
    },
    /// A call expression targeted a value that is not callable.
    NotCallable {
        /// Type of the rejected callee.
        found: TypeId,
        /// Source location of the call.
        span: Span,
    },
    /// An index expression targeted a non-indexable value.
    NotIndexable {
        /// Type of the rejected indexed object.
        found: TypeId,
        /// Source location of the index operation.
        span: Span,
    },
    /// An expression cannot be lowered as a store destination.
    InvalidAssignmentTarget {
        /// Source location of the invalid target expression.
        span: Span,
    },
    /// Lowering reached a construct that requires contextual information that
    /// was not available in the current IR function.
    InvalidLoweringState {
        /// Short description of the violated invariant.
        message: &'static str,
        /// Source location associated with the invariant break.
        span: Span,
    },
    /// A basic block did not end in a terminator after lowering.
    MissingTerminator {
        /// Numeric identifier of the malformed block.
        block: u32,
    },
    /// A basic block attempted to carry more than one terminator.
    DuplicateTerminator {
        /// Numeric identifier of the malformed block.
        block: u32,
    },
    /// A value was used before it was defined in the current function.
    UndefinedValue {
        /// Numeric SSA value identifier.
        value: u32,
        /// Numeric identifier of the block containing the use.
        block: u32,
    },
    /// A basic block referenced a successor or predecessor edge that does not
    /// exist in the function.
    InvalidBlockEdge {
        /// Numeric identifier of the source block.
        from: u32,
        /// Numeric identifier of the target block.
        to: u32,
    },
    /// A phi node referenced an incoming block that is not a predecessor of the
    /// block containing the phi.
    InvalidPhiInput {
        /// Numeric identifier of the block containing the phi.
        block: u32,
        /// Numeric identifier of the predecessor edge encoded by the phi.
        predecessor: u32,
        /// Numeric SSA value identifier used by the phi.
        value: u32,
    },
    /// Sentinel recovery error used after an earlier failure to suppress
    /// secondary panics while continuing IR construction.
    Unknown {
        /// Source location associated with the recovered failure.
        span: Span,
    },
}

impl LoweringError {
    /// Creates a sentinel recovery error.
    #[must_use]
    pub const fn unknown(span: Span) -> Self {
        Self::Unknown { span }
    }

    /// Returns the most relevant source span for the diagnostic, when one
    /// exists.
    #[must_use]
    pub const fn span(&self) -> Option<Span> {
        match self {
            Self::UnknownIdentifier { span, .. }
            | Self::MissingType { span, .. }
            | Self::UnknownStruct { span, .. }
            | Self::UnknownField { span, .. }
            | Self::NotCallable { span, .. }
            | Self::NotIndexable { span, .. }
            | Self::InvalidAssignmentTarget { span }
            | Self::InvalidLoweringState { span, .. }
            | Self::Unknown { span } => Some(*span),
            Self::MissingTerminator { .. }
            | Self::DuplicateTerminator { .. }
            | Self::UndefinedValue { .. }
            | Self::InvalidBlockEdge { .. }
            | Self::InvalidPhiInput { .. } => None,
        }
    }
}

impl fmt::Display for LoweringError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::UnknownIdentifier { name, span } => {
                write!(f, "unknown identifier {name:?} during IR lowering at {span}")
            }
            Self::MissingType { node, span } => {
                write!(f, "missing type for AST node {node} during IR lowering at {span}")
            }
            Self::UnknownStruct { name, span } => {
                write!(f, "unknown struct {name:?} during IR lowering at {span}")
            }
            Self::UnknownField {
                struct_name,
                field,
                span,
            } => write!(
                f,
                "unknown field {field:?} on struct {struct_name:?} during IR lowering at {span}"
            ),
            Self::NotCallable { found, span } => {
                write!(f, "cannot lower call on non-callable type {found} at {span}")
            }
            Self::NotIndexable { found, span } => {
                write!(f, "cannot lower index on non-indexable type {found} at {span}")
            }
            Self::InvalidAssignmentTarget { span } => {
                write!(f, "invalid assignment target during IR lowering at {span}")
            }
            Self::InvalidLoweringState { message, span } => {
                write!(f, "invalid IR lowering state: {message} at {span}")
            }
            Self::MissingTerminator { block } => {
                write!(f, "basic block {block} is missing a terminator")
            }
            Self::DuplicateTerminator { block } => {
                write!(f, "basic block {block} has more than one terminator")
            }
            Self::UndefinedValue { value, block } => {
                write!(f, "value v{value} is used before definition in block {block}")
            }
            Self::InvalidBlockEdge { from, to } => {
                write!(f, "invalid control-flow edge from block {from} to block {to}")
            }
            Self::InvalidPhiInput {
                block,
                predecessor,
                value,
            } => write!(
                f,
                "phi in block {block} references value v{value} from non-predecessor block {predecessor}"
            ),
            Self::Unknown { span } => {
                write!(f, "IR lowering failed after an earlier error at {span}")
            }
        }
    }
}
