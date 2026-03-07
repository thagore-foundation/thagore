//! Builtin function registry shared by the CLI compilation pipelines.

use std::collections::HashMap;

use bumpalo::Bump;
use thagore_ast::{Decl, ExternDecl, InternedStr, NamedTypeExpr, Param, Span, TypeExpr};
use thagore_codegen::builtins::builtin_runtime_symbol;
use thagore_codegen::Codegen;
use thagore_parser::Parser;

use crate::pipeline::{intern_owned_cached, next_synthetic_node};

/// Primitive type surface accepted by builtin declarations.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum BuiltinType {
    /// Pointer-backed string value.
    Str,
    /// Unit / void return.
    Void,
}

impl BuiltinType {
    fn as_surface_name(self) -> &'static str {
        match self {
            Self::Str => "str",
            Self::Void => "void",
        }
    }
}

/// One globally available builtin function.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct Builtin {
    /// Surface function name visible to user code.
    pub name: &'static str,
    /// Parameter list in declaration order.
    pub params: &'static [(&'static str, BuiltinType)],
    /// Return type.
    pub return_type: BuiltinType,
}

/// Builtins available in every Thagore compilation unit.
pub(crate) const BUILTINS: &[Builtin] = &[
    Builtin {
        name: "print",
        params: &[("s", BuiltinType::Str)],
        return_type: BuiltinType::Void,
    },
    Builtin {
        name: "println",
        params: &[("s", BuiltinType::Str)],
        return_type: BuiltinType::Void,
    },
    Builtin {
        name: "eprint",
        params: &[("s", BuiltinType::Str)],
        return_type: BuiltinType::Void,
    },
    Builtin {
        name: "eprintln",
        params: &[("s", BuiltinType::Str)],
        return_type: BuiltinType::Void,
    },
    Builtin {
        name: "flush",
        params: &[],
        return_type: BuiltinType::Void,
    },
];

/// Prepends synthetic extern declarations for the builtin surface.
pub(crate) fn prepend_builtin_externs<'ast>(
    arena: &'ast Bump,
    parser: &mut Parser<'_, '_, 'ast>,
    intern_cache: &mut HashMap<String, InternedStr>,
    next_node_id: &mut u32,
    decls: &mut Vec<Decl<'ast>>,
) -> Vec<(InternedStr, &'static str)> {
    let mut synthetic = Vec::with_capacity(BUILTINS.len());
    let mut runtime_symbols = Vec::with_capacity(BUILTINS.len());

    for builtin in BUILTINS {
        let surface_symbol = intern_owned_cached(parser, intern_cache, builtin.name);
        let params = builtin
            .params
            .iter()
            .map(|(name, ty)| Param {
                id: next_synthetic_node(next_node_id),
                span: Span::empty(),
                name: intern_owned_cached(parser, intern_cache, name),
                ty: arena.alloc(TypeExpr::Named(NamedTypeExpr {
                    id: next_synthetic_node(next_node_id),
                    span: Span::empty(),
                    name: intern_owned_cached(parser, intern_cache, ty.as_surface_name()),
                })),
            })
            .collect::<Vec<_>>();
        let params = arena.alloc_slice_fill_iter(params);
        let return_type = arena.alloc(TypeExpr::Named(NamedTypeExpr {
            id: next_synthetic_node(next_node_id),
            span: Span::empty(),
            name: intern_owned_cached(parser, intern_cache, builtin.return_type.as_surface_name()),
        }));
        synthetic.push(Decl::Extern(ExternDecl {
            id: next_synthetic_node(next_node_id),
            span: Span::empty(),
            name: surface_symbol,
            params,
            return_type,
        }));
        if let Some(runtime) = builtin_runtime_symbol(builtin.name) {
            runtime_symbols.push((surface_symbol, runtime));
        }
    }

    if !synthetic.is_empty() {
        synthetic.append(decls);
        *decls = synthetic;
    }

    runtime_symbols
}

/// Registers runtime linker names for builtin surface symbols.
pub(crate) fn register_builtin_runtime_symbols(
    codegen: &mut Codegen,
    bindings: &[(InternedStr, &'static str)],
) {
    for (symbol, runtime_name) in bindings {
        codegen.register_symbol_name(*symbol, runtime_name);
    }
}

/// Returns a close builtin suggestion for `name` when one exists.
pub(crate) fn builtin_suggestion(name: &str) -> Option<&'static str> {
    BUILTINS
        .iter()
        .filter_map(|builtin| {
            let distance = levenshtein(name, builtin.name);
            (distance <= 2).then_some((distance, builtin.name))
        })
        .min_by(|left, right| left.0.cmp(&right.0).then_with(|| left.1.cmp(right.1)))
        .map(|(_, name)| name)
}

fn levenshtein(left: &str, right: &str) -> usize {
    let left = left.chars().collect::<Vec<_>>();
    let right = right.chars().collect::<Vec<_>>();
    let mut prev = (0..=right.len()).collect::<Vec<_>>();
    let mut curr = vec![0; right.len() + 1];

    for (i, left_ch) in left.iter().enumerate() {
        curr[0] = i + 1;
        for (j, right_ch) in right.iter().enumerate() {
            let cost = usize::from(left_ch != right_ch);
            curr[j + 1] = (curr[j] + 1)
                .min(prev[j + 1] + 1)
                .min(prev[j] + cost);
        }
        prev.clone_from(&curr);
    }

    prev[right.len()]
}

