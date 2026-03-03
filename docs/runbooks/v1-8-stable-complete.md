# v1.8 Stable & Complete Runbook

This runbook documents the in-repo v1.8 completion gates that are enforced by
tests and CI automation.

## 1) Generic Type System Gate

v1.8 generic lane covers built-in generic families with nested validation:

- `Option<T>`
- `Result<T, E>`
- `List<T>`
- `Rc<T>`
- `Arc<T>`

Validation guarantees:

- generic arity is enforced (`Result<T, E>` requires exactly 2 args)
- nested generic arguments are recursively validated
- malformed generic syntax is rejected with diagnostics

Local gate:

```bash
THAGC_BIN=build-llvm21/compiler/thagc python3 -m unittest \
  tests.integration.test_language_feature_completion \
  tests.integration.test_memory_model_send_sync
```

## 2) Public Backend Benchmark Gate (Go Competitive)

Generate public metrics and report:

```bash
PATH="$PWD/.cache/go/bin:$PATH" python3 tooling/bench/run_benchmarks.py \
  --thagc build-llvm21/compiler/thagc \
  --out-json docs/perf/benchmark-v1.8-public.json \
  --out-markdown docs/perf/benchmark-v1.8-public.md \
  --startup-iterations 20 \
  --build-iterations 8 \
  --run-iterations 12 \
  --require-runtimes go,rust,python
```

Enforce v1.8 benchmark contract:

```bash
python3 tooling/policy/check_public_backend_gate.py \
  --metrics docs/perf/benchmark-v1.8-public.json \
  --contract contracts/perf/backend_compete_v1_8.json
```

## 3) Desktop App Cross-Platform Build Gate

Workflow:

- `.github/workflows/desktop-app-matrix.yml`

It builds the desktop drawing example on:

- Linux
- macOS
- Windows

Example source:

- `examples/v1_6_drawing_app.tg`

## 4) Parity Guard

Parity checks for v1.8 completion artifacts:

```bash
python3 -m unittest tests.parity.test_v18_completion_contract
```
