//! Text and JSON benchmark reporting.

use crate::runner::{BenchmarkReport, Language};
use crate::stats::geomean;

/// Renders the benchmark report as a human-readable table.
#[must_use]
pub fn render_text(report: &BenchmarkReport, primary: Language) -> String {
    let mut out = String::new();
    out.push_str("workload          thagore    ");
    out.push_str(primary.as_str());
    out.push_str("        ratio\n");
    out.push_str("─────────────────────────────────────────────\n");

    let mut ratios = Vec::new();
    for workload in &report.workloads {
        let Some(thagore) = workload
            .languages
            .iter()
            .find(|entry| entry.language == Language::Thagore)
        else {
            continue;
        };
        let Some(other) = workload
            .languages
            .iter()
            .find(|entry| entry.language == primary)
        else {
            continue;
        };
        let ratio = other.runtime_ms.mean / thagore.runtime_ms.mean;
        ratios.push(ratio);
        out.push_str(&format!(
            "{:<16} {:>8.1}ms {:>8.1}ms {:>7.2}x {}\n",
            workload.workload,
            thagore.runtime_ms.mean,
            other.runtime_ms.mean,
            ratio,
            if ratio > 1.0 { "✅" } else { "❌" }
        ));
    }
    out.push_str("─────────────────────────────────────────────\n");
    out.push_str(&format!(
        "{:<16} {:>25.2}x {}\n",
        "geomean",
        geomean(&ratios),
        if geomean(&ratios) > 1.0 { "✅" } else { "❌" }
    ));

    if !report.unsupported.is_empty() {
        out.push_str("\nunsupported workloads:\n");
        for workload in &report.unsupported {
            out.push_str("  ");
            out.push_str(workload);
            out.push('\n');
        }
    }

    for workload in &report.workloads {
        if let Some(note) = &workload.note {
            out.push('\n');
            out.push_str(&workload.workload);
            out.push_str(": ");
            out.push_str(note);
            out.push('\n');
        }
    }

    out
}
