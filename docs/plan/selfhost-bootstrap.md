# Self-Host Bootstrap Plan

This document starts the next line after bootstrap-readiness rehearsal:
turning Thagore from a Rust-hosted compiler into a compiler that can build
meaningful parts of itself in Thagore and then sustain a rebuild loop.

The target here is not "some codegen written in Thagore". The target is a
controlled bootstrap chain with explicit stage boundaries, deterministic CI,
and rollback points when a stage regresses.

## 1. Bootstrap Objective

Self-host bootstrap begins only when all of the following become true:

- a Thagore-written compiler stage exists and builds with the Rust-hosted
  `thagc`
- that stage is exercised in CI on Windows x64 and Linux x64
- the stage can rebuild itself or rebuild the next stage without semantic drift
- the bootstrap loop is deterministic enough to compare artifacts, output, and
  diagnostics across repeated runs

## 2. Guardrails

These are hard rules for the first self-host line.

- Do not rewrite the whole compiler at once.
- Do not move backend/codegen first; move frontend-first.
- Do not expand language surface and self-host scope in the same milestone.
- Do not remove the Rust-hosted path until the Thagore-hosted path has at least
  two green rebuild stages in CI.
- Every migrated stage must keep a golden fixture corpus and a differential
  check against the Rust implementation.

## 3. Stage Model

Use explicit stage names. No ambiguous "bootstrap build".

- `stage0`: current Rust-hosted `thagc`
- `stage1`: first Thagore-written compiler component built by `stage0`
- `stage2`: same component rebuilt by `stage1`
- `stage3`: next migrated compiler component, built by `stage2`

The first milestone is not "full compiler self-host". It is:

- `stage0` builds a Thagore-written frontend slice
- `stage1` reproduces the same observable behavior on the bootstrap corpus
- `stage1` rebuilds that slice again into an equivalent `stage2`

## 4. Migration Order

The migration order is fixed unless a blocker is documented.

1. bootstrap frontend library in Thagore
2. parser + symbol collection + import resolver in Thagore
3. semantic/type-lite bootstrap layer in Thagore
4. typed AST / lowered bootstrap IR in Thagore
5. compiler driver orchestration in Thagore
6. backend/codegen adapters last

Reason:

- frontend logic is easier to differential-test
- frontend logic is less host/toolchain-sensitive than codegen
- once frontend is stable, later stages can reuse it to author more compiler
  code in Thagore

## 4.1 High-level bootstrap authoring surface

The topmost bootstrap authoring layer may use a narrower, more ergonomic
surface than the stable self-host core, but only under strict constraints.

This is not permission to loosen the language globally.

Rules:

- the sugar exists only for the highest bootstrap authoring layer
- the lower self-host layer must keep the explicit core syntax
- all sugar must desugar into the current stable core before normal type
  checking, lowering, and codegen
- runtime semantics, entrypoint semantics, and type semantics must remain those
  of the core language, not a Python-like dynamic mode

That gives two stacked layers:

- `bootstrap authoring surface`: highest layer, optimized for writing compiler
  code faster
- `self-host core surface`: lower layer, explicit and stable, used as the real
  compiler contract

The lower layer remains the source of truth for correctness, determinism, and
performance work.

## 4.2 Allowed top-layer sugar

Only the following two shortcuts are approved for the first self-host line.

### A. Implicit executable entrypoint

Executable bootstrap files may omit an explicit `main`.

Example authoring form:

```tg
print("bootstrap")
compile_next_stage()
```

Desugared core form:

```tg
func main() -> i32:
  print("bootstrap")
  compile_next_stage()
  return 0
```

Constraints:

- applies only to executable bootstrap entry files
- does not apply to library modules
- if a file already declares `func main`, no wrapper is synthesized
- top-level `break`, `continue`, and `return` remain illegal
- top-level declarations such as `func`, `struct`, `const`, and `import` must
  preserve their current meaning

Risk controls:

- require a dedicated desugar pass test for executable-vs-library mode
- add a diagnostic when the compiler cannot decide whether a unit is a library
  module or an executable entry file
- never synthesize `main` in imported modules

### B. Omitted return type with static inference

Functions in the highest bootstrap authoring layer may omit `-> T` only when
the compiler can infer a single static return type without ambiguity.

Example authoring form:

```tg
func token_count():
  return 4
```

Desugared core form:

```tg
func token_count() -> i32:
  return 4
```

Constraints:

- this is static inference, not dynamic typing
- all `return` sites in the function must agree on one type
- if any branch disagrees, inference fails early
- if no return value exists, desugar only to an explicitly defined core rule
  such as `-> i32` plus synthesized `return 0`, or reject the form until such a
  rule is formalized
- public/stable lower-layer compiler code should keep explicit return types

Risk controls:

- reject ambiguous inference instead of guessing
- reject mixed-type returns
- reject functions whose control flow leaves the inferred result unclear
- compare inferred signatures against golden normalized output in CI

## 4.3 Explicitly forbidden sugar for now

Do not add any of the following during the first self-host line:

- block syntax changes
- implicit assignment declarations
- implicit receiver or impl rules
- Python-style dynamic returns
- context-sensitive parsing that changes meaning outside the bootstrap top layer

Reason:

- these areas threaten parser stability, diagnostics quality, and bootstrap
  determinism

## 4.4 Implementation model

The implementation order for this sugar is fixed.

1. parse the authoring surface into a distinct high-level AST mode
2. run a desugar pass that emits the stable self-host core form
3. run the normal frontend/typecheck pipeline on the desugared result
4. keep normalized desugared output available for golden tests

Do not let later compiler phases know whether the source started in sugared or
core form.

## 4.5 Exit criteria for top-layer sugar

The top-layer sugar is acceptable only when all of the following are true:

- every sugared form has a deterministic desugared core form
- Linux x64 and Windows x64 produce identical normalized desugared output
- executable files without `main` and library files without `main` are
  distinguished correctly
- return type inference never falls through to lowering/codegen as an internal
  failure
- the lower self-host core layer remains fully supported without the sugar

## 4.6 Technical implementation plan for top-layer sugar

The next bootstrap step must implement the sugar in a narrow and testable way.
This section is the execution plan, not just the language note.

### Track 1: Module kind detection

Goal:

- distinguish executable bootstrap entry files from library modules before
  implicit `main` synthesis is considered

Required rule:

- only files explicitly loaded as executable roots may use implicit `main`
- imported modules are always library modules

Implementation:

1. add a frontend flag or parse context:
   - `ExecutableRoot`
   - `LibraryModule`
2. thread that context from the driver into the high-level bootstrap parser
3. reject any imported module that relies on top-level executable synthesis

Acceptance:

- executable root without `main` desugars successfully
- library module without `main` does not synthesize one
- imported file with top-level executable statements fails early and clearly

Risk controls:

- no filename-based guessing alone
- module kind must come from driver intent, not heuristic parsing

### Track 2: Implicit `main` desugar pass

Goal:

- convert top-level executable statements into an explicit core `main`

Transform:

- gather top-level executable statements in an `ExecutableRoot`
- wrap them as:

```tg
func main() -> i32:
  ...
  return 0
```

Constraints:

- preserve top-level declarations (`import`, `func`, `struct`, `const`)
- preserve statement order
- reject illegal top-level control flow:
  - `return`
  - `break`
  - `continue`
- if explicit `main` exists, disable synthesis
- if both synthesized and explicit entrypoint logic would conflict, fail early

Tests required:

- executable file with only statements
- executable file with imports + declarations + statements
- executable file with explicit `main`
- imported module with top-level statements
- top-level illegal control-flow cases

Acceptance:

- normalized desugared output is stable on Linux x64 and Windows x64
- later compiler phases see only explicit core `main`

### Track 3: Return type inference for top layer only

Goal:

- permit omitted return annotations in the highest bootstrap authoring layer
  while keeping static typing intact

Allowed first scope:

- literal returns
- identifier returns where the identifier type is already known
- simple direct call returns where callee return type is already known

Not in first scope:

- complex branch joins
- recursive inference cycles
- polymorphic/generalized inference
- inference from backend effects

Algorithm:

1. collect all explicit `return` statements in a function
2. classify each return expression into a known static type if possible
3. if all collected returns agree on one type, synthesize `-> T`
4. if any return is unknown or conflicting, emit a frontend diagnostic
5. if the function has no return and no explicit annotation, reject for now
   unless a separate explicit rule is approved later

Acceptance:

- inferred type appears in normalized desugared output
- conflicts fail during frontend analysis
- no omitted return type case falls through as an internal compiler error

Risk controls:

- do not silently default to `i32`
- do not infer across ambiguous control-flow joins yet
- do not let codegen decide inferred types

### Track 4: Desugared-output observability

Goal:

- make sugared and core forms comparable in CI

Implementation:

- add a normalized desugared dump mode for bootstrap/self-host fixtures
- golden-test the desugared form, not just final behavior

Acceptance:

- CI can compare:
  - source authoring form
  - desugared core form
  - observable runtime output

Reason:

- this is the main defense against hidden semantic drift in bootstrap syntax

### Track 5: Layer boundary enforcement

Goal:

- prevent top-layer sugar from leaking into the lower self-host core

Implementation:

- mark self-host source roots as one of:
  - `bootstrap-authoring`
  - `selfhost-core`
- enable sugar only for the first class
- add negative tests showing the same omitted forms are rejected in
  `selfhost-core`

Acceptance:

- sugar is unavailable outside the designated bootstrap-authoring roots
- core compiler code remains explicit and stable

## 4.7 Order of execution before active bootstrap

The order below is mandatory.

1. extract reusable frontend modules into
   `bootstrap/selfhost/frontend/`
2. add module kind detection
3. add implicit `main` desugar with golden desugared output tests
4. add narrow return type inference with conflict tests
5. add layer-boundary enforcement tests
6. add differential frontend corpus between Rust frontend and Thagore frontend
7. only then start `stage0 -> stage1 -> stage2`

Do not start the rebuild chain before steps 1 through 6 are green.

## 4.8 Definition of "ready to begin active bootstrap"

Active bootstrap may begin only when all of the following are true:

- `bootstrap/selfhost/frontend/` exists and is reused by fixtures
- top-layer sugar is implemented through a dedicated desugar pass
- desugared output is golden-tested on Linux x64 and Windows x64
- `selfhost-core` rejects top-layer-only sugar
- differential frontend tests are green against the Rust-hosted reference

At that point, the next step is no longer planning. It is the first real
`stage0 -> stage1 -> stage2` compiler slice.

## 5. Immediate Milestones

### Milestone A: Seed to frontend library

Keep the bootstrap seed corpus fixture-oriented while the reusable frontend implementation stays under `bootstrap/selfhost/frontend/`.

Deliverables:

- `bootstrap/selfhost/frontend/lexer.tg`
- `bootstrap/selfhost/frontend/parser.tg`
- `bootstrap/selfhost/frontend/symbols.tg`
- `bootstrap/selfhost/frontend/resolver.tg`
- `bootstrap/selfhost/frontend/diagnostics.tg`
- a tiny CLI-style entrypoint that prints structured output for fixtures

Acceptance:

- fixture corpus runs through `stage0`
- output matches golden files on Linux x64 and Windows x64
- no logic remains only in fixture glue if it belongs in reusable modules

Owner:

- `core-lang`

Current status:

- extraction has started under `bootstrap/selfhost/frontend/`
- seed modules are now mirrored there as the initial reusable frontend slice
- the bootstrap-seed CLI harness now builds from
  `bootstrap/selfhost/frontend/main.tg`
- executable-root vs library-module mode now exists in the selfhost frontend
  path and is gated with positive and negative bootstrap-seed coverage
- executable-root analysis now starts synthesizing an implicit `main` for
  top-level statement files in the selfhost frontend path
- narrow top-layer return-type inference now exists for bootstrap-top-layer
  functions without `->` when the return can be proven from literals,
  identifier bindings with explicit static types, or direct calls whose callee
  return type is already known; ambiguous cases still fail fast
- the bootstrap authoring boundary is now enforced in the frontend harness:
  `core-exe` / `core-library` inputs reject implicit `main` synthesis and
  omitted return annotations instead of silently accepting bootstrap-only sugar
- negative coverage now exists for both `core-exe` and `core-library`, so the
  lower selfhost layer is tested against bootstrap-only return inference in
  both executable and library contexts
- duplicate seed-local copies of the extracted frontend modules have now been
  removed from the legacy test area, so the reusable implementation lives
  under `bootstrap/selfhost/frontend/` only
- the next implementation step is widening normalized frontend parity beyond the
  current narrow manifests and rehearsal lanes
- the driver boundary now also locks full `analyze` output for the
  frontend differential corpus, widening Target 03 beyond bootstrap-seed-only
  fixtures and report-mode-only frontend cases
- the driver boundary now also locks orchestration behavior above raw output
  parity: default-input fallback, relative vs absolute path routing, kind/mode
  fallback, core-kind routing, and missing-source exit semantics
- the next bootstrap slice now starts at `bootstrap/selfhost/frontend/compiler.tg`,
  which wraps the frontend pipeline as a first selfhost compiler-driver
  surface and is guarded by `bootstrap/selfhost/corpus/compiler-driver-contract.txt`
  plus a dedicated `Selfhost Compiler Driver` CI workflow
- a real bootstrap artifact gate now exists under
  `bootstrap/selfhost/corpus/bootstrap-artifact-contract.txt`: the selfhost
  compiler builds a rebuilt compiler artifact plus a rebuilt frontend-main tool
  artifact, runs them, and keeps those reports stable across CI rebuilds
- the first lowering slice now exists at `bootstrap/selfhost/frontend/lower.tg`
  and is guarded by `bootstrap/selfhost/corpus/lowering-slice.txt`, giving the
  bootstrap line an initial lowered-shape contract instead of stopping at
  frontend and command-routing outputs

### Milestone B: Differential frontend pass

Make the Thagore frontend slice and Rust frontend slice run on the same narrow
corpus and compare normalized output.

Deliverables:

- corpus under `bootstrap/selfhost/corpus/fixtures/frontend/`
- normalization format for tokens, AST summary, symbols, imports, diagnostics
- differential runner in CI

Acceptance:

- `stage0(thagore_frontend)` output equals `rust_frontend` output on the corpus
- corpus includes both valid and invalid files

Owners:

- `core-lang`
- `infra`

### Milestone C: Stage1/Stage2 rebuild loop

Promote the Thagore frontend slice from "library checked by Rust host" to
"self-rebuilt component".

Deliverables:

- workflow `bootstrap-selfhost-stage.yml`
- `stage0 -> stage1 -> stage2` build chain
- artifact compare step for normalized output and diagnostics

Acceptance:

- Linux x64: green
- Windows x64: green
- `stage1` and `stage2` agree on the bootstrap corpus

Owners:

- `infra`
- `core-lang`

Current status:

- `.github/workflows/bootstrap-selfhost-stage.yml` now exists as the dedicated
  stage-labeled rehearsal lane for the current frontend slice
- it builds `stage0` host `thagc`, emits a `stage1` selfhost frontend slice,
  rebuilds a `stage2` copy, and diffs stage reports on Linux x64 and Windows
  x64
- the current frontend slice membership is now declared in
  `bootstrap/selfhost/tools/frontend-stage-manifest.txt`, so CI/stage builders
  do not hardcode `scan/parse/check` in multiple places
- the driver executable boundary is now split out as its own target manifest
  (`bootstrap/selfhost/tools/frontend-driver-manifest.txt`) and can be gated
  independently from the lower `scan/parse/check` stage slice
- the stage-labeled bootstrap rehearsal now validates both the lower
  `scan/parse/check` slice and the higher `main.tg` driver boundary across
  stage1/stage2 rebuilds, so top-level session/driver drift is included in the
  deterministic rebuild check
- that rehearsal now also validates the first compiler-driver slice
  (`bootstrap/selfhost/frontend/compiler.tg`) across stage1/stage2 rebuilds,
  so command-surface bootstrap drift is gated in the same loop as the frontend
  stage and driver boundaries; that slice now includes host-routed `build` and
  `run` orchestration, not just analysis/report commands
- that rehearsal now also validates the first lowering slice
  (`bootstrap/selfhost/frontend/lower.tg`) across stage1/stage2 rebuilds, so a
  compiler-middle summary is now part of the deterministic bootstrap loop
- that same rehearsal lane now also validates the session-routed replacement
  contract for `check.tg` across stage1/stage2, so Target 01 behavior is part
  of the rebuild loop instead of living only in its separate workflow
- this is still a frontend-slice bootstrap rehearsal, not yet a full
  self-hosting compiler binary

## 6. Workstreams

| Workstream | Goal | Owner | Exit condition |
| --- | --- | --- | --- |
| frontend extraction | move seed logic into reusable modules | core-lang | seed code no longer lives only as test glue |
| differential corpus | compare Rust vs Thagore frontend behavior | core-lang | corpus is green on both lanes |
| selfhost CI | build `stage1` and rebuild to `stage2` | infra | dedicated workflow is required and green |
| stdlib contract | freeze bootstrap-facing stdlib APIs used by compiler code | stdlib | no missing parity in compiler-facing helpers |
| perf floor | prevent bootstrap stages from exploding RAM/time | perf | CI thresholds exist for stage1/stage2 |

## 7. Repository Layout

Add a stable location for self-host work.

- `bootstrap/selfhost/frontend/`
- `bootstrap/selfhost/corpus/`
- `bootstrap/selfhost/tools/`
- `bootstrap/selfhost/corpus/fixtures/`

Do not reintroduce reusable logic, fixtures, or goldens outside `bootstrap/selfhost/**`. The active bootstrap corpus now lives under `bootstrap/selfhost/`.

## 8. CI Sequence

The first real self-host workflow should run in this order:

1. build `stage0` (Rust-hosted `thagc`)
2. build Thagore frontend slice with `stage0`
3. run fixture corpus with `stage1`
4. rebuild the same frontend slice with `stage1`
5. run fixture corpus with `stage2`
6. compare `stage1` vs `stage2` normalized outputs
7. enforce runtime and memory thresholds

No later stage should be added until this chain is green and stable.

## 9. Risks That Can Still Break Bootstrap

- parser/semantic divergence between Rust and Thagore implementations
- hidden stdlib differences between Linux and Windows
- nondeterministic traversal or map ordering in compiler-authored code
- diagnostics drift that looks harmless but breaks differential checks
- RAM spikes once the seed grows into reusable frontend modules

Each risk needs a concrete test before the next migration step starts.

## 10. Definition Of "Self-Host Started"

Self-host bootstrap has officially started only when:

- `bootstrap/selfhost/frontend/` exists
- CI builds it with `stage0`
- CI runs a differential corpus against it

That is the line between "planning/bootstrap-ready" and "active self-host".

Current status on `indev-rewrite`:

- `bootstrap/selfhost/frontend/` is now the canonical seed frontend slice
- bootstrap probe already gates normalized desugared output for:
  - implicit `main`
  - inferred top-layer literal return types
- bootstrap probe now also gates a first narrow differential corpus under
  `bootstrap/selfhost/corpus/fixtures/frontend/` against the Rust-hosted `thagc check` surface
- bootstrap probe now also gates selected selfhost `dump-report` goldens so the
  differential track is not limited to coarse diagnostic labels
- bootstrap probe also gates module-kind-sensitive `dump-report` goldens for
  `library` vs synthesized executable-root analysis paths
- `bootstrap/selfhost/frontend/check.tg` now serves as the canonical stage entry
  for selfhost semantic/report execution, reducing reliance on the broader
  harness-oriented `main.tg`
- `bootstrap/selfhost/frontend/parse.tg` now serves as the first dedicated
  pre-check stage entry, and bootstrap probe gates its output independently
- `bootstrap/selfhost/frontend/scan.tg` now serves as the token-only stage
  entry, and bootstrap probe gates a small `stage0 -> scan -> parse -> check`
  chain so stage wiring can regress independently of the broader harness
- that stage-chain gate now covers both success and failing fixtures so
  downstream diagnostics cannot silently drift while the happy path stays green
- shared CLI/stage setup is now centralized in
  `bootstrap/selfhost/frontend/session.tg`, reducing duplication before the
  first real stage replacement work
- shared stage execution is now centralized in
  `bootstrap/selfhost/frontend/pipeline.tg`, so `scan.tg`, `parse.tg`, and
  `check.tg` are thin entrypoints over the same selfhost pipeline core
- diagnostic composition/filtering is now centralized in
  `bootstrap/selfhost/frontend/semantics.tg`, narrowing `pipeline.tg` toward
  pure stage execution before the first real stage replacement
- `scan.tg` and `parse.tg` now have golden coverage on failing fixtures as
  well, so stage differential confidence is no longer limited to happy-path
  token/summary output
- bootstrap probe now also runs a multi-fixture `scan -> parse -> check`
  corpus across executable success, executable failure, and library-mode
  fixtures, which is the first reusable stage-chain gate rather than a one-off
  smoke path
- bootstrap CI now has a dedicated selfhost frontend stage lane, so stage
  corpus regressions are isolated from the larger stdlib/bootstrap audit job
  and can become a real replacement gate
- the first replacement target contract now lives in manifest files under
  `bootstrap/selfhost/corpus/fixtures/frontend/`, so corpus scope can expand without hardcoding the
  target surface inside Rust test logic
- the concrete Rust-side target for that contract is now documented in
  `docs/plan/selfhost-replacement-target.md` as Target 01:
  `check_file(...) -> check_all() -> check_module(...)`
- the next staged replacement boundary is now documented there as Target 02:
  `bootstrap/selfhost/frontend/parse.tg` backed by `parse_corpus.txt` and the
  standalone stage runner
- the dedicated selfhost frontend lane now rebuilds `scan.tg`, `parse.tg`, and
  `check.tg` directly with host `thagc` and validates them via
  `tooling/ci/selfhost_frontend_stage.py`, which is the first real
  `stage0 -> selfhost stage corpus` gate rather than only a Rust test wrapper
- that lane now also performs a second rebuild and diffs the emitted corpus
  reports, which is the first deterministic
  `stage0 -> selfhost stage -> rebuilt stage` confidence check on the frontend
  slice
- `.github/workflows/selfhost-frontend-stage.yml` now provides a dedicated
  first-class workflow for that slice, reducing dependence on the broader
  bootstrap probe workflow and making the replacement target visible on its own
- `.github/workflows/selfhost-frontend-replacement.yml` now treats Target 01 as
  a first-class replacement trial by comparing selfhost `check.tg` directly
  against host `thagc check` on the contract manifest
- that replacement trial now runs a second rebuild pass and diffs the emitted
  host/selfhost summaries, pushing Target 01 from single-pass trial status
  toward deterministic replacement status
- the replacement workflow now injects the selfhost binary back through
  `tools/thagore-cli/src/session.rs` using hidden `thagc check` flags
  (`--selfhost-replacement-bin`, `--selfhost-replacement-manifest`,
  `--selfhost-replacement-strict`, `--selfhost-replacement-report-out`), with
  env fallback retained, so Target 01 is no longer only checked externally; the
  Rust-side trial path itself is exercised in CI
- the replacement workflow now preserves the session-routed transcript beside
  the external validator summary, so CI can diff both the replacement verdict
  and the real Rust-path observability output across rebuilds
- the narrow replacement contract now includes an explicit library-mode success
  fixture, widening Target 01 slightly beyond executable-root-only coverage
- the narrow replacement contract now also includes import-resolution success
  fixtures in both executable and library mode, extending Target 01 into a
  small but real module-surface slice
- the narrow replacement contract now also includes an executable missing-import
  fixture, so Target 01 now checks one real module-resolution failure path in
  addition to import success
- the narrow replacement contract now also includes unresolved-imported-symbol
  coverage, extending Target 01 into imported-symbol semantics instead of only
  file-level module resolution
- those module-surface failure categories now also run in library mode, so
  Target 01 spans executable and library behavior on both import success and
  import failure
- the narrow replacement contract now also includes import-alias success in
  both module kinds; duplicate-alias parity is deferred until the Rust side
  exposes alias-specific diagnostics instead of only generic unresolved paths
- normalized report goldens now also cover module-surface fixtures, so Target
  01 locks import behavior at report level instead of only label level
- scan/parse goldens now also cover module-surface fixtures, so Target 01 has
  early-stage protection on imports instead of relying only on final reports
- report/parse/scan goldens are now manifest-driven, which turns those richer
  stage checks into explicit contracts instead of hard-coded case lists
- the standalone selfhost stage runner now validates those richer manifests
  directly, which moves more of Target 01 confidence out of Rust test code and
  into first-class selfhost CI tooling
- both bootstrap workflows now run on `indev-rewrite`, cancel superseded runs,
  and expose selfhost stage reports directly in job summaries, which shortens
  the inspect-fix loop while the replacement target is still moving
- the differential contract now carries explicit module-kind, so library
  fixtures are validated through the real Rust session path as library inputs
  instead of silently inheriting executable defaults
- the initial differential gate now includes call-arity mismatch parity in
  addition to ok / unknown identifier (value and callee) / assignment-target /
  assignment type / local type / assignment call-result type / local
  call-result type / condition type / return type / return call-result type
  categories
- the next concrete step is widening that corpus beyond diagnostic categories
  into richer normalized frontend output parity
