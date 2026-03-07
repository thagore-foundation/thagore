//! Data-driven formatting rules shared across formatter components.

use thagore_ast::{Decl, ImportDecl};
use thagore_parser::Parser;

use crate::config::{FmtConfig, StylePreset};

/// Import source grouping used for strict-mode sorting.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum ImportGroup {
    /// Standard library module.
    Stdlib,
    /// External dependency.
    Dependency,
    /// Project-local module.
    Project,
    /// Relative module import.
    Relative,
}

/// Normalizes one comment line to `# comment` form.
#[must_use]
pub fn normalize_comment_text(text: &str) -> String {
    let trimmed = text.trim();
    let body = trimmed.trim_start_matches('#').trim();
    if body.is_empty() {
        String::from("#")
    } else {
        format!("# {body}")
    }
}

/// Returns the import group for one declaration.
#[must_use]
pub fn classify_import(import_path: &str) -> ImportGroup {
    if import_path.starts_with("./")
        || import_path.starts_with("../")
        || import_path == "."
        || import_path == ".."
    {
        return ImportGroup::Relative;
    }
    if import_path.starts_with("src.") {
        return ImportGroup::Project;
    }
    if import_path == "math"
        || import_path == "io"
        || import_path == "string"
        || import_path == "vec"
        || import_path == "sort"
        || import_path == "iter"
        || import_path == "fmt"
        || import_path == "convert"
        || import_path == "time"
        || import_path.starts_with("collections.")
    {
        return ImportGroup::Stdlib;
    }
    if import_path.contains('.') {
        return ImportGroup::Project;
    }
    ImportGroup::Dependency
}

/// Returns the number of blank lines required between two top-level declarations.
#[must_use]
pub fn blank_lines_between_decls<'ast>(
    config: &FmtConfig,
    prev: Option<&Decl<'ast>>,
    current: &Decl<'ast>,
    original_gap: usize,
) -> usize {
    if config.preserve_decl_blank_lines && matches!(config.style, StylePreset::Relaxed) {
        return original_gap.min(2);
    }

    match (prev, current) {
        (Some(previous), Decl::Func(_) | Decl::GenericFunc(_))
            if !matches!(previous, Decl::Func(_) | Decl::GenericFunc(_)) =>
        {
            2
        }
        (Some(_), _) => config.blank_lines_between_decls,
        (None, _) => 0,
    }
}

/// Returns the number of blank lines required between statements inside a block.
#[must_use]
pub fn blank_lines_between_statements(original_gap: usize) -> usize {
    usize::from(original_gap > 0)
}

/// Produces a stable sort key for an import declaration.
#[must_use]
pub fn import_sort_key<'ast>(
    parser: &Parser<'_, '_, 'ast>,
    import: &'ast ImportDecl<'ast>,
) -> (ImportGroup, String, String) {
    let path = import_path_text(parser, import);
    let symbol_key = if import.is_from {
        import
            .symbols
            .iter()
            .filter_map(|symbol| parser.resolve_symbol(symbol.name))
            .collect::<Vec<_>>()
            .join(",")
    } else {
        String::new()
    };
    (classify_import(&path), path, symbol_key)
}

/// Formats the raw import path text from an AST import declaration.
#[must_use]
pub fn import_path_text<'ast>(
    parser: &Parser<'_, '_, 'ast>,
    import: &'ast ImportDecl<'ast>,
) -> String {
    let joined = import
        .path_segments
        .iter()
        .filter_map(|segment| parser.resolve_symbol(*segment))
        .collect::<Vec<_>>()
        .join(".");
    if import.relative_level == 0 {
        return joined;
    }

    let mut prefix = String::new();
    for _ in 0..import.relative_level {
        prefix.push('.');
    }
    if joined.is_empty() {
        prefix
    } else {
        format!("{prefix}{joined}")
    }
}
