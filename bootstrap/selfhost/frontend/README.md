# Self-Host Frontend Slice

This directory is the first extraction target from `tests/bootstrap_seed/`.

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

Current scope:

- token scan
- parser-lite summary
- symbol collection
- import resolution
- diagnostics-lite and semantic-lite used by the bootstrap seed
- module-kind-aware analysis for executable roots vs library modules
- implicit `main` synthesis for executable roots with top-level statements
- narrow static return-type inference for single-line funcs without `->`
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
  `tests/selfhost_frontend/differential_corpus.txt` and
  `tests/selfhost_frontend/stage_chain_corpus.txt` instead of being buried
  inside Rust test code
- that lane now also rebuilds `scan.tg`, `parse.tg`, and `check.tg` directly
  with host `thagc` and validates them through a standalone CI runner script,
  so the gate no longer depends only on Rust test harness code
- the lane now rebuilds those stages twice and compares emitted corpus reports,
  giving a first deterministic `stage0 -> selfhost stage -> rebuilt stage`
  style confidence check for the frontend slice
- `.github/workflows/selfhost-frontend-stage.yml` is now the dedicated
  first-class workflow for this slice instead of relying only on the broader
  bootstrap probe workflow
- `.github/workflows/selfhost-frontend-replacement.yml` now treats Target 01 as
  an explicit replacement trial against host `thagc check`, not just a
  stage-quality smoke gate
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
- that workflow now runs on `indev-rewrite` as well, cancels superseded runs,
  and publishes both stage reports into the job summary for faster contract
  review

Boundary:

- this is reusable self-host bootstrap code
- `tests/bootstrap_seed/` remains the harness and fixture corpus
- top-layer bootstrap sugar is not implemented here yet

Near-term next steps:

1. widen the differential corpus beyond narrow diagnostic labels
2. add normalized frontend output parity beyond desugared source dumps
3. widen return-type inference beyond literal-only cases when diagnostics remain fail-fast
