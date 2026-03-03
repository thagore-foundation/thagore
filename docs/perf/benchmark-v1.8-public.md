# Performance Report

- Commit: `8f82706e`
- Timestamp: `2026-03-03T17:55:52.052771+00:00`

## Compiler Metrics

| Metric | p50 (ms) | p95 (ms) | max (ms) |
|---|---:|---:|---:|
| thagc --version startup | 14.872 | 27.991 | 30.106 |
| thagc build tiny program | 342.751 | 440.980 | 440.980 |

## Runtime Comparison

| Runtime | available | p50 (ms) | p95 (ms) |
|---|---|---:|---:|
| thagore_native | yes | 6.082 | 14.952 |
| python | yes | 539.776 | 638.620 |
| go | yes | 12.152 | 15.847 |
| rust | yes | 4.058 | 6.874 |
