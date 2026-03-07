//! Formatter configuration loading and preset handling.

use std::fs;
use std::path::{Path, PathBuf};

use clap::ValueEnum;
use serde::Deserialize;

/// Supported formatter style presets.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize, ValueEnum)]
#[serde(rename_all = "lowercase")]
pub enum StylePreset {
    /// One canonical style with import sorting and normalized spacing.
    Strict,
    /// Looser style that preserves import order and declaration spacing where possible.
    Relaxed,
}

/// Materialized formatter configuration after preset + file overrides.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FmtConfig {
    /// Active style preset.
    pub style: StylePreset,
    /// Preferred maximum line length. This is soft, not a hard wrap boundary.
    pub max_line_length: usize,
    /// Number of spaces used for one indentation level.
    pub indent_size: usize,
    /// Sort imports alphabetically and group them by source.
    pub sort_imports: bool,
    /// Required blank lines between top-level declarations in strict mode.
    pub blank_lines_between_decls: usize,
    /// Emit a trailing newline at the end of every file.
    pub trailing_newline: bool,
    /// Preserve original declaration spacing when safe.
    pub preserve_decl_blank_lines: bool,
}

impl Default for FmtConfig {
    fn default() -> Self {
        Self::strict()
    }
}

impl FmtConfig {
    /// Returns the canonical strict preset.
    #[must_use]
    pub const fn strict() -> Self {
        Self {
            style: StylePreset::Strict,
            max_line_length: 100,
            indent_size: 2,
            sort_imports: true,
            blank_lines_between_decls: 1,
            trailing_newline: true,
            preserve_decl_blank_lines: false,
        }
    }

    /// Returns the relaxed preset.
    #[must_use]
    pub const fn relaxed() -> Self {
        Self {
            style: StylePreset::Relaxed,
            max_line_length: 120,
            indent_size: 2,
            sort_imports: false,
            blank_lines_between_decls: 1,
            trailing_newline: true,
            preserve_decl_blank_lines: true,
        }
    }

    /// Builds a config from the requested preset.
    #[must_use]
    pub const fn from_preset(style: StylePreset) -> Self {
        match style {
            StylePreset::Strict => Self::strict(),
            StylePreset::Relaxed => Self::relaxed(),
        }
    }

    /// Loads the effective config for a target file or project.
    pub fn load(
        explicit_config: Option<&Path>,
        style_override: Option<StylePreset>,
        target: Option<&Path>,
    ) -> Result<Self, String> {
        let mut config = Self::from_preset(style_override.unwrap_or(StylePreset::Strict));
        if let Some(path) = explicit_config {
            let raw = RawFmtConfig::from_file(path)?;
            config.apply_raw(raw);
            return Ok(config);
        }

        if let Some(target) = target {
            if let Some(path) = find_config_path(target) {
                let raw = RawFmtConfig::from_file(&path)?;
                if raw.style.is_none() {
                    config = Self::from_preset(style_override.unwrap_or(config.style));
                }
                config.apply_raw(raw);
            }
        }

        if let Some(style) = style_override {
            config = Self::from_preset(style);
            if let Some(target) = target {
                if let Some(path) = find_config_path(target) {
                    config.apply_raw(RawFmtConfig::from_file(&path)?);
                }
            }
        }

        Ok(config)
    }

    fn apply_raw(&mut self, raw: RawFmtConfig) {
        if let Some(style) = raw.style {
            *self = Self::from_preset(style);
        }
        if let Some(max_line_length) = raw.max_line_length {
            self.max_line_length = max_line_length;
        }
        if let Some(indent_size) = raw.indent_size {
            self.indent_size = indent_size;
        }
        if let Some(sort_imports) = raw.sort_imports {
            self.sort_imports = sort_imports;
        }
        if let Some(blank_lines_between_decls) = raw.blank_lines_between_decls {
            self.blank_lines_between_decls = blank_lines_between_decls;
        }
        if let Some(trailing_newline) = raw.trailing_newline {
            self.trailing_newline = trailing_newline;
        }
        self.preserve_decl_blank_lines = matches!(self.style, StylePreset::Relaxed);
    }
}

#[derive(Debug, Default, Deserialize)]
struct RawFmtConfig {
    style: Option<StylePreset>,
    max_line_length: Option<usize>,
    indent_size: Option<usize>,
    sort_imports: Option<bool>,
    blank_lines_between_decls: Option<usize>,
    trailing_newline: Option<bool>,
}

impl RawFmtConfig {
    fn from_file(path: &Path) -> Result<Self, String> {
        let source = fs::read_to_string(path)
            .map_err(|error| format!("failed to read {}: {error}", path.display()))?;
        toml::from_str(&source)
            .map_err(|error| format!("failed to parse {}: {error}", path.display()))
    }
}

/// Walks upwards from `target` to find `.thagore-fmt.toml`.
#[must_use]
pub fn find_config_path(target: &Path) -> Option<PathBuf> {
    let mut current = if target.is_dir() {
        target.to_path_buf()
    } else {
        target.parent()?.to_path_buf()
    };

    loop {
        let candidate = current.join(".thagore-fmt.toml");
        if candidate.is_file() {
            return Some(candidate);
        }
        if !current.pop() {
            return None;
        }
    }
}

/// Walks upwards from `target` to find the nearest `drago.toml` project root.
#[must_use]
pub fn find_project_root(target: &Path) -> Option<PathBuf> {
    let mut current = if target.is_dir() {
        target.to_path_buf()
    } else {
        target.parent()?.to_path_buf()
    };

    loop {
        if current.join("drago.toml").is_file() {
            return Some(current);
        }
        if !current.pop() {
            return None;
        }
    }
}
