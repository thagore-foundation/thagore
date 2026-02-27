# Thagore Roadmap — v0.1 → v1.8 Stable

Current effective milestone by code audit (February 27, 2026): `v0.4` (Module & Import System).
Status note: tag `v0.6.0` exists, but concurrency work is currently treated as prototype track until compiler gates (`v0.2` → `v0.5`) are passed.
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

**Overall: compiler gate `v0.4` đã pass trên Linux (multi-file import + package import compile/run).**

Execution order (re-anchored to dependency reality):
1. Close `v0.2` gate (compiler foundation)
2. Complete `v0.3` (struct/type system)
3. Complete `v0.4` (module/import pipeline)
4. Complete `v0.5` (language completeness)
5. Resume and harden concurrency track (`v0.6+`)

---

## v0.2 — Compiler Foundation 🚧 In Progress (~60%)
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
- [ ] Chương trình có nhiều functions, float, bool compile và chạy đúng trên Linux/macOS/Windows

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

## v0.5 — Language Features Complete 🔲 Planned (dependency on v0.4 gate)
> *"Ngôn ngữ đủ để viết chương trình thật"*

### Compiler — Language Features
- [ ] Closures — capture, function-value type, emit đúng trong LLVM
- [ ] `defer` stack — defer-stack mechanism trong backend, đúng execution order
- [ ] Interpolated strings — `"Hello {name}!"` compile thành string concat
- [ ] `Result<T, E>` / `Option<T>` — built-in sum types, không cần generic đầy đủ
- [ ] `?` operator — early return cho Result/Option
- [ ] Tuple type — `(i32, string)`, tuple destructuring
- [ ] Array/slice literals — `[1, 2, 3]`, index access `arr[i]`
- [ ] Loop labels + labeled `break`/`continue`
- [ ] `pub` visibility enforcement — cross-module access control

### Type System
- [ ] Generic types cơ bản — `List<T>`, `Option<T>`, `Result<T, E>`
- [ ] Function types — `fn(i32) -> string`
- [ ] User-defined type validation — struct field types, trait method signatures

### Gate
- [ ] Có thể viết một CLI tool hoàn chỉnh bằng Thagore (sort, search, transform data)

---

## v0.6 — Concurrency Primitives (Prototype Track) ⚠️ Out-of-order
> *"Track này đã có prototype, nhưng chưa thể release trước khi pass `v0.2` → `v0.5` gates."*

### Runtime
- [x] Task scope/nursery/cancel/timeout APIs ở mức prototype
- [ ] Async scheduler event loop thay thế thread-per-task
- [ ] epoll/kqueue integration cho Linux/macOS
- [ ] Platform layer production (không còn stub)

### Compiler
- [ ] `Rc<T>` codegen — single-thread reference counting
- [ ] `Arc<T>` codegen — atomic reference counting
- [ ] `Send`/`Sync` auto-check thật sự trong typechecker
- [ ] Diagnostic `E_SEND_SYNC_004` với fix hint "use `Arc` instead of `Rc`"

### Tests
- [ ] Child tasks cannot silently leak outside scope
- [ ] Cancel propagation deterministic under stress

### Gate
- [ ] Structured concurrency contracts pass — scope/cancel/timeout behavior deterministic

---

## v0.7 — Structured Concurrency by Default (Beta)
> *"Concurrent code an toàn như single-thread code"*

### Runtime
- [ ] Timeout API và propagation semantics
- [ ] Scheduler fairness — starvation protection
- [ ] Backpressure mechanism
- [ ] First deadlock/race regression suite

### Language
- [ ] `async`/`await` syntax hoặc equivalent Thagore construct
- [ ] Task cancellation propagation qua IO boundaries
- [ ] Cancellation check trong HTTP/WS/DB calls

### Docs
- [ ] Concurrency debugging playbook
- [ ] Structured concurrency guide cho beginners

### Gate
- [ ] Scope/cancel/timeout behavior deterministic dưới stress tests 20-iteration soak

---

## v0.8 — Memory Model MVP
> *"Compiler lo memory — mày lo logic"*

### Compiler
- [ ] Automatic `Send`/`Sync` validation trong typechecker và middleend
- [ ] Reject `Rc` across thread boundaries với actionable diagnostic
- [ ] Suggest `Arc` khi `Rc` Send/Sync fail
- [ ] Memory model compliance cases trong contracts

### Runtime
- [ ] `platform_posix.cpp` implementation đầy đủ (không còn 4-line stub)
- [ ] `platform_windows.cpp` implementation đầy đủ
- [ ] `thag_runtime.cpp` — init, shutdown, memory tracking thật sự

### Gate
- [ ] Invalid cross-thread `Rc` usage bị reject tại compile time với actionable diagnostics

---

## v0.9 — IO Stack Alpha
> *"HTTP, WebSocket, DB — single binary, no dependencies"*

### Runtime
- [ ] `thag_http_get/post` — real HTTP client (libcurl hoặc native)
- [ ] `ws_connect/send/close` — real WebSocket implementation
- [ ] `db_connect/query/close` — real DB client (SQLite ít nhất)
- [ ] Cancellation/timeout propagate qua IO boundaries
- [ ] Async scheduler ổn định với IO integration

### Stdlib
- [ ] `lib/http.tg` — production-ready HTTP client wrapper
- [ ] `lib/ws.tg` — WebSocket wrapper với error handling
- [ ] `lib/db.tg` — DB wrapper với query builder cơ bản
- [ ] `lib/time.tg` — `now()`, `sleep()`, duration types
- [ ] `lib/map.tg` — HashMap thật sự, không phải stub
- [ ] `std/string.tg` — đầy đủ: format, split, join, parse
- [ ] `std/list.tg` — append, remove, sort, filter, map, reduce
- [ ] `std/core.tg` — error types, Result/Option helpers

### Gate
- [ ] End-to-end async IO path pass Linux parity contracts

---

## v1.0 — Deploy Baseline
> *"Single binary. One command. Ship anywhere."*

### Compiler & Tooling
- [ ] Single-binary default — không cần runtime dependency
- [ ] One-command cross-compile: `thagc build --target aarch64-linux`
- [ ] Cold-start budget enforced trong CI (< 10ms target)
- [ ] Binary-size baseline trong CI
- [ ] `thagc target` + `thagc build` recipes đầy đủ documented

### Drago Package Registry (Production)
- [ ] Registry stable, versioned packages
- [ ] `thagc install/update/remove` hoàn chỉnh
- [ ] Package publishing workflow
- [ ] Dependency resolution + lock file

### FFI
- [ ] `extern` C function calls hoạt động đầy đủ
- [ ] C library linking trong build pipeline
- [ ] FFI safety guidelines documented

### Docs
- [ ] Getting started guide — từ install đến hello world trong 60 giây
- [ ] Beginner tutorial — học giải thuật bằng Thagore
- [ ] API reference cơ bản

### Gate
- [ ] Một CLI tool thực tế được build, cross-compiled, và distributed dưới dạng single binary

---

## v1.1 — Structured Concurrency GA
> *"Concurrent code in production — zero surprises"*

### Runtime
- [ ] Soak tests cho long-running scoped workloads (1h+)
- [ ] Task tree diagnosis / tracing hooks
- [ ] No open P0/P1 bugs cho scope/cancel/timeout/nursery
- [ ] Deadlock detection với helpful error message

### Language
- [ ] `flow` construct MVP — `flow`/`step`/`undo`/`retry`/`timeout`/`idempotent` keywords
- [ ] Flow compile-time validation — undo/retry semantics enforced

### Gate
- [ ] No open P0/P1 concurrency bugs, soak tests stable

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
- [ ] `lib/crypto.tg` — hash, HMAC cơ bản (FFI vào libcrypto)

### Gate
- [ ] HTTP/WebSocket/DB lanes pass trên Linux/macOS/Windows trong CI

---

## v1.3 — Performance Lockdown
> *"Fast enough to replace Go. Simple enough to teach beginners."*

### Compiler
- [ ] LLVM optimization passes được enable đúng cách
- [ ] Codegen cho tight loops không có overhead
- [ ] Inlining hints cho small functions

### Tooling
- [ ] Benchmark automation — `tooling/bench/`
- [ ] Performance threshold alerts trong CI
- [ ] p95 latency và startup metrics tracked per commit

### AI/ML Foundation (Preview)
- [ ] FFI bindings cho CUDA/OpenCL basic
- [ ] `lib/tensor.tg` stub — groundwork cho AI use case
- [ ] PyTorch interop proof of concept (call C++ kernel từ Thagore)

### Gate
- [ ] p95 latency và startup metrics đạt release budgets
- [ ] Benchmark so sánh với Go, Rust, Python được publish

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
| v0.1 | Baseline rewrite | ✅ Completed (infrastructure baseline) |
| v0.2 | Compiler foundation | 🚧 In progress (~60% by audit) |
| v0.3 | Struct & type system | ✅ Completed (gate passed on Linux) |
| v0.4 | Module & import system | 🔲 Planned |
| v0.5 | Language features complete | 🔲 Planned |
| v0.6 | Concurrency alpha | ⚠️ Prototype track (out-of-order, not releasable) |
| v0.7 | Structured concurrency beta | 🔲 Planned |
| v0.8 | Memory model MVP | 🔲 Planned |
| v0.9 | IO stack alpha | 🔲 Planned |
| v1.0 | Deploy baseline | 🔲 Planned |
| v1.1 | Concurrency GA | 🔲 Planned |
| v1.2 | IO stack GA | 🔲 Planned |
| v1.3 | Performance lockdown | 🔲 Planned |
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
