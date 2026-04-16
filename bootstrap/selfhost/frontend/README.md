# Self-Host Frontend Slice

This directory is the canonical selfhost frontend implementation owned under `bootstrap/selfhost/`.

Purpose:

- hold reusable Thagore frontend modules for self-host bootstrap
- keep compiler-like logic out of fixture glue
- become the source used by the first `stage0 -> stage1 -> stage2` bootstrap
  slice

Current contents:

- `chars.tg`
- `lexer.tg`
- `parser.tg`
- `symbols.tg`
- `resolver.tg`
- `diagnostics.tg`
- `semantics.tg`
- `driver.tg`
- `pipeline.tg`
- `report.tg`
- `session.tg`
- `scan.tg`
- `parse.tg`
- `main.tg`
- `compiler.tg`
- `adapter.tg`
- `lower.tg`

Current scope:

- token scan
- parser-lite summary
- symbol collection
- import resolution
- diagnostics-lite and semantic-lite used by the bootstrap seed
- module-kind-aware analysis for executable roots vs library modules
- implicit `main` synthesis for executable roots with top-level statements
- narrow static return-type inference for funcs without `->` when the return
  can be proven from literals, typed identifier bindings, or direct calls to
  known-return functions
- bootstrap-only sugar is now blocked in `selfhost-core` mode, so implicit
  `main` and omitted return annotations stay confined to the top authoring
  layer
- bootstrap-seed coverage now includes both `core-exe` and `core-library`
  negative cases for omitted return annotations
- primitive/simple-call assignment diagnostics and non-bool condition checks
- desugared dump mode for golden-testing synthesized core form
- report dump mode for golden-testing normalized frontend summaries
- bootstrap-seed report goldens now cover both library-mode and synthesized
  executable-root paths
- first narrow differential corpus against the Rust-hosted frontend, including
  call-arity, assignment/local/return type drift, assignment/local/return
  call-result drift, assignment-target drift, and call-site identifier drift
- `check.tg` is now the canonical selfhost frontend stage entry for
  semantic/report execution; `main.tg` remains the broader harness entry
- `report.tg` now owns deterministic frontend output assembly so `driver.tg`
  stays focused on pipeline orchestration
- `session.tg` now owns shared CLI/stage session plumbing so `scan.tg`,
  `parse.tg`, `check.tg`, and `main.tg` stop duplicating argument and source
  setup
- `pipeline.tg` now owns stage execution so `scan.tg`, `parse.tg`, and
  `check.tg` are thin entrypoints around stable selfhost pipeline calls
- `compiler.tg` is now the first selfhost compiler-driver slice, locking
  command dispatch for `help`, `version`, `analyze`, `check`, `report`,
  `desugar`, `scan`, `parse`, `build`, and `run` above the existing frontend
  pipeline; its hidden orchestration/adapter surfaces now also lock explicit
  `plan-*` and `adapter-*` contracts for `check/build/run`
- `adapter.tg` now owns the explicit selfhost-to-host backend adapter contract:
  phase routing, route status, artifact naming, capture naming, and host
  command rendering for `build` / `run`; actual `build` / `run` now emit
  sidecar plan/request artifacts from that contract
- `lower.tg` is now the first selfhost lowering slice, emitting a stable
  lowered-function summary for a narrow corpus so compiler-middle behavior
  starts getting its own contract; that contract now includes assignment
  paths, local bindings, call lowering, control-flow shape, and typed
  value/return flow
- `semantics.tg` now owns diagnostic composition/filtering so `pipeline.tg`
  stays focused on stage execution rather than policy decisions
- `parse.tg` is the first dedicated pre-check stage entry for token/summary/
  symbol/import output
- `scan.tg` is the first dedicated token-only stage entry and anchors the
  `stage0 -> scan -> parse -> check` chain gated in CI
- CI now gates that chain on both `ok` and failing fixtures so stage
  composition does not only look correct on the happy path
- `scan.tg` and `parse.tg` now both have golden coverage on failing fixtures,
  so pre-check stage output can drift independently from `check.tg` and still
  get caught
- CI now also runs a multi-fixture stage-chain corpus across executable success,
  executable failure, and library-mode inputs, which is the first stage wiring
  gate that looks like a reusable replacement target instead of a single smoke
  test
- bootstrap CI now gives selfhost frontend stages their own dedicated lane,
  separate from the broader stdlib/bootstrap probe lane, so stage regressions
  are visible as first-class failures
- replacement-target contracts now live in
  `bootstrap/selfhost/corpus/frontend-differential.txt` and
  `bootstrap/selfhost/corpus/frontend-stage-chain.txt` instead of being buried
  inside Rust test code
- the differential contract now carries explicit module-kind, so the
  replacement workflow can validate library fixtures as library inputs through
  the real Rust session path instead of relying on executable defaults
- that lane now also rebuilds `scan.tg`, `parse.tg`, and `check.tg` directly
  with host `thagc` and validates them through a standalone CI runner script,
  so the gate no longer depends only on Rust test harness code
- the lane now rebuilds those stages twice and compares emitted corpus reports,
  giving a first deterministic `stage0 -> selfhost stage -> rebuilt stage`
  style confidence check for the frontend slice
- `.github/workflows/selfhost-frontend-stage.yml` is now the dedicated
  first-class workflow for this slice instead of relying only on the broader
  bootstrap probe workflow
- `.github/workflows/selfhost-frontend-parse-target.yml` now gives Target 02
  (`parse.tg`) its own first-class CI boundary on Linux x64 and Windows x64,
  with first-pass vs second-pass report diffs on the parse contract instead of
  only the broader stage lane
- `.github/workflows/selfhost-frontend-replacement.yml` now treats Target 01 as
  an explicit replacement trial against host `thagc check`, not just a
  stage-quality smoke gate
- `.github/workflows/bootstrap-selfhost-stage.yml` now labels the same slice as
  `stage0 -> stage1 -> stage2` in CI, so bootstrap rehearsal can be tracked in
  stage terms instead of only stage-runner terminology
- `bootstrap/selfhost/tools/frontend-stage-manifest.txt` now declares the
  current stage slice membership explicitly, so builder scripts and workflows
  share one source of truth for `scan` / `parse` / `check`
- `bootstrap/selfhost/tools/frontend-driver-manifest.txt` now declares the
  higher executable driver boundary (`main.tg`) separately, so the top-level
  session/driver path can be hardened without coupling it to the lower stage
  slice
- `bootstrap/selfhost/tools/compiler-driver-manifest.txt` now declares the
  first compiler-driver executable boundary (`compiler.tg`) separately, so
  command orchestration can be hardened without coupling it to either the
  lower stage slice or the narrower `main.tg` frontend harness
- `bootstrap/selfhost/tools/frontend-lowering-manifest.txt` now declares the
  first lowering boundary (`lower.tg`) separately, so lowering behavior can be
  hardened without coupling it to command routing or the stage-entry slice
- `bootstrap/selfhost/corpus/frontend-analyze.txt` now locks full `analyze`
  output for the frontend differential corpus too, so the driver boundary is
  no longer limited to bootstrap-seed fixtures or report-mode-only frontend
  cases
- `bootstrap/selfhost/corpus/frontend-driver-orchestration.txt` now locks the
  session/driver orchestration surface itself: default sample fallback,
  relative vs absolute path routing, kind/mode fallback, core-kind routing,
  and missing-source exit behavior
- `bootstrap/selfhost/corpus/compiler-driver-contract.txt` now locks the
  higher compiler-driver command surface: help/version output, relative and
  absolute path routing, command dispatch, core-kind dispatch, build/run
  orchestration through host `thagc`, hidden `plan-*` orchestration reports,
  invalid-command fallback, and missing-source exits
- `bootstrap/selfhost/corpus/compiler-phase-contract.txt` now locks the first
  compiler phase-body surface separately, so `phase-check`, `phase-build`, and
  `phase-run` expose frontend/lowering/adapter/backend body boundaries instead
  of collapsing back into the thinner plan preview
- `bootstrap/selfhost/corpus/backend-adapter-contract.txt` now locks the
  explicit adapter boundary between the selfhost compiler slice and the host
  backend/codegen path, including hidden `emit-build` / `emit-run` previews
  that expose the narrowed emission handoff directly
- `bootstrap/selfhost/corpus/backend-adapter-artifacts.txt` now locks the
  real adapter sidecar artifacts emitted by `build` / `run`, including route
  changes for diagnostics-ok vs diagnostics-error paths plus the emitted
  lowered sidecar, emission sidecar, and normalized host-command sidecar that
  the backend adapter consumes; that contract now also proves a host-fallback
  build can emit a runnable artifact with stable output
- `bootstrap/selfhost/corpus/bootstrap-artifact-contract.txt` now locks a real
  bootstrap artifact loop: the selfhost compiler builds a rebuilt compiler
  artifact and a rebuilt frontend-main tool artifact, then CI runs both and
  compares their observable output
- that same artifact loop now also requires the rebuilt compiler artifact to
  build `main.tg` and replay a report-mode output, so the artifact chain is no
  longer limited to `compiler.tg` and `lower.tg`
- that same artifact loop now also requires the rebuilt compiler artifact to
  build `scan.tg`, `parse.tg`, and `check.tg`, and then requires the rebuilt
  compiler artifact to rebuild another compiler that can build `main.tg`,
  `check.tg`, and `lower.tg`, and then run nested `phase-check`,
  `plan-check`, `phase-build`, `phase-run`, `plan-build`, `plan-run`, `adapter-build`,
  `adapter-run`, full nested `build` and `run`, `emit-build`, `link-build`, and
  `verify-build` contracts from that rebuilt compiler, plus nested `emit-run`,
  `link-run`, and `verify-run` contracts, so the chain now reaches through the
  canonical frontend stages, lowering slice, and one deeper nested
  compiler/tool handoff into compiler-body, planning, adapter, full build/run,
  emission, link, and verification surfaces for both build and run flows
- that same nested chain now also locks the rebuilt compiler's emitted
  `.adapter.txt`, `.host.txt`, `.lowered.txt`, `.emit.txt`, `.link.txt`, and
  `.verify.txt` sidecars across both `build` and `run`, so the useful artifact
  chain covers not only final stdout/artifact behavior but also the
  selfhost-visible compiler-middle and backend-boundary reports emitted during
  nested execution
- that nested sidecar coverage now also includes a second observable executable
  path (`ok_build_print.tg`), so the rebuilt compiler chain locks not only the
  minimal return-only success path but also a stdout-producing build flow across
  lowered/adapter/emit/link/verify/host sidecars
- that same stdout-producing path is now locked for nested `run` sidecars too,
  so rebuilt-compiler execution with captured stdout is covered across lowered,
  adapter, emit, link, verify, and host summaries instead of stopping at the
  build-side artifact boundary
- that nested artifact loop now also locks a third useful executable path
  (`hello_run.tg`) across direct adapter artifacts plus rebuilt-compiler
  `build` / `run` sidecars, so the chain covers a real line-printing program
  instead of only the silent return path and the short inline `print(...)` path
- `bootstrap/selfhost/corpus/lowering-slice.txt` now locks the first lowering
  contract: constant returns, direct-call returns, local-load returns,
  assignment flow, control-flow shape, typed lowered operations, and explicit
  split between lowered `values`, `statements`, and `terminators` through
  `lower.tg`, including explicit block-to-block CFG edges
- `.github/workflows/bootstrap-selfhost-stage.yml` now diffs both the lower
  stage slice reports and the higher driver-boundary reports across stage1 and
  stage2, so the bootstrap rehearsal covers `main.tg` as well as
  `scan` / `parse` / `check`
- that same rehearsal now also diffs the first compiler-driver boundary
  (`compiler.tg`) across stage1 and stage2, so command-surface drift is part
  of the bootstrap loop rather than living only in a standalone workflow
- that same rehearsal now also diffs the explicit backend-adapter boundary
  across stage1 and stage2, so host-backend integration drift is visible in
  the bootstrap loop instead of being inferred indirectly from final artifacts
- that same rehearsal now also diffs the first lowering boundary (`lower.tg`)
  across stage1 and stage2, so narrowed lowered-shape drift becomes part of
  the bootstrap loop instead of living only in a standalone workflow
- `.github/workflows/selfhost-backend-adapter.yml` now gives the adapter
  contract its own Linux x64 and Windows x64 lane, separate from the broader
  compiler-driver lane, and that lane now validates actual sidecar artifacts
  emitted by `build` / `run`
- `.github/workflows/bootstrap-declaration-gate.yml` now reruns the compiler
  driver, compiler phase body, backend adapter, bootstrap artifact, and
  lowering reports twice on both Linux and Windows using the same built
  selfhost artifacts, so bootstrap declaration is now a first-class CI gate
  instead of a manual judgment call
- that rehearsal now also diffs rebuilt bootstrap-artifact reports across
  stage1 and stage2, so a tool built through the selfhost compiler path is
  exercised and stabilized inside the same deterministic loop
- that rehearsal now also diffs the session-routed replacement summary for
  `check.tg` across stage1 and stage2, so deterministic rebuild confidence
  includes the real Rust-path replacement hook rather than only standalone
  stage runners
- that replacement workflow now runs two rebuild passes and diffs the
  host-vs-selfhost summaries, so Target 01 is checked for deterministic
  replacement behavior instead of single-pass agreement only
- the replacement trial now routes through `tools/thagore-cli/src/session.rs`
  via hidden `thagc check` flags (`--selfhost-replacement-bin`,
  `--selfhost-replacement-manifest`, `--selfhost-replacement-strict`,
  `--selfhost-replacement-report-out`), with the older environment hook kept as
  fallback; the Rust `check_file(...)` surface itself participates in the
  comparison instead of only an external helper
- the replacement workflow now uploads both the external validator summary and
  the session-routed transcript, so contract drift in the real Rust path is
  visible directly in CI artifacts and job summaries
- the narrow replacement contract now includes an explicit library-mode success
  fixture, so Target 01 no longer measures only executable-root files
- the narrow replacement contract now also includes import-resolution success
  fixtures in both executable and library mode, widening confidence on module
  surface without jumping yet to import-error parity
- the narrow replacement contract now also includes an executable missing-import
  fixture, so Target 01 exercises a real module-surface failure and not only
  import success
- the narrow replacement contract now also includes an unresolved-imported-symbol
  fixture, extending Target 01 into imported-symbol semantics rather than only
  module-file existence
- both missing-import and unresolved-imported-symbol coverage now run in
  library mode as well, so Target 01 exercises module-surface failures across
  both module kinds
- the replacement contract now also includes import-alias success fixtures in
  both executable and library mode, widening module-surface coverage without
  assuming Rust-side alias-specific diagnostics that do not exist yet
- normalized selfhost report goldens now also cover module-surface fixtures
  (plain import success, missing import, unresolved imported symbol, alias
  success), so module behavior is not guarded only by coarse labels
- scan/parse goldens now also cover those module-surface fixtures, so import
  drift in early selfhost stages is caught before it collapses into
  check-stage-only behavior
- report/parse/scan goldens are now contract-driven through dedicated corpus
  manifests under `bootstrap/selfhost/corpus/`, which is the first small step
  toward treating these stages as replacement targets rather than ad hoc tests
- the standalone CI runner now validates those richer golden corpora directly,
  so stage-lane confidence no longer depends only on Rust-side test harness
- `docs/plan/selfhost-replacement-target.md` now names `parse.tg` as Target 02,
  which is the next stage-level boundary after the current `check_file(...)`
  replacement trial
- that workflow now runs on `indev-rewrite` as well, cancels superseded runs,
  and publishes both stage reports into the job summary for faster contract
  review

Boundary:

- this is reusable self-host bootstrap code
- bootstrap fixtures now live under `bootstrap/selfhost/corpus/fixtures/`
- top-layer bootstrap sugar is not implemented here yet

Near-term next steps:

1. widen the differential corpus beyond narrow diagnostic labels
2. add normalized frontend output parity beyond desugared source dumps
3. widen return-type inference beyond literal/identifier/direct-call cases when diagnostics remain fail-fast
