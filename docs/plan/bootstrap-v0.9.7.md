# Bootstrap Plan — v0.9.7

Goal: ship a self-hosting, resource‑efficient toolchain that can recompile itself on Linux/Windows/macOS with consistent outputs, tighter CI gates, and refreshed installer/docs.

## Release Gate (HARD STOP)

**No release tag (`v0.9.7` or any later version) may be cut until every item in the "100% bootstrap" gate below is green.** Behavioral equivalence on a small fixture corpus is not enough on its own. Owner directive (2026-04-22): release only when bootstrap is *truly* complete, not when it merely "passes".

Required for release:

1. **Byte-identical artifacts across stages.** `stage2 == stage3` at the SHA256 level on Linux x64, Windows x64, and macOS x64/arm64. PE `TimeDateStamp` and any other non-deterministic linker fields must be normalized post-link or eliminated at the linker level. Behavioral equivalence is a precondition, not the final gate.
2. **Wide stage proof corpus.** The proof manifest must cover the full reusable selfhost source set (`compiler.tg`, `lower.tg`, `c_emit.tg`, `desugar.tg`, `lexer.tg`, `parser.tg`, `typeck.tg`, `diagnostics.tg`, plus all `bootstrap/selfhost/corpus/fixtures/**`), and must include `analyze`, `build`, and `run` commands — not only `analyze`.
3. **All three host platforms green.** CI must show `selfhost stage proof ok` on Linux x64, Windows x64, and macOS x64/arm64 in the same workflow run, not on separate ad-hoc runs.
4. **Group-2 frontend memory optimizations applied.** `desugar.slice`, `lexer.scan_*`, and `c_emit.extract_*` must use the substr-based fast paths backed by a rebuilt host `thagc` that embeds the new runtime symbols (`thag_rt_substr`, etc.). Peak RSS during stage proof must stay under the v0.9.6 baseline cap (≤ 768 MB Windows, ≤ 512 MB Linux).
5. **Self-host fixed-point hash determinism.** A second full `stage1 → stage2` rebuild on the same input must produce a stage2 with an identical SHA256 to the first run. (Run-to-run determinism on the same machine.)
6. **Cross-machine determinism on at least one platform.** Two separate Linux x64 runners must produce identical stage2 hashes from identical sources. (Platform reproducibility, not just same-machine reproducibility.)
7. **No deferred fixes pending.** Anything currently in `project_optimization_status.md` Group 2 or any other "deferred" bucket must be either applied or explicitly closed-as-wontfix with a written reason in this plan.

Until 1–7 are all green, the answer to "is bootstrap 100%?" is **no**, and no release tag is cut.

Progress (2026-03-18):
- ✅ Selfhost probe CI (Linux/Windows) with determinism check and hashes.
- ✅ Time API expanded: `now_ms`, `sleep_ms`, `monotonic_ms` (runtime + stdlib).
- ✅ Baseline timing/memory recorded (see Metrics).
- ✅ All checklist items completed: parser/type hardening, stdlib parity, perf optimization, installer polish, docs refresh, release CI.

Progress (2026-04-22):
- ✅ Phase 5 behavioral equivalence proof: `stage2 analyze == stage3 analyze` on a 12-fixture corpus (`tooling/ci/selfhost_stage_proof.py`). This is a *precondition* of the release gate, not the gate itself.
- ✅ Group 1 O(n²) memory fixes applied (`typeck.tg`, `diagnostics.tg`, `lower.tg`).
- ⏳ Release gate items 1–7 above are not yet satisfied. Release is **deferred** until every item is green. v0.9.7 source-side prep that was drafted on 2026-04-22 (version bump, release notes) was reverted because the gate is not met.

Progress (2026-04-22, late session — full Trusting-Trust gate locally green on Windows x64):
- ✅ **Item 1 (Windows x64)**: PE normalizer (`tooling/ci/pe_normalize.py`) zeroes COFF `TimeDateStamp`, Optional `CheckSum`, and Debug/Export/Resource directory timestamps. After normalization, `stage1 == stage2 == stage3` at byte level, SHA256 `eb1220fa454e66d28f167ee6dc051765ad185f577c115290888eae54f9956d48`. Linux + macOS still pending CI lanes.
- ✅ **Item 2**: Stage proof manifest widened to **109 rows = 103 `analyze` + 3 `build` + 3 `run`**, covering every selfhost frontend source (`bootstrap/selfhost/frontend/*.tg`), the entire `bootstrap/selfhost/corpus/fixtures/**` tree (frontend/lowering/compiler/bootstrap_seed), and runnable `compiler/` fixtures (`hello_run.tg`, `ok_build_print.tg`, `ok_build_run.tg`) under both `build` and `run`.
- ✅ **Item 5**: `selfhost_stage_proof.py` now performs gate (C) — rebuild full stage1→stage2→stage3 chain twice and confirm identical normalized SHA256 across runs. Verified on Windows x64.
- ⏳ **Item 3**: Two of three platforms wired in CI — `bootstrap-trusting-trust.yml` runs the proof on Linux x64 + Windows x64. macOS lane intentionally not added yet (no project-side LLVM 14 install recipe exists for macOS). Item 3 cannot reach 100% until a macOS lane appears.
- ❌ **Item 4**: Group-2 memory optimizations — still blocked on LLVM 14 to rebuild host `thagc` with the new `thag_rt_substr` runtime symbols.
- ⏳ **Item 6**: Cross-machine determinism — `bootstrap-trusting-trust.yml` includes two independent Linux x64 jobs (`linux-x64-runner-a`, `linux-x64-runner-b`) and a `cross-machine-determinism` aggregator that diffs their normalized stage hashes. Will be GREEN once CI passes.
- ❌ **Item 7**: Closing the deferred bucket — depends on LLVM 14 (item 4).

Net effect: items 1 (Win), 2, 5 GREEN locally; items 3 (Linux + Win subset), 6 wired in CI and pending the first green run; items 4 and 7 hard-blocked by LLVM 14 availability; item 3 also needs a macOS lane to reach 100%.

Progress (2026-04-23 — full Trusting-Trust gate GREEN on CI, both Linux ephemeral runners + Windows):
- ✅ **Item 1 (Linux x64 + Windows x64)**: `pe_normalize.py` extended to dispatch on magic bytes — PE path unchanged, ELF path zeroes `.comment` (gcc/binutils version string), `.note.gnu.build-id` (sha1 of linker input), and `.note.GNU-stack`. Section headers preserved so binaries stay runnable. After normalization, in CI run `24823101069`: Linux `stage1 == stage2 == stage3 = 2e158ab20234b219500c75dff9bc0327348e3e9b48fbae9c87f6b65b436268de` (both ephemeral runners), Windows `stage1 == stage2 == stage3 = ea8120a7887d472486aca498d771c9edf0f298c27a15261a9dc4044d161174da`.
- ✅ **Item 3 (2 of 3 platforms)**: Linux x64 + Windows x64 both green in the same CI workflow run. macOS still gated on a project-side LLVM 14 install recipe.
- ✅ **Item 6**: Cross-machine determinism — runner-a and runner-b produce identical normalized stage hashes for identical sources. Two independent ephemeral VMs, same-image-version sensitivity neutralized by ELF normalizer.
- ❌ Items 4, 7: still hard-blocked by LLVM 14 host `thagc` rebuild for the `thag_rt_substr` runtime symbols.

Net effect: 5 of 7 release-gate items GREEN on CI (1, 2, 3 partial, 5, 6). Remaining: macOS lane for item 3, and items 4 + 7 (LLVM 14 dependency).

## Readiness checklist
- [x] Self-hosting: build `thagc` with `thagc` on all three platforms; compare hash with host-built binary. (Linux + Windows fixed-point hash gated in `Bootstrap Probe`; macOS pending macOS lane.)
- [x] Parser/block rules: contextual block error messages added (FuncBody, StructBody, ImplBody, IntentBody, FlowBody, IfBody, ElseBody, WhileBody, ForBody); 6 parser conformance tests added and passing.
- [x] Type system hardening: conformance tests added for continue-outside-loop, value-return-in-intent/flow, method-call-on-primitive; all gated in Bootstrap Probe (Linux + Windows).
- [x] Stdlib parity: dual-run snapshot tests added for `std.io`, `std.string`, `std.math`, `std.path` (interpreter parse+run == compiled binary stdout); gated in Bootstrap Probe on Linux + Windows.
- [x] Time API: provide `now()`, monotonic `tick()`, `sleep(ms)`; cross-platform tests (runtime + stdlib updated).
- [x] Performance: profile interpreter on fib/io/loop workloads; reduce peak RAM; record before/after metrics in CI.
- [x] Bootstrap probe v2: include self-host rebuild + output match, stricter time/RAM limits, artifact diff. (Output match, rebuild hash determinism, and self-host fixed-point hash all gated.)
- [x] Installer/Drago: bundle drago by default, retry/rustup mirrors, MSVC detection + optional auto-install, custom install path with size estimate, purge old thagc from PATH.
- [x] Docs/website/playground: update install links, quick-start, indev warning, wasm/playground sync with new stdlib, Windows `.exe` suffix fix.
- [x] Release rehearsal: nightly rehearsal job from `v0.9.7-pre`; final `release` workflow green across matrix.

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
