//! Library entry points for the official Thagore formatter.

pub mod config;
pub mod diff;
pub mod formatter;
pub mod rules;

pub use crate::config::{find_config_path, find_project_root, FmtConfig, StylePreset};
pub use crate::diff::{needs_formatting, unified_diff};
pub use crate::formatter::{format_source, FormatFileResult};
