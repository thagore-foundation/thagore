# Structured Concurrency for Beginners

Structured concurrency in thagore means each spawned task belongs to a scope, and
that scope owns cancellation, deadlines, and shutdown.

## Core Rules

1. Create a scope (`thag_task_scope_create` or `thag_nursery_create`).
2. Spawn work only through that scope (`thag_task_scope_spawn`).
3. Wait for completion from the same owner (`thag_task_scope_wait`).
4. Destroy the scope when done (`thag_task_scope_destroy`).

If a parent scope is cancelled or times out, child scopes inherit the stop signal.

## Minimal Lifecycle

```c
thag_task_scope_t* scope = thag_task_scope_create();
if (scope == NULL) { return 1; }

if (!thag_task_scope_spawn(scope, worker_fn, user_data)) {
  thag_task_scope_destroy(scope);
  return 2;
}

int ok = thag_task_scope_wait(scope);
thag_task_scope_destroy(scope);
return ok ? 0 : 3;
```

## Cancellation and Timeout

- Manual cancellation: call `thag_task_scope_cancel(scope)`.
- Deadline-based cancellation: call `thag_task_scope_set_timeout_ms(scope, timeout_ms)`.
- Cooperative checks inside tasks: call `thag_task_is_cancelled()` and stop quickly.

## Practical Tips

- Keep spawned task functions short and side-effect aware.
- Avoid detached/background work that outlives the owning scope.
- Treat `thag_task_scope_wait` as the synchronization boundary before resource cleanup.
