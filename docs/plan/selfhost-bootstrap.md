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

## 5. Immediate Milestones

### Milestone A: Seed to frontend library

Turn `tests/bootstrap_seed/` from fixture-oriented seed code into a reusable
library crate/module tree.

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

### Milestone B: Differential frontend pass

Make the Thagore frontend slice and Rust frontend slice run on the same narrow
corpus and compare normalized output.

Deliverables:

- corpus under `tests/selfhost_frontend/`
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
- `tests/selfhost_frontend/`

Do not keep growing `tests/bootstrap_seed/` forever. That path should remain
the seed harness, while reusable code moves under `bootstrap/selfhost/`.

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
