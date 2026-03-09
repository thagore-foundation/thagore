//! Release target metadata shared by the CLI and packaging tooling.

use serde::Deserialize;

/// Release metadata describing one target triple.
#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
pub struct ReleaseTarget {
    /// Rust target triple.
    pub target: String,
    /// Support lane for this target.
    pub tier: String,
    /// Operating system family.
    pub os: String,
    /// CPU architecture.
    pub arch: String,
    /// Runtime ABI family.
    pub abi: String,
    /// Archive format emitted for this target.
    pub archive: String,
    /// Whether GitHub-hosted automation is expected to build it directly.
    pub automated: bool,
}

/// Returns the full supported release target table embedded at build time.
pub fn release_targets() -> &'static [ReleaseTarget] {
    static TARGETS: std::sync::OnceLock<Vec<ReleaseTarget>> = std::sync::OnceLock::new();
    TARGETS.get_or_init(|| {
        serde_json::from_str(include_str!("../../../tooling/packaging/targets.json"))
            .expect("embedded target metadata must be valid JSON")
    })
}

/// Returns the target triples in release-manifest order.
pub fn target_triples() -> Vec<String> {
    release_targets()
        .iter()
        .map(|target| target.target.clone())
        .collect()
}

#[cfg(test)]
mod tests {
    use super::release_targets;

    #[test]
    fn embedded_target_matrix_matches_release_plan() {
        let targets = release_targets();
        assert_eq!(targets.len(), 27);
        assert_eq!(
            targets
                .iter()
                .filter(|target| target.tier == "indev")
                .count(),
            8
        );
        assert_eq!(
            targets
                .iter()
                .filter(|target| target.tier == "extended")
                .count(),
            9
        );
        assert_eq!(
            targets
                .iter()
                .filter(|target| target.tier == "nightly")
                .count(),
            10
        );
    }
}
