# Benchmark Tooling

This folder contains repeatable benchmark lanes for `v1.3` performance lockdown.

## Fixtures

- `fixtures/tight_loop.tg`: compiler latency fixture used by CI budget checks.
- `fixtures/bench_sum.{tg,py,go,rs}`: cross-language runtime comparison workload.

## Commands

Collect per-commit Thagore metrics (startup + compile latency):

```bash
python tooling/bench/collect_metrics.py \
  --bin build/compiler/thagc \
  --source tooling/bench/fixtures/tight_loop.tg \
  --platform-key linux-x86_64 \
  --commit-sha "$(git rev-parse HEAD)" \
  --json-out perf-metrics-linux-x86_64.json
```

Run cross-language runtime comparison (Thagore vs Go vs Rust vs Python):

```bash
python tooling/bench/compare_languages.py \
  --thagc build/compiler/thagc \
  --runs 20 \
  --json-out docs/runbooks/perf-language-compare-local.json
```
