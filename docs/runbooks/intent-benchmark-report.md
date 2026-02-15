# Intent Benchmark Report

Date: February 15, 2026  
Host: local Windows dev machine (single run sample)

## Scope

This report measures build-time overhead of the intent pipeline using:

- fixture: `tests/intent/fixtures/intent_demo_golden.tg`
- runner: `scripts/benchmark_intent.py`
- runs per mode: 5
- output mode: `--emit-llvm` (to isolate compiler path)

## Command

```bash
py -3 scripts/benchmark_intent.py \
  --cli runtime/build/Release/thagore_runtime_cli.exe \
  --runs 5 \
  --json-out docs/runbooks/intent-benchmark-local.json
```

## Results

| Variant | Median (s) | Mean (s) | Min (s) | Max (s) | Overhead vs off |
|---|---:|---:|---:|---:|---:|
| `intent_off` | 0.016251 | 0.016026 | 0.007626 | 0.022919 | 0.00% |
| `intent_min` | 0.014842 | 0.013602 | 0.007408 | 0.016304 | -8.67% |
| `intent_max` | 0.014472 | 0.013793 | 0.009442 | 0.016912 | -10.95% |
| `intent_max_strict_lock` | 0.012792 | 0.013860 | 0.007638 | 0.024538 | -21.29% |

`lock_deterministic: true` (two consecutive `intent lock` outputs were byte-identical for the fixture).

## Interpretation

- Absolute times are very small, so variance/noise dominates this local sample.
- The key release gate is deterministic lock behavior, which passed.
- For stable trend tracking, run this benchmark in CI on fixed hardware and aggregate over larger sample sizes.

## Artifact

Raw data: `docs/runbooks/intent-benchmark-local.json`
