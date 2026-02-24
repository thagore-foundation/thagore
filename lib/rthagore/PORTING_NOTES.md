# rthagore stdlib/drago import

This folder contains a staged port of `rthagore/stdlib` into the C++ thagore
runtime/compiler tree.

## Current focus: std core

Core module graph currently targeted by the compiler-first port:

- `std/lib.tg`
- `std/prelude.tg`
- `std/option.tg`
- `std/result.tg`
- `std/platform.tg`
- `std/gc.tg`
- `std/sys_platform.tg`
- `std/env.tg`
- `std/fs.tg`
- `std/time.tg`
- `std/sys.tg`
- `std/process.tg` (compat shim used by platform/sys shims)
- `std/io.tg`
- `std/sys_linux.tg`
- `std/sys_windows.tg`
- `std/sys_bsd.tg`
- `std/sys_macos.tg`
- `std/sys_mach.tg`

Broader `rthagore/*` modules are now wired as compatibility surfaces so they can
be imported and compiled by the current frontend/runtime subset.

## Compiler/runtime support expected by std core

- Frontend syntax:
  - `use ... as ...`
  - `throw`
  - `if cond:` / `while cond:` without mandatory parentheses
  - `and` / `or` / `not`
  - `true` / `false` / `null`
  - untyped params + default args (missing args lowered to zero/null)
  - expression-body function syntax (`func f(...) = expr`)
- Runtime bridge ABI:
  - env: `__env_get`, `__env_set`, `__env_args`, `__env_cwd`
  - fs: `__fs_read_text`, `__fs_write_text`, `__fs_exists`, `__fs_mkdir`,
    `__fs_list_dir`, `__fs_open_binary`, `__fs_write_bytes`,
    `__fs_read_bytes`, `__fs_seek`, `__fs_close`
  - time: `__time_now_ms`, `__time_sleep`
  - char: `__string_codepoint`, `__string_from_codepoint`
  - throw: `__thg_throw`

Additional compat runtime helpers now used by non-core modules:

- string: `__thg_str_contains`, `__thg_str_starts_with`, `__thg_str_ends_with`,
  `__thg_str_trim`, `__thg_str_replace`, `__thg_str_lower`, `__thg_str_upper`,
  `__thg_str_compare`
- path: `__thg_path_strip_trailing`, `__thg_path_strip_leading`,
  `__thg_path_basename`, `__thg_path_dirname`, `__thg_path_ext`,
  `__thg_path_join2`
- formatter: `__thg_fmt_trim_trailing`

Compat policy updates applied to `std/*` compatibility wrappers:

- non-core wrappers now prefer explicit function signatures (typed params/returns)
- String-returning shim APIs normalize missing/error paths to `""` (instead of
  propagating `null`) for more stable call contracts

## Memory contract

Runtime-created strings from bridge helpers are registered as managed buffers
and released through the unified release path:

- allocation path: managed registry (`registerManagedBuffer`)
- free path: `__thg_str_free` -> `__thg_release`
- safe no-op behavior for null/unknown pointers (double-free resistant)

## Known limitations

- Native `class`, `for`, `enum`, `match` parity is not complete.
- `option/result/gc/sys_platform` are compatibility shims, not full parity.
- Many non-core `std/*`, `drago/*`, `tools/*`, `scale/*`, and `sysroot/*`
  modules are compatibility-first implementations (mixed: thin wrappers,
  simplified behavior, and stubs depending on module).
- This phase does not cover full `drago/*` and `tools/*` execution parity.
- `drago/*` now supports basic cache/package/lock/resolve/test-runner flows in
  string-based compat mode (no full manifest graph semantics yet).

## Verification checklist (run when toolchain is ready)

1. Configure and build:
   - `cmake -S . -B build`
   - `cmake --build build`
2. Run test suite:
   - `ctest --test-dir build --output-on-failure`
3. Confirm `thag_tests` covers:
   - parser/lexer support for new std-core syntax
   - semantic checks for unknown/default/throw/logical ops
   - std core module graph smoke via driver import chain
   - libsystem shim smoke (`sys_platform` + `io`) via driver import chain
   - drago/tools compat smoke via driver import chain
   - recursive `lib/rthagore/**/*.tg` import/compile surface smoke
   - runtime bridge string alloc/free loop contracts

## Next milestone

Implement native `enum + match` lowering so `option/result` can move off shims.
