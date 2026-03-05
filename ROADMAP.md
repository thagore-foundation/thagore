# Thagore Roadmap — v0.1 → v2.5 Production Complete

Current effective milestone by code audit (March 6, 2026): `v2.3` (HM inference + generics baseline, released).
Status note: v2.3 inference/generics gates pass on the active release lane with parity + integration + e2e suites green.
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
- [x] Float types (`f32`, `f64`) có trong backend `ValueType::F32/F64` (decimal float atom hiện được recognize ở expression lowering path)
- [x] Multi-function codegen — user-defined functions được emit và gọi được
- [x] `bool` literal `true`/`false` là first-class value end-to-end
- [x] Binary arithmetic cho float — `+`, `-`, `*`, `/` với `f32`/`f64`
- [x] Typechecker pass thật sự — kiểm tra type mismatch, undefined variable
- [x] Error messages rõ ràng, có line/column number, không cryptic

### Stdlib
- [x] `std/core.tg` — `print_int`, `print_float`, `print_bool` thật sự hoạt động
- [x] `std/string.tg` — `len()`, `concat()`, `trim()` basic

### CI
- [x] Windows build job trong `ci.yml`
- [x] 3-OS matrix đầy đủ cho build jobs

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
- [x] Incremental compilation — chỉ recompile file thay đổi

### Drago Package Registry (MVP)
- [x] `thagc install <package>` hoạt động
- [x] Local cache tại `~/.thagore/packages/`
- [x] Registry endpoint cơ bản
- [x] `thagore.toml` lock file

### Gate
- [x] Project nhiều file compile thành công, import package từ manifest/cache hoạt động

---

## v0.5 — Language Features Complete ✅ Released
> *"Ngôn ngữ đủ để viết chương trình thật"*

### Compiler — Language Features
- [x] Closures — capture, function-value type, emit đúng trong LLVM
- [x] `defer` stack — defer-stack mechanism trong backend, đúng execution order
- [x] Interpolated strings — `v"Hello {name}!"` compile thành string concat (`"..."` thuần không interpolate)
- [x] `Result<T, E>` / `Option<T>` — built-in sum types, không cần generic đầy đủ
- [x] `?` operator — early return cho Result/Option
- [x] Tuple type — `(i32, string)`, tuple destructuring
- [x] Array/slice literals — `[1, 2, 3]`, index access `arr[i]`
- [x] Loop labels + labeled `break`/`continue`
- [x] `pub` visibility enforcement — cross-module access control

### Type System
- [x] Generic-like builtins cơ bản — `Option<T>`, `Result<T, E>` là builtins; `List<T>` currently name-level/sugar (chưa có full generic instantiation)
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

### Stdlib Auto-Resolution (compiler tự tìm stdlib, không cần copy vào project)
- [x] Embed stdlib `.tg` files vào `thagc` binary (giống embed `libthag_runtime.a`)
  - Khi compile `import std.string` hoặc `from lib.fs import read`, compiler extract stdlib `.tg` từ embedded data
  - Không cần `~/.thagore/stdlib/`, không cần copy file, không cần env var
  - Có fallback `THAG_STDLIB_PATH` cho development
- [x] Import resolution order cho `std.*` và `lib.*`:
  1. Project-local file (nếu tồn tại) — cho phép override
  2. Embedded stdlib trong `thagc` binary
  3. `THAG_STDLIB_PATH` env var (optional, cho development)
- [x] `from std.string import concat` hoạt động mà không cần copy `stdlib/` vào project
- [x] `from lib.fs import read` hoạt động mà không cần copy `stdlib/` vào project
- [x] Tất cả stdlib modules được embed: `std/core.tg`, `std/string.tg`, `std/list.tg`, `lib/fs.tg`, `lib/process.tg`, `lib/toml.tg`, `lib/http.tg`, `lib/ws.tg`, `lib/db.tg`, `lib/time.tg`, `lib/map.tg`

### Gate
- [x] Drago (pure .tg, no C helpers in drago repo) có thể dùng stdlib để: đọc file, parse TOML, chạy process, manipulate strings
- [x] End-to-end async IO path pass Linux parity contracts
- [x] `import std.string` / `from lib.fs import read` hoạt động out-of-the-box, zero setup

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
- [x] `flow` construct MVP (syntax layer) — `flow`/`step`/`undo`/`retry`/`timeout`/`idempotent` keywords
- [x] Flow compile-time validation — undo/retry semantics enforced
- [x] Flow runtime/codegen execution path (flow block emits executable runtime behavior via middleend lowering)

### Gate
- [x] No open P0/P1 concurrency bugs, soak tests stable

---

## v1.2 — IO Stack GA
> *"Production HTTP/WS/DB — reliable across all platforms"*

### Runtime & Stdlib
- [x] HTTP/WebSocket/DB production-ready với retry/backoff
- [x] Multi-OS IO parity — Linux/macOS/Windows
- [x] IO failure handling runbook

### Stdlib Expansion
- [x] `lib/json.tg` — parse/serialize JSON
- [x] `lib/env.tg` — environment variables, process args
- [x] `lib/fs.tg` — file read/write/stat
- [x] `lib/crypto.tg` — hash, HMAC cơ bản (FFI vào libcrypto)

### Gate
- [x] HTTP/WebSocket/DB lanes pass trên Linux/macOS/Windows trong CI

---

## v1.2b — Drago–Thagc Unification
> *"Một manifest, một tool, một ecosystem — không mâu thuẫn"*

### Vấn đề hiện tại
thagc có `thagc install` + `thagore.toml`, drago có `drago add/install` + `drago.toml`.
Hai hệ thống song song, gây nhầm lẫn. Cần thống nhất: **drago là frontend duy nhất
cho project management**, thagc chỉ là compiler backend thuần.

### Manifest unification
- [x] `drago.toml` là manifest chính thức duy nhất cho compile/dependency resolution; `thagore.toml` chỉ còn dùng cho migration
- [x] `thagc` đọc `drago.toml` khi cần resolve dependencies (thay vì `thagore.toml`)
- [x] Migration tool: `thagc migrate` convert `thagore.toml` → `drago.toml` (+ `thagore.lock` → `drago.lock`)
- [x] `drago.lock` là lock file duy nhất cho active workflow; `thagore.lock` chỉ còn input của `thagc migrate`

### CLI unification
- [x] Xóa `thagc install` — chuyển hoàn toàn sang `drago add/install`
- [x] Xóa `thagc update` — chuyển sang `drago update`
- [x] `thagc` chỉ giữ compile surface tối giản: `build`, `run` (compile file đơn), `check`, `fmt` (+ `migrate` transitional)
- [x] `drago build/run/test` gọi `thagc` ngầm — user dùng drago cho project workflows
- [x] `thagc build` khi thấy `drago.toml` trong cwd → tự delegate sang `drago build`

### Package cache unification
- [x] `~/.thagore/packages/` — chỉ drago quản lý, thagc không đọc/ghi trực tiếp
- [x] thagc nhận package paths từ drago qua CLI args hoặc manifest
- [x] `drago` resolve dependencies → pass `--include-path` cho `thagc`

### Registry unification
- [x] `thagore-lang/registry` — drago là client duy nhất tương tác với registry
- [x] `thagc` không biết registry tồn tại — nó chỉ compile files được đưa cho

### Installer unification
- [x] `thagup-init.sh` cài cả `thagc` + `drago` — user dùng `drago` ngay từ đầu
- [x] First-run experience: `drago new myapp && cd myapp && drago run`
- [x] Không ai cần biết `thagc` tồn tại trừ khi compile file lẻ

### Gate
- [x] `thagore.toml` không còn nằm trong active compile flow hoặc docs hướng dẫn chính (chỉ còn trong migrate + tests migration)
- [x] `thagc install` / `thagc update` bị xóa hoàn toàn
- [x] `drago build` → `drago run` → `drago test` hoạt động end-to-end với `drago.toml`
- [x] Documentation user-flow dùng `drago` cho project/dependency/update workflows

---

## v1.3 — Performance Lockdown
> *"Fast enough to replace Go. Simple enough to teach beginners."*

### Compiler
- [x] LLVM optimization passes được enable đúng cách (`run_performance_passes`, O3 pipeline, aggressive codegen opt level)
- [x] Codegen cho tight loops không có overhead đáng kể (loop unroll/vectorization/interleaving in optimization pipeline)
- [x] Inlining hints cho small functions (`InlineHint` cho small non-async functions)

### Tooling
- [x] Benchmark automation — `tooling/bench/` (`run_benchmarks.py`)
- [x] Performance threshold alerts trong CI (`check_latency_budget.py` + CI gate)
- [x] p95 latency và startup metrics tracked per commit (artifact `perf-metrics-${sha}`)

### AI/ML Foundation (Preview)
- [x] FFI bindings cho CUDA/OpenCL basic (`thag_cuda_available`, `thag_opencl_available`)
- [x] `lib/tensor.tg` runtime-backed module — groundwork cho AI use case
- [x] PyTorch interop proof of concept (Thagore gọi `thag_pytorch_axpy_i64` C++ kernel path)

### Gate
- [x] p95 latency và startup metrics đạt release budgets (`contracts/perf/latency_budget.json`)
- [x] Benchmark so sánh với Go, Rust, Python được publish (`docs/perf/benchmark-v1.3.md`)

---

## v1.4 — Platform Hardening + Developer Experience
> *"Works everywhere. Feels great everywhere."*

### Cross-Platform
- [x] 3-OS deterministic gate — Linux/macOS/Windows
- [x] Windows code signing trong release workflow
- [x] Cross-compile + deploy + smoke tests stable
- [x] Migration guide và rollback playbook

### Developer Experience
- [x] LSP server MVP — protocol wiring (`--stdio`), basic completion keywords, text-search definition lookup
- [x] Error messages review — tất cả errors đều có fix suggestion
- [x] `thagc fix` — safe syntax autofix lane (normalize indent + append missing block `:`)
- [x] REPL / interactive mode cho học giải thuật

### Typestate (Preview)
- [x] `state Session: Init | Ready | Closed` syntax trong lexer/parser
- [x] Typestate tracking trong typechecker
- [x] `W_STATE_AMBIGUOUS` / `E_STATE_*` diagnostics
- [x] Compile-time wrong-state-use prevention

### Community
- [x] Drago Registry public launch
- [x] Contributor guide hoàn chỉnh
- [x] Discord/GitHub Discussions active
- [x] First external contributor PR merged

### Gate
- [x] Cross-compile + deploy stable trên cả 3 OS
- [x] LSP cơ bản hoạt động trong VS Code

---

## v1.5 — Stable Release
> *"Ready for production. Ready for beginners. Ready for Vietnam."*

### Final Hardening
- [x] Tất cả P0/P1 bugs closed
- [x] Concurrency, memory, IO, deploy UX đều hoàn chỉnh
- [x] 1:1 syntax/semantics parity với baseline contracts
- [x] Tất cả 10 CLI command groups đầy đủ và documented
- [x] Release gate: Linux + macOS + Windows deterministic ✓

### Documentation Complete
- [x] Language reference đầy đủ
- [x] Standard library docs
- [x] Tutorial series: beginner → intermediate → advanced
- [x] "Build a bot in Thagore" tutorial cho người mới học
- [x] Vietnamese documentation

### Ecosystem
- [x] 20+ packages trên Drago Registry
- [ ] Selfhost milestone — compiler tự compile một phần code của mình
- [x] Example projects: CLI tool, REST API, bot, algorithm visualizer

Selfhost note: workflow `selfhost-readiness.yml` currently validates deterministic/soak readiness only; it is not yet a full selfhost compilation proof.

### Gate
- [x] Một người không biết lập trình có thể follow tutorial và build bot trong 2 giờ

---

## v1.6 — Joy Release
> *"The language that makes you smile."*

- [x] GUI framework binding — cross-platform native canvas via runtime FFI (`runtime/src/gui.cpp`, `lib/gui.tg`)
- [x] `lib/gui.tg` — window, canvas, event loop (runtime canvas backend + frame present)
- [x] Drawing app demo bằng Thagore — proof of concept cho creative use case (`examples/v1_6_drawing_app.tg`)
- [x] Game loop support — fixed timestep, input handling (`run_fixed_timestep`)
- [x] WASM compilation target — `thagc build --target=wasm32-unknown-unknown` emits runnable wasm module
- [x] Playground online — browser editor + `/api/run` compile/execute server (`playground/`, `tooling/playground/server.py`)
- [x] `intent` construct MVP — `thagc intent explain|doctor` + compile-time goal/strategy validation
- [x] Typestate GA — production-ready diagnostics + `thagc state doctor` full finding output

### Gate
- [x] Có thể build một drawing/painting app đơn giản bằng Thagore (canvas frame output path verified by integration test)

---

## v1.7 — AI & Scale Release
> *"From prototype to production — no rewrite."*

- [x] `lib/tensor.tg` — production-ready tensor operations via FFI (`add/mul/dot/relu/argmax/cuda_axpy`)
- [x] CUDA kernel calls từ Thagore — AI inference lane via runtime CUDA-aware AXPY hook (`thag_tensor_cuda_axpy_i64`)
- [x] Model serving example — service-style inference pipeline bằng Thagore (`examples/v1_7_model_serving.tg`)
- [x] `lib/grpc.tg` — gRPC client lane (`unary`, `health`) backed by runtime transport hook
- [x] `lib/sql.tg` — SQL builder + migration helpers (`SELECT/FROM/WHERE/ORDER/LIMIT`, `migrate_apply`)
- [x] Distributed tracing integration (`lib/trace.tg`, `thag_trace_span_begin/end`, `thag_trace_event`)
- [x] `flow` construct GA — executable flow functions with retry/timeout/undo rollback
- [x] Hot reload cho development workflow (`thagc run --watch`)
- [x] Package ecosystem: 100+ packages trên Drago Registry (`docs/community/registry-package-catalog-v1.7.md`)

### Gate
- [x] Có thể serve AI model inference bằng Thagore, nhanh hơn Python Flask 10x (`docs/perf/benchmark-v1.7-model-serving.md`, speedup gate contract passed)

---

## v1.8 — Stable & Complete
> *"One language. Every dream."*

### Language Complete
- [x] Tất cả language features đã design đều implemented và stable (validated by compiler/integration/parity suites)
- [x] `flow`, `intent`, `typestate` đều GA
- [x] Generic types đầy đủ (built-in generic families `Option/Result/List/Rc/Arc` with nested + arity validation)
- [x] Macro / comptime system cơ bản (`macro name(args) = expr`, `comptime:` compile-time bindings)

### Ecosystem Mature
- [ ] 500+ packages trên Drago Registry
- [ ] Selfhost compiler hoàn chỉnh — Thagore tự compile bằng Thagore
- [ ] Thagore được dùng trong ít nhất một trường đại học Việt Nam
- [x] Benchmark public: cạnh tranh với Go trên backend benchmarks (`docs/perf/benchmark-v1.8-public.md`, CI gate `check_public_backend_gate.py`)
- [x] Windows/macOS/Linux desktop app được build bằng Thagore (workflow `.github/workflows/desktop-app-matrix.yml`)

### Community
- [ ] 10,000+ GitHub stars
- [ ] Active contributors từ ngoài core team
- [ ] Conference talk về Thagore tại một sự kiện tech Việt Nam
- [ ] Một startup Việt Nam dùng Thagore ở production

### The Proof
- [ ] Một học sinh lớp 10 học giải thuật bằng Thagore — vui vẻ, không bị lỗi làm nản
- [ ] Một dev build project lớn một mình bằng Thagore — hào hứng, không bị complexity cản
- [ ] Một startup build và deploy AI app bằng Thagore — không cần rewrite khi lên production

v1.8 note: engineering gates are in-repo/CI verifiable. External ecosystem/community
adoption gates above require real-world evidence and remain tracked until fulfilled.

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
| v1.2 | IO stack GA | ✅ Released |
| v1.2b | Drago–Thagc unification | ✅ Released |
| v1.3 | Performance lockdown | ✅ Released |
| v1.4 | Platform hardening + DX | ✅ Released |
| v1.5 | **Stable release** | ✅ Released |
| v1.6 | Joy release (GUI, WASM) | ✅ Released |
| v1.7 | AI & scale | ✅ Released |
| v1.8 | **Stable & complete** | ✅ Engineering Complete (ecosystem adoption tracked) |
| v1.9 | Span system + token pipeline | ✅ Released |
| v2.0 | Real recursive-descent parser | ✅ Released |
| v2.1 | Symbol table & name resolution | 🔲 Planned |
| v2.2 | HIR & bidirectional type checking | ✅ Released |
| v2.3 | Full type inference & generics | ✅ Released |
| v2.4 | MIR & borrow checker | 🔲 Planned |
| v2.5 | **Production complete** | 🔲 Planned |

---

## v1.9 — Span System + Token Pipeline ✅ Released

**Theme:** Stop ignoring the lexer. Wire the real token stream into the parser so every
subsequent pass has byte-level location data to work with.

**Why this version exists (code audit findings):**
- `compiler/src/frontend/parser.cpp` line 6: `(void)tokens;` — the entire token stream
  produced by the lexer is discarded; the parser re-reads raw source text instead.
- `compiler/include/thagc/frontend/token.hpp`: `Token` carries only `line`/`column`,
  no byte-offset `Span`. Diagnostics can only print line numbers, not underline ranges.
- Without spans, every later pass (name resolution, type errors, borrow errors) can only
  produce weak messages like `error on line 42` instead of `^^^ here`.

**Deliverables:**

### 1. Span type
```
compiler/include/thagc/frontend/span.hpp
  struct Span { uint32_t lo; uint32_t hi; uint32_t file_id; };
```
Add `Span span` field to `Token`. Update `Lexer::tokenize()` to fill it from byte offsets.

### 2. Source map
```
compiler/include/thagc/frontend/source_map.hpp
  class SourceMap {
    uint32_t add_file(std::string path, std::string src);
    std::pair<uint32_t,uint32_t> lookup_line_col(Span) const;
    std::string_view snippet(Span) const;
  };
```

### 3. Wire tokens into Parser
Remove `(void)tokens;`. Store the token list as `std::span<const Token>` on the Parser
context. The existing line-by-line logic continues to work (no breakage), but the token
list is now available for future passes.

### 4. Span propagation into AST nodes
Add optional `Span span` to `AstFunctionDecl`, `AstStructDecl`, `AstImport`,
`AstFlowStep`, `CoreStmt`. Fill from the first/last token that produced each node.

### 5. Diagnostic helper
```
compiler/include/thagc/diag/diag.hpp
  enum DiagLevel { Note, Warning, Error };
  struct Diag { DiagLevel level; Span span; std::string message; };
  void emit_diag(const SourceMap&, const Diag&);   // prints ^^^-style caret
```
Replace all bare `std::cerr` / `add_parse_error` string pushes with `Diag` emission.

**Build gate:** `cmake --build build` succeeds, existing `hello.tg` compile still works,
new `thagc --check` on a file with a known parse error prints a caret underline.

**Release gate:** tag `v1.9.0`, publish Linux + macOS binaries via `.github/workflows/release.yml`.
Changelog entry must include "span-aware diagnostics".

---

## v2.0 — Real Recursive-Descent Parser ✅ Released

**Theme:** Replace the line-by-line text-processing parser with a real token-stream
recursive-descent parser. This is the single largest structural change in the compiler.

**Why this version exists:**
- Current `Parser::parse()` in `parser.cpp` re-reads `source` as raw text, splitting on
  newlines, using `starts_with("func ")`, `starts_with("struct ")`, etc. It cannot handle
  multi-line expressions, complex generics, nested closures, or operator precedence
  without special-casing every pattern.
- `backend/expr.cpp` `parse_primary` (624 lines) re-parses string expressions a **third**
  time. Three parsing passes on the same source is unsustainable.
- Rustc's parser is a single recursive-descent pass over a real token stream, producing a
  well-typed AST. Thagc needs the same.

**Deliverables:**

### 1. Parser context
```cpp
struct ParserCtx {
  std::span<const Token> tokens;
  std::size_t pos = 0;
  SourceMap& smap;
  std::vector<Diag> diags;
  Token peek(int offset = 0) const;
  Token advance();
  bool eat(TokenKind);
  Token expect(TokenKind, std::string_view msg);
};
```

### 2. Expression parser (Pratt / precedence-climbing)
Single `parse_expr(ParserCtx&, int min_bp)` replacing both the existing
`tokenize_expression` + ad-hoc string passes in `frontend/expr.cpp` and the
`parse_primary` in `backend/expr.cpp`.

Precedence table covers: literals, identifiers, unary, `*/%`, `+-`, `<><=>=`,
`==!=`, `&&`, `||`, `?:`, assignments, `|>`, `..`, `as`, `await`.

### 3. Statement parser
`parse_stmt(ParserCtx&)` handles: `let`, `var`, assignment, `return`, `if`/`elif`/`else`,
`for`, `while`, `match`, `break`/`continue`, expression-statements.

### 4. Top-level parser
`parse_item(ParserCtx&)` handles: `func`, `struct`, `enum`, `impl`, `state`,
`flow`, `import`/`from`, `macro`, `extern`.

### 5. Remove the old line-by-line path
Delete the `src/frontend/syntax.cpp` text-scanner helpers that are now superseded.
Keep `internal.hpp` shared utilities (`trim`, `is_identifier`) used elsewhere.

### 6. Remove third-pass re-parsing in backend
`backend/expr.cpp` must consume `AstExpr` nodes (from step 2) instead of
re-tokenizing string expressions. `CoreStmt::expression` becomes `AstExprPtr`
(a `std::unique_ptr<AstExpr>`).

**Build gate:** `cmake --build build` clean succeeds. Full test suite (`thagc --check`
on all files in `tests/`) passes. No regressions on `hello.tg`, structs, enums, imports.

**Release gate:** tag `v2.0.0`. Binaries ship. Changelog: "replaced line-by-line parser
with recursive-descent token-stream parser; 3-pass re-parse eliminated."

---

## v2.1 — Symbol Table & Name Resolution

**Theme:** Every identifier gets a `DefId`. Scopes are tracked. Unresolved names are
errors at compile time, not silent runtime failures.

**Why this version exists:**
- There is no symbol table in the current compiler. `CoreStmt::expression` is a raw
  string; identifiers are never looked up against a scope.
- Rustc's name resolution assigns every binding a `DefId` and resolves every use-site
  back to its definition before any type-checking begins.

**Deliverables:**

### 1. DefId and the crate map
```cpp
struct DefId { uint32_t krate; uint32_t index; };
struct DefInfo { DefKind kind; Span span; std::string name; };
class DefMap { DefId intern(DefInfo); const DefInfo& get(DefId) const; };
```
`DefKind`: `Function`, `Struct`, `Enum`, `EnumVariant`, `Field`, `Local`,
`Param`, `TypeParam`, `Trait`, `ImplMethod`, `ExternFn`, `Module`.

### 2. Scope resolver pass
Walk the AST produced by v2.0 parser. Build a scope stack. For each binding
(`let x`, `func f`, `struct S`, `param p`) call `DefMap::intern` and attach
`DefId` to the AST node. For each use-site (`x`, `f(...)`, `S { }`) look up
the scope chain and store `DefId` on the use node.

Emit `Diag::Error` with caret underline for:
- undefined name
- use before declaration
- duplicate binding in same scope
- import of non-existent symbol

### 3. Module system wiring
`import std.core` → resolve to embedded stdlib source, run resolver on it,
expose its exported `DefId`s into the importing scope.

### 4. `thagc --check` reports name errors
No codegen for files with name errors. Exit code non-zero.

**Build gate:** `cmake --build build` clean. `tests/name_resolution/` suite all pass.
Previous test suites still pass.

**Release gate:** tag `v2.1.0`. Binaries ship. Changelog: "symbol table, DefId,
scope resolution, undefined-name errors with source spans."

---

## v2.2 — HIR & Bidirectional Type Checking ✅ Released

**Theme:** Introduce a typed High-level IR. Replace `std::string expression` in
`CoreStmt` with a proper expression tree. Add bidirectional type checking for the
simple cases (literals, calls with known signatures, let-with-annotation).

**Why this version exists:**
- `compiler/include/thagc/middleend/core_ir.hpp`: `CoreStmt` stores a raw `std::string
  expression`. No type is ever computed or verified. Passing a string to an integer
  parameter is undetected.
- `compiler/include/thagc/frontend/typechecker.hpp`: stub — `bool check(...)` declared,
  nothing implemented.
- Rustc's HIR is a desugared, fully-resolved, partially-typed tree that feeds into the
  type checker (rustc_hir_typeck). We need the equivalent.

**Deliverables:**

### 1. HIR expression nodes
```cpp
// compiler/include/thagc/hir/expr.hpp
struct HirLit   { LitKind kind; std::string value; Span span; };
struct HirIdent { DefId def; Span span; };
struct HirCall  { HirExprPtr callee; std::vector<HirExprPtr> args; Span span; };
struct HirBin   { BinOp op; HirExprPtr lhs, rhs; Span span; };
struct HirUnary { UnaryOp op; HirExprPtr operand; Span span; };
struct HirField { HirExprPtr base; std::string field; Span span; };
struct HirIndex { HirExprPtr base; HirExprPtr index; Span span; };
struct HirIf    { HirExprPtr cond; HirBlock then_b; std::optional<HirBlock> else_b; };
// ... match, closure, await, pipe, range, cast, struct-literal, tuple
using HirExpr = std::variant<HirLit,HirIdent,HirCall,HirBin,HirUnary,
                             HirField,HirIndex,HirIf,...>;
using HirExprPtr = std::unique_ptr<HirExpr>;
```

### 2. HIR lowering pass
`AstExpr → HirExpr`. Resolves `DefId` (using v2.1 symbol table), desugars
`for x in y` → iterator loop, desugars `match` into decision tree, desugars
`|>` pipe into nested calls.

### 3. Type representation
```cpp
// compiler/include/thagc/ty/ty.hpp
struct TyInt { int bits; bool is_signed; };
struct TyFloat { int bits; };
struct TyBool {}; struct TyUnit {}; struct TyStr {};
struct TyNamed { DefId def; std::vector<Ty> args; };   // structs, enums
struct TyFn    { std::vector<Ty> params; Ty ret; };
struct TyVar   { uint32_t id; };  // unification variable for HM
using Ty = std::variant<TyInt,TyFloat,TyBool,TyUnit,TyStr,TyNamed,TyFn,TyVar,...>;
```

### 4. Bidirectional type checker
`TypeChecker::infer(HirExpr&) -> Ty` and `TypeChecker::check(HirExpr&, Ty expected)`.
Handles: integer/float/bool/string literals, identifiers (look up `DefId` → type),
function calls (check arity + argument types), `let x: T = e` (check `e` against `T`),
`return e` (check against enclosing function return type).

Emits typed `Diag::Error` with caret for:
- type mismatch
- wrong arity
- unknown field
- non-callable expression

### 5. Replace `CoreStmt::expression: std::string` with `HirExprPtr`
All backend codegen that previously re-parsed the string now walks the HIR tree.
`backend/expr.cpp` `evaluate_expression` becomes `lower_hir_expr_to_llvm`.

**Build gate:** `cmake --build build` clean. `tests/typeck/` suite passes.
No regressions. `thagc --check bad_type.tg` exits non-zero with underlined error.

**Release gate:** tag `v2.2.0`. Changelog: "typed HIR, bidirectional type checker,
type-mismatch errors with source underlines."

---

## v2.3 — Full Hindley-Milner Type Inference & Generics ✅ Released

**Theme:** Infer types everywhere annotations are omitted. Make generics real:
monomorphize them, emit correct LLVM IR per instantiation.

**Why this version exists:**
- Currently no type inference exists. Every inferred-type `let x = expr` has unknown type.
- Generic functions/structs (`func map<T, U>(...)`) are parsed but never monomorphized;
  the backend emits one LLVM function regardless of type arguments.
- Rustc uses Hindley-Milner + subtyping + trait bounds. Thagc needs HM + trait dispatch.

**Deliverables:**

### 1. Unification engine
```cpp
class Unifier {
  std::vector<std::optional<Ty>> table;   // TyVar id → solution
  TyVar fresh();
  void unify(Ty a, Ty b, Span);           // Robinson unification; emits Diag on fail
  Ty apply(Ty) const;                     // substitute solved vars
};
```

### 2. Constraint generation
Walk HIR. For each `let x = e` without annotation: create `TyVar v`, infer type of `e`,
unify `v = inferred`. For generic calls: instantiate type params as fresh `TyVar`s,
unify each argument type with the instantiated parameter type.

### 3. Trait system
```cpp
struct TraitDef { DefId id; std::vector<TraitMethod> methods; };
struct ImplBlock { DefId trait_id; Ty self_ty; std::vector<ImplMethod> methods; };
class TraitSolver { ImplBlock* find_impl(DefId trait, Ty for_ty); };
```
Builtin traits: `Display`, `Debug`, `Add`, `Sub`, `Mul`, `Div`, `Eq`, `Ord`,
`Clone`, `Iterator`, `Into`, `From`.

### 4. Monomorphization
After inference: collect all generic instantiations (e.g. `map<i32, str>`).
For each unique set of type args, clone the HIR subtree with TyVars substituted,
assign a mangled name (`map__i32__str`), lower to LLVM IR separately.

### 5. Standard-library generics
`std/list.tg`: `List<T>` backed by LLVM array allocation, monomorphized per `T`.
`lib/map.tg`: `Map<K,V>` using hash map, monomorphized.

**Build gate:** `cmake --build build` clean. `tests/inference/` and `tests/generics/`
suites pass. Generic stdlib types usable from user code.

**Release gate:** tag `v2.3.0`. Changelog: "Hindley-Milner type inference, trait system,
monomorphized generics."

---

## v2.4 — MIR & Borrow Checker

**Theme:** Introduce a Mid-level IR. Implement ownership tracking, move semantics,
and borrow checking. This is the deepest and most complex version.

**Why this version exists:**
- No ownership model exists. Variables are never moved, values are freely aliased.
  Thagore's language spec promises memory safety without GC — this version delivers it.
- Rustc's MIR is a CFG of basic blocks with explicit `Move`/`Copy`/`Ref` operations.
  The borrow checker (NLL — Non-Lexical Lifetimes) runs on MIR.

**Deliverables:**

### 1. MIR definition
```cpp
// compiler/include/thagc/mir/mir.hpp
struct MirLocal  { uint32_t id; Ty ty; bool is_mut; };
struct MirPlace  { MirLocal base; std::vector<PlaceElem> projection; };
enum class MirRvalueKind { Use, Ref, MutRef, BinaryOp, UnaryOp, Aggregate, Discriminant };
struct MirStatement { MirPlace lhs; MirRvalue rhs; Span span; };
struct MirTerminator { /* Goto, Return, Call, SwitchInt, Drop */ };
struct MirBasicBlock { std::vector<MirStatement> stmts; MirTerminator term; };
struct MirBody { std::vector<MirLocal> locals; std::vector<MirBasicBlock> blocks; };
```

### 2. HIR → MIR lowering
- All expressions flatten into temporaries.
- `if`/`match` → `SwitchInt` terminator.
- `for`/`while` → back-edge CFG.
- `func` calls → `Call` terminator with return destination.
- Explicit `Drop` terminators inserted at end-of-scope for owned values.

### 3. Move analysis
Track `MoveData`: for each `MirPlace`, record where it is initialized, moved, or dropped.
Detect use-after-move: walk CFG, if a place is read after a `Move` on any path → error.
Detect double-move: if `Drop` is reached after `Move` → error.

### 4. Borrow checker (NLL)
Compute live ranges for borrows. A borrow `&x` is live from its creation until its last
use. Check that the borrowed place is not mutated or moved while the borrow is live.
Emit `Diag::Error` with dual-span annotations (where borrow begins, where conflict is).

### 5. Ownership annotations in language
`own T` — owned, moved on assignment.
`ref T` — immutable borrow (like `&T`).
`mut T` — mutable borrow (like `&mut T`).
These are already partially in the parser; this version makes them semantically enforced.

### 6. LLVM backend consumes MIR
Replace HIR-to-LLVM with MIR-to-LLVM. Each `MirBasicBlock` → LLVM `BasicBlock`.
`MirStatement` → `alloca`/`store`/`load`. `Call` terminator → `llvm::CallInst`.
`Drop` terminator → call to type's destructor (if any).

**Build gate:** `cmake --build build` clean. `tests/ownership/` suite passes.
Programs with use-after-move and double-borrow are rejected with clear errors.
Valid programs compile and run correctly.

**Release gate:** tag `v2.4.0`. Changelog: "MIR, NLL borrow checker, move semantics,
ownership enforced at compile time — no GC, no undefined behavior."

---

## v2.5 — Production Complete

**Theme:** Query-based incremental compilation. Rich diagnostics. Self-hosting.
This version reaches rustc-level production completeness.

**Milestone definition:** Thagore v2.5 is production-complete when:
1. `thagc` can compile itself (self-hosting bootstrap replaces the seed binary)
2. Incremental compilation: unchanged files are not re-compiled across builds
3. Diagnostics are rich (multi-span, suggestions, error codes, `--explain`)
4. LSP server (`thagc lsp`) provides hover types, go-to-definition, inline errors
5. All stdlib modules pass the borrow checker and compile without warnings
6. `thagc --check` on a 50 kloc project completes in < 2 seconds (query caching)

**Deliverables:**

### 1. Query engine
```cpp
// compiler/include/thagc/query/query.hpp
template<typename K, typename V>
class QueryCache {
  std::unordered_map<K, V> results;
  std::unordered_map<K, uint64_t> dep_hash;
public:
  std::optional<V> get(const K& key, uint64_t input_hash) const;
  void put(const K& key, V value, uint64_t input_hash);
};
```
Named queries: `parse_file`, `name_resolve`, `type_check_fn`, `borrow_check_fn`,
`monomorphize`, `codegen_fn`, `link`. Each query stores its result keyed by
(file_id, content_hash). On re-build, only queries with changed inputs re-run.

### 2. Parallel compilation
Use a thread pool (std::jthread, C++20). Independent functions are type-checked and
code-generated in parallel. Dependency graph prevents data races.

### 3. Rich diagnostics
Each `Diag` supports:
- Multiple labeled spans (`label: "expected i32 here"`, `label: "found str here"`)
- A `help:` suggestion string (optionally machine-applicable)
- An error code (`E0001`–`E9999`) with `thagc --explain E0042` printing the full doc
- Rendered with color (ANSI) and `~~~` underlines like rustc

### 4. LSP server
`thagc lsp` speaks Language Server Protocol over stdin/stdout.
Implements: `textDocument/hover` (show inferred type), `textDocument/definition`
(jump to `DefId` span), `textDocument/diagnostics` (live errors), `textDocument/completion`
(method/field suggestions from type).

### 5. Self-hosting
`src/thagc.tg` (or a new `src/compiler/` directory) contains the compiler written in
Thagore itself. The C++ `thagc` compiles it. The resulting binary is used as the new
`thagc` in CI. The bootstrap chain becomes:
```
stage1_helper (C++ binary, seed) → thagc.tg → thagc_tg_binary → thagc.tg (verify)
```
Self-hosting gate: the Thagore-compiled `thagc` must produce identical output to the
C++-compiled `thagc` on all test inputs.

### 6. Stabilize stdlib
All modules in `stdlib/` pass borrow checker. Public APIs frozen. Semver guarantees apply.
`std/core.tg`, `std/string.tg`, `std/list.tg` are `#[stable]`.

### 7. Toolchain packaging
`thagup` installs a self-contained toolchain: `thagc`, `thag` (runner), stdlib, LSP.
`thagore.toml` project manifest: dependencies, edition, target triple.
`thagc build` resolves dependencies, caches compiled artifacts in `~/.thagore/cache/`.

**Build gate:** `cmake --build build` clean from scratch in < 5 minutes on modern hardware.
Self-hosting test passes. LSP server starts and responds to hover requests.
Incremental build of unchanged project: 0 files recompiled.

**Release gate:** tag `v2.5.0` as `stable`. Binaries for Linux x86_64, Linux aarch64,
macOS x86_64, macOS arm64, Windows x64. Changelog: "production-complete — self-hosting,
incremental compilation, borrow checker, LSP, rich diagnostics."

---

## Summary: v1.8 → v2.5 Upgrade Path

| Version | Theme | Build Gate | Release Gate |
|---------|-------|------------|--------------|
| v1.9 | Span system + token pipeline | ✅ hello.tg compiles, caret diagnostics | tag + binaries |
| v2.0 | Real recursive-descent parser | ✅ full test suite, no regressions | tag + binaries |
| v2.1 | Symbol table & name resolution | ✅ name_resolution/ tests pass | tag + binaries |
| v2.2 | HIR & bidirectional type checking | ✅ typeck/ tests, typed HIR in backend | tag + binaries |
| v2.3 | HM inference & generics | ✅ inference/ + generics/ tests | tag + binaries |
| v2.4 | MIR & borrow checker | ✅ ownership/ tests, borrow errors | tag + binaries |
| v2.5 | **Production complete** | ✅ self-hosting, LSP, incremental | stable release |

Each version is independently buildable (`cmake --build` → 0 errors) and independently
releasable (GitHub release with Linux + macOS binaries, changelog, passing CI).
No version may break any capability shipped in a prior version.

---

*"Thagore was built because no language made learning algorithms fun,
no language was both powerful and simple,
and no language let you go from first line to production without fighting it.
We're fixing that — for everyone."*
