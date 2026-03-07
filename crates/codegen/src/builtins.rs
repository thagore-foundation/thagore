//! Builtin runtime symbol mappings.

/// Returns the runtime symbol that implements the builtin surface name.
#[must_use]
pub fn builtin_runtime_symbol(name: &str) -> Option<&'static str> {
    match name {
        "print" => Some("thagore_print"),
        "println" => Some("thagore_println"),
        "eprint" => Some("thagore_eprint"),
        "eprintln" => Some("thagore_eprintln"),
        "flush" => Some("thagore_flush"),
        _ => None,
    }
}
