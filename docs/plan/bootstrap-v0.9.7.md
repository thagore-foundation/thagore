# Bootstrap Plan — v0.9.7

Goal: ship a self-hosting, resource‑efficient toolchain that can recompile itself on Linux/Windows/macOS with consistent outputs, tighter CI gates, and refreshed installer/docs.

Progress (2026-03-18):
- ✅ Selfhost probe CI (Linux/Windows) with determinism check and hashes.
- ✅ Time API expanded: `now_ms`, `sleep_ms`, `monotonic_ms` (runtime + stdlib).
- ✅ Baseline timing/memory recorded (see Metrics).
- ⏳ Parser/type hardening, stdlib parity audits, perf targets, installer polish, docs refresh.

## Readiness checklist
- [x] Self-hosting: build `thagc` with `thagc` on all three platforms; compare hash with host-built binary. (Linux + Windows fixed-point hash gated in `Bootstrap Probe`; macOS pending macOS lane.)
- [x] Parser/block rules: contextual block error messages added (FuncBody, StructBody, ImplBody, IntentBody, FlowBody, IfBody, ElseBody, WhileBody, ForBody); 6 parser conformance tests added and passing.
- [x] Type system hardening: conformance tests added for continue-outside-loop, value-return-in-intent/flow, method-call-on-primitive; all gated in Bootstrap Probe (Linux + Windows).
- [x] Stdlib parity: dual-run snapshot tests added for `std.io`, `std.string`, `std.math`, `std.path` (interpreter parse+run == compiled binary stdout); gated in Bootstrap Probe on Linux + Windows.
- [x] Time API: provide `now()`, monotonic `tick()`, `sleep(ms)`; cross-platform tests (runtime + stdlib updated).
- [ ] Performance: profile interpreter on fib/io/loop workloads; reduce peak RAM; record before/after metrics in CI.
- [x] Bootstrap probe v2: include self-host rebuild + output match, stricter time/RAM limits, artifact diff. (Output match, rebuild hash determinism, and self-host fixed-point hash all gated.)
- [ ] Installer/Drago: bundle drago by default, retry/rustup mirrors, MSVC detection + optional auto-install, custom install path with size estimate, purge old thagc from PATH.
- [ ] Docs/website/playground: update install links, quick-start, indev warning, wasm/playground sync with new stdlib, Windows `.exe` suffix fix.
- [ ] Release rehearsal: nightly rehearsal job from `v0.9.7-pre`; final `release` workflow green across matrix.

## Metrics to gate release
- Self-host hash match across platforms.
- Probe time & peak RAM caps: check ≤45s build, ≤20s run, peak RAM ≤512MB (Linux) / ≤768MB (Windows).
- Interpreter benchmarks: target ≥20% speedup vs v0.9.6 baseline on fib(35) + tight loop; document results.
- Installer success rate: end-to-end on clean Windows VM + macOS + Ubuntu.

### Baseline (2026-03-18, run 23240082537)
- Linux selfhost build: 33.30s, peak ~558 MB; hash `421baefa...e2136`.
- Linux probe run: 0.52s, peak ~63 MB.
- Windows selfhost build: ~64.1s (timed via Measure-Command); hash `746BDB0C...B7EDE`.
- Windows probe run: ~0.79s (`run.metrics`).

## Timeline (suggested)
- Week 1: parser/type hardening, stdlib parity, baseline benchmarks.
- Week 2: perf optimizations, time API, bootstrap probe v2, self-host pipeline.
- Week 3: installer polish, docs/playground refresh, rehearsal + tag `v0.9.7-rc`, final release.

## Owners
- Compiler/TS: ducknogit (lead), support: typecheck team
- Runtime/Stdlib: ngocuyensie (lead), support: stdlib maintainers
- Installer/CI: release-engineering crew (owner: ops-bot + ducknogit)
- Docs/Web/Playground: docs-team (lead: content-maintainer), support: frontend-volunteers
