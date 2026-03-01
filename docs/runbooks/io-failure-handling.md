# IO Failure Handling Runbook

This runbook covers HTTP/WebSocket/DB failure handling for the v1.2 IO Stack GA lane.

## 1. Retry/Backoff Surface

Runtime APIs:

- `thag_http_get_retry(url, timeout_ms, retries, backoff_ms)`
- `thag_http_post_retry(url, payload, timeout_ms, retries, backoff_ms)`
- `thag_ws_connect_retry(endpoint, timeout_ms, retries, backoff_ms)`
- `thag_db_connect_retry(dsn, retries, backoff_ms)`
- `thag_db_query_retry(handle, query, retries, backoff_ms)`

Behavior:

- Retries are capped (`retries <= 8`).
- Backoff starts at `backoff_ms` and doubles each attempt (cap `2000ms`).
- Cancellation (`thag_task_is_cancelled`) short-circuits with `-2`.

## 2. HTTP Failure Model

- Transport/protocol failure maps to synthetic status `599`.
- Retry wrapper retries retryable statuses (`0`, `408`, `425`, `429`, and `>=500`).
- Caller should treat persistent `599` as upstream/network outage.

## 3. WebSocket Failure Model

- `ws://local` and `ws://localhost` may use deterministic offline fallback for local smoke lanes.
- Non-local endpoints must complete TCP + handshake; otherwise connect fails (`0`) and can be retried.

## 4. Database Failure Model

- Supported DSN forms:
  - `memory://`
  - `sqlite://...`
  - plain local file path
- Unsupported schemes fail fast (`0`) and can be retried by `thag_db_connect_retry`.

## 5. Operational Checklist

1. Confirm timeout is non-zero for every network call.
2. Use retry wrappers for non-idempotent sensitive paths only when caller-level idempotency is guaranteed.
3. Log final status/result after retries are exhausted.
4. Propagate cancellation outward instead of swallowing `-2`.

## 6. Test Commands

- Runtime IO surface + behavior:
  - `THAGC_BIN=build-llvm21/compiler/thagc python3 -m unittest tests.integration.test_runtime_api_surface tests.integration.test_runtime_behavior_native`
- Full IO/regression lane:
  - `THAGC_BIN=build-llvm21/compiler/thagc python3 -m unittest tests.integration.test_runtime_behavior_native tests.integration.test_concurrency_regression_native tests.integration.test_structured_concurrency_beta`
