# Concurrency Debugging Runbook

This runbook covers task-scope leaks, cancellation regressions, timeout behavior, and deadlock diagnostics in the Thagore runtime.

## 1. Quick Checks

- Build runtime and compiler:
  - `cmake -S . -B build-llvm21 -G Ninja -DLLVM_DIR=/usr/lib/llvm-21/lib/cmake/llvm`
  - `cmake --build build-llvm21 -j"$(nproc)"`
- Run core concurrency suites:
  - `THAGC_BIN=build-llvm21/compiler/thagc python3 -m unittest tests.integration.test_concurrency_regression_native tests.integration.test_structured_concurrency_beta tests.soak.test_compiler_stress`
- Run long soak lane (1h+ contract):
  - `THAG_SOAK_LONG_SECONDS=3600 python3 -m unittest tests.soak.test_concurrency_long_running`

## 2. Task Leaks (child outlives parent)

Symptoms:
- Parent scope exits but background work keeps mutating state.
- Non-deterministic failures in repeated native soak runs.

Checks:
- Confirm all child work is spawned via `thag_task_scope_spawn`.
- Confirm parent calls `thag_task_scope_wait` before destroy.
- Verify nested scopes are created while a parent scope is active.

Expected behavior:
- Child scopes are attached to the current scope.
- `thag_task_scope_wait` drains descendants.

## 3. Cancellation Failures

Symptoms:
- `thag_task_scope_cancel` called but workers continue forever.
- Timeouts trigger but return success.

Checks:
- Worker code must call `thag_task_is_cancelled()` at yield points.
- IO code paths must check cancellation before and after blocking calls.
- For stress cases, prefer short periodic sleeps/yields over long blocking loops.

Expected behavior:
- Cancel propagates recursively to nested scopes.
- Wait returns failure (`0`) when scope was cancelled.

## 4. Timeout Regressions

Symptoms:
- Scope timeout set but children continue without cancellation.
- Nested scopes ignore parent timeout.

Checks:
- Use `thag_task_scope_set_timeout(scope, ms)` on the parent scope.
- Verify nested scopes are created after timeout is set, or parent re-propagates timeout.
- Add assertions on both `thag_task_scope_wait(scope)` and `thag_task_scope_cancelled(scope)`.

Expected behavior:
- Deadline applies to the scope and descendants.
- On timeout, scope is cancelled and wait returns `0`.

## 5. Deadlock Diagnostics

Symptoms:
- `thag_task_scope_wait` stalls with no progress.
- No tasks are completing while no timers/ready tasks remain.

Checks:
- Capture stderr from native test binaries.
- Look for runtime message:
  - `deadlock detected: task A waiting on task B, task B waiting on task A [scope#<id>]`
- Inspect auto-dumped task tree that follows the deadlock line.
- Verify waiting tasks periodically check cancellation so runtime can unwind after detection.

## 6. Task Tree Tracing Hooks

Runtime API:
- `thag_task_trace_set_enabled(int enabled)`
- `thag_task_trace_enabled()`
- `thag_task_trace_set_hook(thag_task_trace_hook_t hook, void* user_data)`
- `thag_task_scope_dump_tree(const thag_task_scope_t* scope)`

Usage patterns:
- Enable stderr tracing via `THAG_TRACE_TASK_TREE=1`.
- Attach a custom hook to aggregate/scrape traces during integration tests.
- Call `thag_task_scope_dump_tree(scope)` before destroy for manual inspection.

P0/P1 gate source of truth:
- `contracts/concurrency/p0_p1_registry.json` must not contain open `P0`/`P1` issues.

## 7. Scheduler Tuning (for repro or stress)

Environment variables:
- `THAG_SCHED_QUEUE_LIMIT`: max queue size before spawn backpressure blocks.
- `THAG_SCHED_STARVATION_MS`: wait threshold for starvation priority boost.

Use small limits in tests to force backpressure behavior:
- `THAG_SCHED_QUEUE_LIMIT=64`

## 8. Common Failure Patterns

- Missing cancellation checkpoints inside worker loops.
- Blocking IO without pre/post cancel check.
- Scope destroyed without waiting children.
- Test flakiness due to too-short time budgets on loaded CI hosts.
