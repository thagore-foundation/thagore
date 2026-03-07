//! Function return-inference helpers shared by the main type checker.

use crate::types::TypeId;

/// Inferred return-type mismatch discovered after walking a function body.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct ReturnInferenceError {
    /// Candidate type expected for every explicit return path.
    pub expected: TypeId,
    /// Type found on the mismatching path.
    pub found: TypeId,
    /// Source span to diagnose.
    pub span: thagore_ast::Span,
}

/// Infers one concrete function return type from explicit return sites.
pub(crate) fn infer_function_return_type(
    return_types: &[ResolvedReturnType],
    guarantees_return: bool,
    unit: TypeId,
) -> Result<TypeId, ReturnInferenceError> {
    if return_types.is_empty() {
        return Ok(unit);
    }

    let mut iter = return_types.iter().copied();
    let first = iter.next().expect("return types is not empty");
    let candidate = first.ty;

    for site in iter {
        let found = site.ty;
        if found != candidate {
            return Err(ReturnInferenceError {
                expected: candidate,
                found,
                span: site.span,
            });
        }
    }

    if candidate != unit && !guarantees_return {
        return Err(ReturnInferenceError {
            expected: candidate,
            found: unit,
            span: first.span,
        });
    }

    Ok(candidate)
}

/// One resolved return-site type used during inference finalization.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct ResolvedReturnType {
    /// Return-site type after local inference resolution.
    pub ty: TypeId,
    /// Span of the originating `return` statement.
    pub span: thagore_ast::Span,
}
