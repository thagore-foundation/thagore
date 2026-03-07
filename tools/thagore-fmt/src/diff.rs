//! Diff and check helpers for `thagore-fmt`.

use std::path::Path;

use similar::TextDiff;

/// Returns `true` when the formatted text differs from the original text.
#[must_use]
pub fn needs_formatting(original: &str, formatted: &str) -> bool {
    original != formatted
}

/// Renders a unified diff for one formatted file.
#[must_use]
pub fn unified_diff(path: &Path, original: &str, formatted: &str) -> String {
    TextDiff::from_lines(original, formatted)
        .unified_diff()
        .header(
            &format!("--- {}", path.display()),
            &format!("+++ {} (formatted)", path.display()),
        )
        .to_string()
}
