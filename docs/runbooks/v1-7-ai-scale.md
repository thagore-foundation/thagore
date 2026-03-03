# v1.7 AI & Scale Runbook

This runbook documents the delivered v1.7 lanes in the compiler/runtime stack.

## 1) Flow Runtime Execution

- `flow` declarations now lower into executable functions during middleend lowering.
- Each step supports:
  - retry attempts (`retry N`)
  - timeout guard (`timeout 200ms` / `timeout 1s`)
  - rollback via `undo` actions (reverse order on failure)

Example:

```tg
flow checkout:
  step reserve = reserve_inventory()
    undo release_inventory()
    retry 2
    idempotent
    timeout 500ms
```

## 2) Hot Reload

Use watch mode during development:

```bash
thagc run src/main.tg --watch
```

Optional controls:

```bash
thagc run src/main.tg --watch --watch-interval-ms=250 --watch-iterations=3
```

## 3) Tensor Runtime Surface

`lib/tensor.tg` now exposes:

- `add_i64`, `mul_i64`, `dot_i64`
- `scale_i64`, `relu_i64`, `argmax_i64`
- `cuda_axpy_i64` runtime CUDA-aware path (deterministic CPU fallback if CUDA is unavailable)

## 4) SQL Builder + Migration

`lib/sql.tg` provides runtime-backed SQL builder and migration helpers:

- `builder/select/from/where/order_by/limit/build/reset/close`
- `migrate_apply(db_handle, migration_name, sql)`

## 5) gRPC Transport Lane

`lib/grpc.tg` provides:

- `unary(endpoint, method, payload, timeout_ms)`
- `health(endpoint, timeout_ms)`

The runtime transport hook uses HTTP-compatible request lanes so it can run in the existing runtime stack.

## 6) Distributed Tracing Hooks

`lib/trace.tg` provides:

- `enable/disable/is_enabled`
- `span_begin/span_end`
- `event`

Tracing is routed through runtime task-trace infrastructure.

## 7) Model Serving Example

Reference example:

- `examples/v1_7_model_serving.tg`

It demonstrates flow orchestration + tensor scoring + trace spans + SQL query builder in one serving-style pipeline.
