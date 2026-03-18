# Bootstrap Plan — v0.9.7

Goal: ship a self-hosting, resource‑efficient toolchain that can recompile itself on Linux/Windows/macOS with consistent outputs, tighter CI gates, and refreshed installer/docs.

## Readiness checklist
- [ ] Self-hosting: build `thagc` with `thagc` on all three platforms; compare hash with host-built binary.
- [ ] Parser/block rules: add explicit errors for indent/block detection; expand golden tests for control-flow + flow/intent constructs.
- [ ] Type system hardening: forbid unsupported generics/beta features; add conformance tests for method calls, intent, flow resumption.
- [ ] Stdlib parity: align interpreter vs compiled for `std.io`, `time`, `fs`, `path`, `string`; add dual-run snapshot tests.
- [ ] Time API: provide `now()`, monotonic `tick()`, `sleep(ms)`; cross-platform tests.
- [ ] Performance: profile interpreter on fib/io/loop workloads; reduce peak RAM; record before/after metrics in CI.
- [ ] Bootstrap probe v2: include self-host rebuild + output match, stricter time/RAM limits, artifact diff.
- [ ] Installer/Drago: bundle drago by default, retry/rustup mirrors, MSVC detection + optional auto-install, custom install path with size estimate, purge old thagc from PATH.
- [ ] Docs/website/playground: update install links, quick-start, indev warning, wasm/playground sync with new stdlib, Windows `.exe` suffix fix.
- [ ] Release rehearsal: nightly rehearsal job from `v0.9.7-pre`; final `release` workflow green across matrix.

## Metrics to gate release
- Self-host hash match across platforms.
- Probe time & peak RAM caps: check ≤45s build, ≤20s run, peak RAM ≤512MB (Linux) / ≤768MB (Windows).
- Interpreter benchmarks: target ≥20% speedup vs v0.9.6 baseline on fib(35) + tight loop; document results.
- Installer success rate: end-to-end on clean Windows VM + macOS + Ubuntu.

## Timeline (suggested)
- Week 1: parser/type hardening, stdlib parity, baseline benchmarks.
- Week 2: perf optimizations, time API, bootstrap probe v2, self-host pipeline.
- Week 3: installer polish, docs/playground refresh, rehearsal + tag `v0.9.7-rc`, final release.

## Owners (fill in)
- Compiler/TS: _________________
- Runtime/Stdlib: _______________
- Installer/CI: _________________
- Docs/Web/Playground: __________
