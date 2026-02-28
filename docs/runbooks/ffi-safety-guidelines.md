# FFI Safety Guidelines (v1.0 Baseline)

This runbook documents safe usage for `extern func` C interop in Thagore.

## 1. ABI and type mapping

- Keep `extern func` signatures exactly aligned with the C declaration.
- Match integer widths (`i32`, `i64`) and pointer semantics (`ptr`) exactly.
- Do not assume platform-dependent C type aliases (`long`, `size_t`) map to one fixed width.

## 2. Ownership rules

- Define ownership per function boundary:
  - who allocates memory,
  - who frees memory,
  - and whether returned pointers are borrowed or owned.
- Wrap allocation/free pairs in small Thagore helper functions to avoid leaks.

## 3. Pointer lifetime and mutation

- Never pass pointers to stack-allocated or temporary values beyond their lifetime.
- Avoid mutating memory through aliasing pointers unless the C API explicitly allows it.
- Validate null pointers before dereference in C-side runtime wrappers.

## 4. Threading and concurrency

- Treat non-thread-safe C libraries as single-threaded unless docs say otherwise.
- Do not share mutable FFI state across tasks without synchronization.
- Prefer immutable data handoff or dedicated worker scopes around unsafe C calls.

## 5. Build/link discipline

- Use explicit linker flags in `thagc`:
  - `--link-lib=<name>`
  - `--link-dir=<path>`
  - `--link-arg=<arg>`
- Keep platform-specific link flags in build scripts, not scattered in source files.

## 6. Error handling

- Map C error returns (`0/-1/null`) into Thagore `Result` or status codes early.
- Do not ignore partial failure states from FFI calls.
- Add runtime checks for boundary conditions (length, null, range).

## 7. Minimum verification before merge

- Build on target platform with all required `--link-*` flags.
- Run at least one positive and one negative integration test for each new FFI binding.
- Confirm diagnostics remain actionable on link failure (missing symbols/libs).
