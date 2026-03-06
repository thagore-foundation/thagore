# Thagore Roadmap — v0.1 → v1.8 Stable

Current effective milestone by code audit (March 1, 2026): `v1.0` (Deploy Baseline, released).
Status note: `v0.6` → `v1.0` gates pass on the active release lane; `v1.0.0` release train ships multi-platform Thagore core archives + multi-platform Drago bundles with installer/updater integration.
Active implementation policy: finish one milestone completely before starting the next.

> **Mission:** Thagore trao quyền sáng tạo cho tất cả mọi người — từ học sinh lớp 10 đến AI engineer —
> để bất kỳ ai cũng có thể build thứ họ tưởng tượng mà không bị ngôn ngữ cản trở.
>
> *"Stop fighting your language. Start building."*

---

## Trạng thái hiện tại (audit ngày February 27, 2026)

| Area | % Done | Ghi chú |
|------|--------|---------|
| Compiler Frontend (lexer/parser) | ~35% | Lexer + parser usable, typechecker vẫn là stub |
| Compiler Middleend (typed IR / lowering) | ~25% | Scaffolding có, thiếu implementation thật |
| Compiler Backend (LLVM emit) | ~35% | Chủ yếu single-function flow; multi-function còn thiếu |
| CLI Driver (10 command groups) | ~85% | 10 groups wired, nhưng bị giới hạn bởi compiler/runtime backend |
| Runtime (concurrency + IO) | ~25% | Có API prototype; scheduler/IO platform backend chưa production |
| Stdlib | ~30% | File structure đúng, implementation gần như rỗng |
| Tooling | ~75% | Policy, packaging, compare tools hiện có |
| CI / Release | ~55% | Linux/macOS tốt hơn, Windows build gate chưa ổn định |

**Overall: compiler + runtime gates through `v0.9` pass on Linux (import pipeline, language completeness, structured concurrency, memory model checks, stdlib/runtime IO alpha lane).**

Execution order (re-anchored to dependency reality):
1. Close `v0.2` gate (compiler foundation)
2. Complete `v0.3` (struct/type system)
3. Complete `v0.4` (module/import pipeline)
4. Complete `v0.5` (language completeness)
5. Resume and harden concurrency track (`v0.6+`)

---

## v0.2 — Compiler Foundation ✅ Completed (Linux Gate Passed)
> *"Hello World thật sự chạy được — không chỉ `func main()`"*

### Compiler
- [x] `func main` + print + arithmetic MVP chạy được
- [x] Float types (`f32`, `f64`) có trong backend `ValueType::F32/F64`
- [x] Multi-function codegen — user-defined functions được emit và gọi được
- [x] `bool` literal `true`/`false` là first-class value end-to-end
- [x] Binary arithmetic cho float — `+`, `-`, `*`, `/` với `f32`/`f64`
- [x] Typechecker pass thật sự — kiểm tra type mismatch, undefined variable
- [x] Error messages rõ ràng, có line/column number, không cryptic

### Stdlib
- [ ] `std/core.tg` — `print_int`, `print_float`, `print_bool` thật sự hoạt động
- [ ] `std/string.tg` — `len()`, `concat()`, `trim()` basic

### CI
- [ ] Windows build job trong `ci.yml`
- [ ] 3-OS matrix đầy đủ cho build jobs

### Gate
- [x] Chương trình có nhiều functions, float, bool compile và chạy đúng trên Linux (macOS/Windows gate moved to v1.4)

---

## v0.3 — Struct & Type System ✅ Completed
> *"Tao có thể định nghĩa kiểu dữ liệu của riêng mình"*

### Compiler
- [x] Struct definition codegen — field layout, alloca cho struct type
- [x] Field access (`obj.field`) trong LLVM emitter
- [x] `impl` block codegen — methods được emit như functions với implicit `self`
- [x] Method call syntax `obj.method()` lowering
- [x] Enum với payload — variant mang data được emit đúng
- [x] `TypeKind` mở rộng — struct type, enum type, function type

### Middleend
- [x] Typed IR implementation — `ast_to_typed_ir` pass thật sự (scaffolding exists)
- [x] `typed_ir_to_core_ir` lowering cho struct/impl/enum

### Gate
- [x] Struct với fields, methods, và enum với payload compile và chạy đúng

---

## v0.4 — Module & Import System ✅ Completed
> *"Tao có thể chia code ra nhiều file"*

### Language — Import Syntax
- [x] `import a.b.c` — resolve từ project root, dùng qua prefix `c.func()`
- [x] `from a.b.c import func` — import trực tiếp symbol
- [x] `import pkg_name` — resolve Drago package từ manifest
- [x] `from pkg_name import func` — import trực tiếp từ package
- [x] `import a.b.c as alias` — alias để tránh conflict tên
- [x] Conflict detection — compiler error khi hai import cùng tên cuối, yêu cầu alias
- [x] Import scope: top-level only, không hỗ trợ block-level import

### Build Pipeline
- [x] Multi-file compilation — locate + compile + link `.tg` modules
- [x] `thagore.toml` manifest — khai báo dependencies, version pinning
- [x] Module resolver — phân biệt package vs file path qua manifest
- [ ] Incremental compilation — chỉ recompile file thay đổi

### Drago Package Registry (MVP)
- [x] `thagc install <package>` hoạt động
- [x] Local cache tại `~/.thagore/packages/`
- [ ] Registry endpoint cơ bản
- [x] `thagore.toml` lock file

### Gate
- [x] Project nhiều file compile thành công, import package từ manifest/cache hoạt động

---

## v0.5 — Language Features Complete ✅ Released
> *"Ngôn ngữ đủ để viết chương trình thật"*

### Compiler — Language Features
- [x] Closures — capture, function-value type, emit đúng trong LLVM
- [x] `defer` stack — defer-stack mechanism trong backend, đúng execution order
- [x] Interpolated strings — `"Hello {name}!"` compile thành string concat
- [x] `Result<T, E>` / `Option<T>` — built-in sum types, không cần generic đầy đủ
- [x] `?` operator — early return cho Result/Option
- [x] Tuple type — `(i32, string)`, tuple destructuring
- [x] Array/slice literals — `[1, 2, 3]`, index access `arr[i]`
- [x] Loop labels + labeled `break`/`continue`
- [x] `pub` visibility enforcement — cross-module access control

### Type System
- [x] Generic types cơ bản — `List<T>`, `Option<T>`, `Result<T, E>`
- [x] Function types — `fn(i32) -> string`
- [x] User-defined type validation — struct field types, trait method signatures

### Gate
- [x] Có thể viết một CLI tool hoàn chỉnh bằng Thagore (sort, search, transform data)

---

## v0.6 — Concurrency Primitives (Alpha) ✅ Released
> *"Structured concurrency primitives đã pass gate stress deterministic."*

### Runtime
- [x] Task scope/nursery/cancel/timeout APIs
- [x] Async scheduler event loop thay thế thread-per-task
- [x] epoll/kqueue integration cho Linux/macOS
- [x] Platform layer production baseline (không còn stub APIs)

### Compiler
- [x] `Rc<T>` codegen — single-thread reference counting
- [x] `Arc<T>` codegen — atomic reference counting
- [x] `Send`/`Sync` auto-check cho task-boundary checks trong typechecker
- [x] Diagnostic `E_SEND_SYNC_004` với fix hint "use `Arc` instead of `Rc`"

### Tests
- [x] Child tasks cannot silently leak outside scope
- [x] Cancel propagation deterministic under stress
- [x] 20-iteration deterministic soak lane cho native concurrency

### Gate
- [x] Structured concurrency contracts pass — scope/cancel/timeout behavior deterministic

---

## v0.7 — Structured Concurrency by Default (Beta) ✅ Released
> *"Concurrent code an toàn như single-thread code"*

### Runtime
- [x] Timeout API và propagation semantics
- [x] Scheduler fairness — starvation protection
- [x] Backpressure mechanism
- [x] First deadlock/race regression suite

### Language
- [x] `async`/`await` syntax hoặc equivalent Thagore construct
- [x] Task cancellation propagation qua IO boundaries
- [x] Cancellation check trong HTTP/WS/DB calls

### Docs
- [x] Concurrency debugging playbook
- [x] Structured concurrency guide cho beginners

### Gate
- [x] Scope/cancel/timeout behavior deterministic dưới stress tests 20-iteration soak

---

## v0.8 — Memory Model MVP ✅ Released
> *"Compiler lo memory — mày lo logic"*

### Compiler
- [x] Automatic `Send`/`Sync` validation trong typechecker và middleend
- [x] Reject `Rc` across thread boundaries với actionable diagnostic
- [x] Suggest `Arc` khi `Rc` Send/Sync fail
- [x] Memory model compliance cases trong contracts

### Runtime
- [x] `platform_posix.cpp` implementation đầy đủ (không còn 4-line stub)
- [x] `platform_windows.cpp` implementation đầy đủ
- [x] `thag_runtime.cpp` — init, shutdown, memory tracking thật sự

### Gate
- [x] Invalid cross-thread `Rc` usage bị reject tại compile time với actionable diagnostics

---

## v0.9 — Stdlib & IO Stack Alpha
> *"Stdlib thật sự hoạt động — Drago có thể viết bằng pure Thagore"*

### Runtime — Stdlib Backend (C++ trong thagore runtime, gọi qua extern func)
- [x] `thag_str_concat(a, b)` → real string concatenation
- [x] `thag_str_split(s, delim)` → split string, trả ptr array
- [x] `thag_str_join(parts, sep)` → join array thành string
- [x] `thag_str_trim(s)` → trim whitespace
- [x] `thag_str_contains(s, sub)` → check substring
- [x] `thag_str_starts_with(s, prefix)` → check prefix
- [x] `thag_str_equals(a, b)` → compare strings
- [x] `thag_str_len(s)` → real string length
- [x] `thag_str_from_int(n)` → int to string
- [x] `thag_str_to_int(s)` → string to int
- [x] `thag_str_substr(s, start, len)` → substring
- [x] `thag_str_replace(s, old, new)` → replace all occurrences
- [x] `thag_str_format(fmt, args)` → basic format string

### Runtime — File System (C++ trong thagore runtime)
- [x] `thag_fs_read(path)` → read file to string
- [x] `thag_fs_write(path, content)` → write string to file
- [x] `thag_fs_exists(path)` → check file/dir exists
- [x] `thag_fs_mkdir(path)` → create directory (recursive)
- [x] `thag_fs_readdir(path)` → list directory entries
- [x] `thag_fs_remove(path)` → remove file/dir
- [x] `thag_fs_getcwd()` → current working directory
- [x] `thag_fs_path_join(a, b)` → join path segments
- [x] `thag_fs_is_dir(path)` → check if path is directory
- [x] `thag_fs_filesize(path)` → get file size in bytes

### Runtime — Process Execution (C++ trong thagore runtime)
- [x] `thag_process_run(cmd)` → run command, return exit code
- [x] `thag_process_capture(cmd)` → run command, capture stdout
- [x] `thag_process_argv(index)` → get argv[index]
- [x] `thag_process_argc()` → get argc
- [x] `thag_process_env(name)` → get environment variable
- [x] `thag_process_exit(code)` → exit with code

### Runtime — TOML Parser (C++ trong thagore runtime)
- [x] `thag_toml_parse(content)` → parse TOML string into key-value handle
- [x] `thag_toml_get_str(handle, key)` → get string value
- [x] `thag_toml_get_int(handle, key)` → get int value
- [x] `thag_toml_get_section(handle, section)` → get sub-section handle
- [x] `thag_toml_get_keys(handle)` → list all keys
- [x] `thag_toml_free(handle)` → free parsed TOML

### Runtime — IO Stack
- [x] `thag_http_get/post` — real HTTP client (already scaffolded, harden)
- [x] `ws_connect/send/close` — real WebSocket (already scaffolded, harden)
- [x] `db_connect/query/close` — real DB client (SQLite amalgamation)
- [x] Cancellation/timeout propagate qua IO boundaries
- [x] Async scheduler ổn định với IO integration

### Stdlib — Real Implementations (gọi runtime backend qua extern func)
- [x] `std/string.tg` — đầy đủ: concat, split, join, trim, contains, starts_with, len, from_int, to_int, substr, replace, format
- [x] `std/list.tg` — append, remove, sort, filter, map, reduce, len
- [x] `std/core.tg` — error types, Result/Option helpers, assert
- [x] `lib/fs.tg` — read, write, exists, mkdir, readdir, remove, getcwd, path_join, is_dir, filesize
- [x] `lib/process.tg` — run, capture, argv, argc, env, exit
- [x] `lib/toml.tg` — parse, get_str, get_int, get_section, get_keys, free
- [x] `lib/http.tg` — production-ready HTTP client wrapper
- [x] `lib/ws.tg` — WebSocket wrapper với error handling
- [x] `lib/db.tg` — DB wrapper với query builder cơ bản
- [x] `lib/time.tg` — `now()`, `sleep()`, duration types
- [x] `lib/map.tg` — HashMap thật sự, không phải stub

### Gate
- [x] Drago (pure .tg, no C helpers in drago repo) có thể dùng stdlib để: đọc file, parse TOML, chạy process, manipulate strings
- [x] End-to-end async IO path pass Linux parity contracts

---

## v1.0 — Deploy Baseline
> *"Single binary. One command. Ship anywhere."*

### Compiler & Tooling
- [x] Static link LLVM vào `thagc` — true standalone binary (~50-80MB), user không cần cài LLVM
- [x] Single-binary default — không cần runtime dependency
- [x] One-command cross-compile: `thagc build --target aarch64-linux`
- [x] Cold-start budget enforced trong CI (< 10ms target)
- [x] Binary-size baseline trong CI
- [x] `thagc target` + `thagc build` recipes đầy đủ documented

### Drago Package Registry (Production)
- [x] Registry stable, versioned packages
- [x] `drago install/update/remove` hoàn chỉnh (thay thế `thagc install`)
- [x] Package publishing workflow
- [x] Dependency resolution + lock file

### FFI
- [x] `extern` C function calls hoạt động đầy đủ
- [x] C library linking trong build pipeline
- [x] FFI safety guidelines documented

### Docs
- [x] Getting started guide — từ install đến hello world trong 60 giây
- [x] Beginner tutorial — học giải thuật bằng Thagore
- [x] API reference cơ bản

### Gate
- [x] Một CLI tool thực tế được build, cross-compiled, và distributed dưới dạng single binary
- Release execution runbook: `docs/runbooks/v1-0-release-thagore-drago.md`

---

## v1.1 — Structured Concurrency GA
> *"Concurrent code in production — zero surprises"*

### Runtime
- [x] Soak tests cho long-running scoped workloads (1h+) (`tests/soak/test_concurrency_long_running.py`, env-gated)
- [x] Task tree diagnosis / tracing hooks
- [x] No open P0/P1 bugs cho scope/cancel/timeout/nursery (`contracts/concurrency/p0_p1_registry.json` gate)
- [x] Deadlock detection với helpful error message

### Language
- [x] `flow` construct MVP — `flow`/`step`/`undo`/`retry`/`timeout`/`idempotent` keywords
- [x] Flow compile-time validation — undo/retry semantics enforced

### Gate
- [x] No open P0/P1 concurrency bugs, soak tests stable

---

## v1.2 — IO Stack GA
> *"Production HTTP/WS/DB — reliable across all platforms"*

### Runtime & Stdlib
- [ ] HTTP/WebSocket/DB production-ready với retry/backoff
- [ ] Multi-OS IO parity — Linux/macOS/Windows
- [ ] IO failure handling runbook

### Stdlib Expansion
- [ ] `lib/json.tg` — parse/serialize JSON
- [ ] `lib/env.tg` — environment variables, process args
- [ ] `lib/fs.tg` — file read/write/stat
- [ ] `lib/crypto.tg` — hash, HMAC cơ bản

### Gate
- [ ] HTTP/WebSocket/DB lanes pass trên Linux/macOS/Windows trong CI

---

## v1.3 — Performance Lockdown (In Progress)
> *"Fast enough to replace Go. Simple enough to teach beginners."*

### Status Snapshot (audit ngày March 2, 2026)
- [x] Startup p95 budget gate (Linux) trong CI (`tooling/policy/check_startup_budget.py` + `contracts/perf/startup_budget.json`)
- [x] Binary size budget gate (Linux) trong CI (`tooling/policy/check_binary_size_budget.py` + `contracts/perf/binary_size_budget.json`)
- [ ] Latency benchmark lane cho workload chuẩn chưa có automation per-commit
- [ ] `tooling/bench/` chưa tồn tại

### Compiler
- [ ] Expose optimization levels (`-O0/-O1/-O2/-O3`) từ CLI build pipeline
- [ ] Chạy LLVM optimization pipeline trước object emission (không chỉ coroutine lowering)
- [ ] Codegen cho tight loops (`for`/`while`) không có overhead + có regression tests
- [ ] Inlining hints/heuristics cho small functions + validation benchmarks

### Tooling
- [ ] Benchmark automation — `tooling/bench/`
- [x] Performance threshold alerts trong CI cho startup + binary size (Linux lane)
- [ ] p95 latency và startup metrics tracked per commit (artifact JSON + trend diff)
- [ ] Perf budgets mở rộng cho macOS/Windows

### AI/ML Foundation (Preview)
- [ ] FFI bindings cho CUDA/OpenCL basic
- [ ] `stdlib/lib/tensor.tg` stub — groundwork cho AI use case
- [ ] PyTorch interop proof of concept (call C++ kernel từ Thagore)

### Gate
- [ ] Startup + binary size + p95 latency metrics đạt release budgets trên Linux/macOS/Windows
- [ ] Benchmark automation publish report so sánh với Go, Rust, Python

---

## v1.4 — Platform Hardening + Developer Experience
> *"Works everywhere. Feels great everywhere."*

### Cross-Platform
- [ ] 3-OS deterministic gate — Linux/macOS/Windows
- [ ] Windows code signing trong release workflow
- [ ] Cross-compile + deploy + smoke tests stable
- [ ] Migration guide và rollback playbook

### Developer Experience
- [ ] LSP server MVP — syntax highlighting, go-to-definition, autocomplete
- [ ] Error messages review — tất cả errors đều có fix suggestion
- [ ] `thagc fix` — autofix thật sự cho common errors
- [ ] REPL / interactive mode cho học giải thuật

### Typestate (Preview)
- [ ] `state Session: Init | Ready | Closed` syntax trong lexer/parser
- [ ] Typestate tracking trong typechecker
- [ ] `W_STATE_AMBIGUOUS` / `E_STATE_*` diagnostics
- [ ] Compile-time wrong-state-use prevention

### Community
- [ ] Drago Registry public launch
- [ ] Contributor guide hoàn chỉnh
- [ ] Discord/GitHub Discussions active
- [ ] First external contributor PR merged

### Gate
- [ ] Cross-compile + deploy stable trên cả 3 OS
- [ ] LSP cơ bản hoạt động trong VS Code

---

## v1.5 — Stable Release
> *"Ready for production. Ready for beginners. Ready for Vietnam."*

### Final Hardening
- [ ] Tất cả P0/P1 bugs closed
- [ ] Concurrency, memory, IO, deploy UX đều hoàn chỉnh
- [ ] 1:1 syntax/semantics parity với baseline contracts
- [ ] Tất cả 10 CLI command groups đầy đủ và documented
- [ ] Release gate: Linux + macOS + Windows deterministic ✓

### Documentation Complete
- [ ] Language reference đầy đủ
- [ ] Standard library docs
- [ ] Tutorial series: beginner → intermediate → advanced
- [ ] "Build a bot in Thagore" tutorial cho người mới học
- [ ] Vietnamese documentation

### Ecosystem
- [ ] 20+ packages trên Drago Registry
- [ ] Selfhost milestone — compiler tự compile một phần code của mình
- [ ] Example projects: CLI tool, REST API, bot, algorithm visualizer

### Gate
- [ ] Một người không biết lập trình có thể follow tutorial và build bot trong 2 giờ

---

## v1.6 — Joy Release
> *"The language that makes you smile."*

- [ ] GUI framework binding — cross-platform UI (SDL2 hoặc native via FFI)
- [ ] `lib/gui.tg` — window, canvas, event loop
- [ ] Drawing app demo bằng Thagore — proof of concept cho creative use case
- [ ] Game loop support — fixed timestep, input handling
- [ ] WASM compilation target — chạy Thagore trên browser
- [ ] Playground online — viết Thagore trên web không cần install
- [ ] `intent` construct MVP — compiler-driven optimization annotation
- [ ] Typestate GA — production-ready, full diagnostics

### Gate
- [ ] Có thể build một drawing/painting app đơn giản bằng Thagore

---

## v1.7 — AI & Scale Release
> *"From prototype to production — no rewrite."*

- [ ] `lib/tensor.tg` — production-ready tensor operations via FFI
- [ ] CUDA kernel calls từ Thagore — AI inference use case
- [ ] Model serving example — FastAPI-equivalent bằng Thagore
- [ ] `lib/grpc.tg` — gRPC client/server
- [ ] `lib/sql.tg` — full SQL builder, migrations
- [ ] Distributed tracing integration
- [ ] `flow` construct GA — production-ready saga/transaction pattern
- [ ] Hot reload cho development workflow
- [ ] Package ecosystem: 100+ packages trên Drago Registry

### Gate
- [ ] Có thể serve AI model inference bằng Thagore, nhanh hơn Python Flask 10x

---

## v1.8 — Stable & Complete
> *"One language. Every dream."*

### Language Complete
- [ ] Tất cả language features đã design đều implemented và stable
- [ ] `flow`, `intent`, `typestate` đều GA
- [ ] Generic types đầy đủ
- [ ] Macro / comptime system cơ bản

### Ecosystem Mature
- [ ] 500+ packages trên Drago Registry
- [ ] Selfhost compiler hoàn chỉnh — Thagore tự compile bằng Thagore
- [ ] Thagore được dùng trong ít nhất một trường đại học Việt Nam
- [ ] Benchmark public: cạnh tranh với Go trên backend benchmarks
- [ ] Windows/macOS/Linux desktop app được build bằng Thagore

### Community
- [ ] 10,000+ GitHub stars
- [ ] Active contributors từ ngoài core team
- [ ] Conference talk về Thagore tại một sự kiện tech Việt Nam
- [ ] Một startup Việt Nam dùng Thagore ở production

### The Proof
- [ ] Một học sinh lớp 10 học giải thuật bằng Thagore — vui vẻ, không bị lỗi làm nản
- [ ] Một dev build project lớn một mình bằng Thagore — hào hứng, không bị complexity cản
- [ ] Một startup build và deploy AI app bằng Thagore — không cần rewrite khi lên production

---

## Summary

| Version | Theme | Status |
|---------|-------|--------|
| v0.1 | Baseline rewrite | ✅ Completed |
| v0.2 | Compiler foundation | ✅ Completed (gate passed Linux) |
| v0.3 | Struct & type system | ✅ Completed |
| v0.4 | Module & import system | ✅ Completed |
| v0.5 | Language features | ✅ Completed |
| v0.6 | Concurrency alpha | ✅ Released |
| v0.7 | Structured concurrency | ✅ Released |
| v0.8 | Memory model MVP | ✅ Released |
| v0.9 | IO stack alpha | ✅ Released |
| v1.0 | Deploy baseline | ✅ Released |
| v1.1 | Concurrency GA | ✅ Released |
| v1.2 | IO stack GA | 🔲 Planned |
| v1.3 | Performance lockdown | 🟨 In progress |
| v1.4 | Platform hardening + DX | 🔲 Planned |
| v1.5 | **Stable release** | 🔲 Planned |
| v1.6 | Joy release (GUI, WASM) | 🔲 Planned |
| v1.7 | AI & scale | 🔲 Planned |
| v1.8 | **Stable & complete** | 🔲 Planned |

---

*"Thagore was built because no language made learning algorithms fun,
no language was both powerful and simple,
and no language let you go from first line to production without fighting it.
We're fixing that — for everyone."*
