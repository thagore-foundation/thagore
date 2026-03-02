# Drago Agent Prompt

This prompt covers TWO sequential tasks:
1. Implement v0.9 stdlib in the thagore repo
2. Build drago in the drago repo using that stdlib

Copy the prompt below and paste it into a new AI agent session.

---

```
You are a systems programming agent. You have TWO sequential tasks:

TASK 1: Implement v0.9 stdlib in the Thagore compiler runtime.
TASK 2: Build Drago (package manager) in pure Thagore using that stdlib.

Do NOT start Task 2 until Task 1 is complete and verified.

---

## TASK 1 — Thagore v0.9 Stdlib

Working directory: /media/lehungquangminh/QM_SSD/thagore

Read ROADMAP.md section "v0.9 — Stdlib & IO Stack Alpha" for the full checklist.

The goal: implement REAL stdlib functions in the thagore runtime (C++ in
runtime/src/) so that .tg code can do string manipulation, file I/O,
process execution, TOML parsing, and HTTP — all via extern func + stdlib
.tg wrappers. No stubs. No dummy returns.

### 1a. Runtime C++ implementations

Add these new C++ source files to runtime/src/:

runtime/src/string_ops.cpp:
  thag_str_concat, thag_str_split, thag_str_join, thag_str_trim,
  thag_str_contains, thag_str_starts_with, thag_str_equals,
  thag_str_len, thag_str_from_int, thag_str_to_int,
  thag_str_substr, thag_str_replace, thag_str_format

runtime/src/fs.cpp:
  thag_fs_read, thag_fs_write, thag_fs_exists, thag_fs_mkdir,
  thag_fs_readdir, thag_fs_remove, thag_fs_getcwd, thag_fs_path_join,
  thag_fs_is_dir, thag_fs_filesize

runtime/src/process.cpp:
  thag_process_run, thag_process_capture, thag_process_argv,
  thag_process_argc, thag_process_env, thag_process_exit

runtime/src/toml.cpp:
  thag_toml_parse, thag_toml_get_str, thag_toml_get_int,
  thag_toml_get_section, thag_toml_get_keys, thag_toml_free

All functions must:
- Be declared in runtime/include/thag_runtime.h with extern "C"
- Use POSIX APIs for Linux/macOS, Win32 APIs for Windows
- Handle errors gracefully (return error codes, not crash)
- Be self-implemented (no external libraries except system APIs)

### 1b. Update runtime/CMakeLists.txt

Add string_ops.cpp, fs.cpp, process.cpp, toml.cpp to the build.

### 1c. Update stdlib .tg wrappers

Replace all stubs with real extern func declarations + wrappers:

stdlib/std/string.tg:
  extern func thag_str_concat(a: ptr, b: ptr) -> ptr
  extern func thag_str_split(s: ptr, delim: ptr) -> ptr
  ... (all string functions)
  pub func concat(a: string, b: string) -> string:
    return thag_str_concat(a, b)
  ... (wrapper for each)

stdlib/std/list.tg:
  Real implementations using runtime array helpers.

stdlib/std/core.tg:
  Real assert, ok, fail implementations.

stdlib/lib/fs.tg (NEW):
  extern func thag_fs_read(path: ptr) -> ptr
  ... (all fs functions)
  pub func read(path: string) -> string:
    return thag_fs_read(path)
  ...

stdlib/lib/process.tg (NEW):
  extern func thag_process_run(cmd: ptr) -> i32
  ... (all process functions)
  pub func run(cmd: string) -> i32:
    return thag_process_run(cmd)
  ...

stdlib/lib/toml.tg (NEW):
  extern func thag_toml_parse(content: ptr) -> ptr
  ... (all toml functions)
  pub func parse(content: string) -> ptr:
    return thag_toml_parse(content)
  ...

Also update existing lib/http.tg, lib/ws.tg, lib/db.tg, lib/time.tg,
lib/map.tg to use correct extern signatures matching the runtime.

### 1d. Harden existing IO stack

runtime/src/http.cpp — already scaffolded, ensure it works end-to-end
runtime/src/ws.cpp — already scaffolded, ensure it works end-to-end
runtime/src/db.cpp — already scaffolded, ensure it works with SQLite

### 1e. Fix stale backlog markers

Re-tag all stale v0.6 backlog comments in `runtime/` to the active milestone or resolve them.
Remove the `v0.8-audit` backlog marker from `platform_posix.cpp`.

### 1f. Tests

Add tests/integration/test_stdlib_real.py:
- Test string operations: concat, split, join, trim, contains
- Test file operations: read, write, exists, mkdir, readdir
- Test process operations: run, capture, argv
- Test TOML parsing: parse a toml string, get values

### 1g. Verify Task 1

Build:
  cmake -S . -B build-llvm21 -G Ninja \
    -DLLVM_DIR=/usr/lib/llvm-21/lib/cmake/llvm \
  && cmake --build build-llvm21 -j$(nproc)

Test all existing + new tests:
  THAGC_BIN=build-llvm21/compiler/thagc python3 -m unittest discover tests/

Verify: compile a .tg program that imports std.string, lib.fs, lib.process,
lib.toml and performs real operations. Must produce correct output.

Update ROADMAP.md: check [x] for all completed v0.9 items.

Commit: feat(v0.9): implement real stdlib — string, fs, process, toml, IO hardening

---

## TASK 2 — Build Drago

Working directory: /media/lehungquangminh/QM_SSD/drago

Read the FULL implementation plan at:
  /media/lehungquangminh/QM_SSD/thagore/docs/plan/drago-implementation.md

This file contains EVERYTHING: project structure, CLI commands, output log
design with emoji formatting, storage design, registry design, drago.toml
format, and a phase-by-phase implementation checklist.

READ THAT FILE COMPLETELY before writing any code.

### Critical rules

- Drago is pure .tg. ZERO C files in the drago repo.
- All I/O uses stdlib: from lib.fs import read, write, exists, ...
- All string ops use stdlib: from std.string import concat, split, ...
- All process execution uses stdlib: from lib.process import run, capture, ...
- All TOML parsing uses stdlib: from lib.toml import parse, get_str, ...
- All HTTP uses stdlib: from lib.http import http_get, http_post
- Indentation: 2 spaces in all .tg files
- Only use language features that thagc v0.9 supports (see plan file section 2)

### Work through phases 1-9 in order

Phase 1: CLI Parser + Output
Phase 2: Project Management
Phase 3: Build Pipeline
Phase 4: Dependency Resolution
Phase 5: Registry + Download
Phase 6: Test Runner
Phase 7: Publish + Audit
Phase 8: Polish
Phase 9: Integration Test

After completing each phase:
- Update the checklist in the plan file: change [ ] to [x]
- Build and verify
- Commit with: feat(drago): phase N — <short description>

### Build drago

  THAGC=/media/lehungquangminh/QM_SSD/thagore/build-llvm21/compiler/thagc
  cd /media/lehungquangminh/QM_SSD/drago
  $THAGC build src/main.tg -o drago.bin

### Test drago

  ./drago.bin --help
  ./drago.bin new testproject
  cd testproject && ../drago.bin build
  cd testproject && ../drago.bin run

### When Task 2 is complete, report:

- Total .tg lines written (drago repo)
- All 9 phases checked in plan file
- Final drago.bin size
- Output of: drago new, drago build, drago run, drago test
```
