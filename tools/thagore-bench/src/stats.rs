//! Statistical helpers for Thagore benchmark reporting.

use serde::Serialize;

/// Summary statistics for a trimmed benchmark sample set.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct SummaryStats {
    /// Number of retained samples after trimming.
    pub samples: usize,
    /// Arithmetic mean.
    pub mean: f64,
    /// Median.
    pub median: f64,
    /// 95th percentile.
    pub p95: f64,
    /// 99th percentile.
    pub p99: f64,
    /// Population standard deviation.
    pub stddev: f64,
    /// Minimum retained sample.
    pub min: f64,
    /// Maximum retained sample.
    pub max: f64,
}

/// Computes summary statistics after discarding the lowest and highest sample.
#[must_use]
pub fn summarize(samples: &[f64]) -> SummaryStats {
    let mut sorted = samples.to_vec();
    sorted.sort_by(f64::total_cmp);

    let trimmed = if sorted.len() > 2 {
        &sorted[1..sorted.len() - 1]
    } else {
        &sorted[..]
    };

    let mean = trimmed.iter().sum::<f64>() / trimmed.len() as f64;
    let variance = trimmed
        .iter()
        .map(|sample| {
            let delta = *sample - mean;
            delta * delta
        })
        .sum::<f64>()
        / trimmed.len() as f64;

    SummaryStats {
        samples: trimmed.len(),
        mean,
        median: percentile(trimmed, 0.50),
        p95: percentile(trimmed, 0.95),
        p99: percentile(trimmed, 0.99),
        stddev: variance.sqrt(),
        min: *trimmed.first().unwrap_or(&0.0),
        max: *trimmed.last().unwrap_or(&0.0),
    }
}

/// Computes the geometric mean for positive ratios.
#[must_use]
pub fn geomean(values: &[f64]) -> f64 {
    let positive = values
        .iter()
        .copied()
        .filter(|value| *value > 0.0)
        .collect::<Vec<_>>();
    if positive.is_empty() {
        return 0.0;
    }
    let log_sum = positive.iter().map(|value| value.ln()).sum::<f64>();
    (log_sum / positive.len() as f64).exp()
}

fn percentile(sorted: &[f64], ratio: f64) -> f64 {
    if sorted.is_empty() {
        return 0.0;
    }
    let position = ((sorted.len() - 1) as f64 * ratio).round() as usize;
    sorted[position.min(sorted.len() - 1)]
}

#[cfg(test)]
mod tests {
    use super::{geomean, summarize};

    #[test]
    fn summarize_trims_extremes() {
        let stats = summarize(&[1.0, 2.0, 3.0, 100.0]);
        assert_eq!(stats.samples, 2);
        assert_eq!(stats.min, 2.0);
        assert_eq!(stats.max, 3.0);
        assert!((stats.mean - 2.5).abs() < f64::EPSILON);
    }

    #[test]
    fn geomean_handles_positive_ratios() {
        let value = geomean(&[2.0, 8.0]);
        assert!((value - 4.0).abs() < 1e-9);
    }
}
