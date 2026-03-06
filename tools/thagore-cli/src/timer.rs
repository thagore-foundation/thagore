//! Wall-clock timing utilities for the Thagore CLI pipeline.

use std::fmt;
use std::io::{self, Write};
use std::time::{Duration, Instant};

/// Wall-clock timer started from `Instant::now()`.
#[derive(Debug, Clone)]
pub(crate) struct Timer {
    start: Instant,
}

impl Timer {
    /// Starts a new timer.
    #[must_use]
    pub(crate) fn start() -> Self {
        Self {
            start: Instant::now(),
        }
    }

    /// Returns the elapsed wall-clock duration.
    #[must_use]
    pub(crate) fn elapsed(&self) -> Duration {
        self.start.elapsed()
    }
}

/// Single stage timing entry.
#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct StageTiming {
    /// Human-readable stage label.
    pub label: &'static str,
    /// Measured wall-clock duration.
    pub duration: Duration,
}

/// Collection of pipeline timings, including total wall time.
#[derive(Debug, Clone)]
pub(crate) struct TimingReport {
    total: Timer,
    stages: Vec<StageTiming>,
}

impl TimingReport {
    /// Creates a new empty timing report and starts the total timer.
    #[must_use]
    pub(crate) fn new() -> Self {
        Self {
            total: Timer::start(),
            stages: Vec::new(),
        }
    }

    /// Records a completed pipeline stage.
    pub(crate) fn record(&mut self, label: &'static str, duration: Duration) {
        self.stages.push(StageTiming { label, duration });
    }

    /// Returns the total elapsed wall time.
    #[must_use]
    pub(crate) fn total_duration(&self) -> Duration {
        self.total.elapsed()
    }

    /// Writes the timing report in human-readable tabular form.
    pub(crate) fn write<W: Write>(&self, mut writer: W) -> io::Result<()> {
        for stage in &self.stages {
            writeln!(writer, "  {:<9} {}", stage.label, HumanDuration(stage.duration))?;
        }
        writeln!(writer, "  {:<9} {}", "total", HumanDuration(self.total_duration()))
    }
}

struct HumanDuration(Duration);

impl fmt::Display for HumanDuration {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let millis = self.0.as_secs_f64() * 1_000.0;
        write!(f, "{millis:.1}ms")
    }
}

#[cfg(test)]
mod tests {
    use super::{Timer, TimingReport};
    use std::thread;
    use std::time::Duration;

    #[test]
    fn timer_measures_elapsed_time() {
        let timer = Timer::start();
        thread::sleep(Duration::from_millis(5));
        assert!(timer.elapsed() >= Duration::from_millis(5));
    }

    #[test]
    fn timing_report_renders_stage_lines() {
        let mut report = TimingReport::new();
        report.record("lexer", Duration::from_millis(1));
        report.record("parser", Duration::from_millis(2));

        let mut rendered = Vec::new();
        report.write(&mut rendered).expect("render timings");
        let rendered = String::from_utf8(rendered).expect("utf8");

        assert!(rendered.contains("lexer"));
        assert!(rendered.contains("parser"));
        assert!(rendered.contains("total"));
    }
}
