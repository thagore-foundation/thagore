# Performance Report

- Commit: `2e4f5aec`
- Timestamp: `2026-03-01T17:42:38.792382+00:00`

## Compiler Metrics

| Metric | p50 (ms) | p95 (ms) | max (ms) |
|---|---:|---:|---:|
| thagc --version startup | 4.938 | 7.855 | 7.855 |
| thagc build tiny program | 105.802 | 114.921 | 114.921 |

## Runtime Comparison

| Runtime | available | p50 (ms) | p95 (ms) |
|---|---|---:|---:|
| thagore_native | yes | 1.222 | 1.404 |
| python | yes | 215.397 | 236.098 |
| go | no (go not found) | - | - |
| rust | yes | 0.750 | 0.974 |

