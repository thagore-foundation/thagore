# v1.7 Model Serving Benchmark

- Timestamp: `2026-03-03T16:23:11.196862+00:00`
- Iterations per command sample: `12`
- Workload loop count: `4000`
- Speedup vs Flask (p50): `48.80x`

| Runtime | p50 (ms) | p95 (ms) | max (ms) |
|---|---:|---:|---:|
| thagore_native | 5.344 | 7.230 | 7.230 |
| python_flask | 260.807 | 312.741 | 312.741 |
