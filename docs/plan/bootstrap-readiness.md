# Bootstrap Readiness Plan

This document defines the work required before Thagore can be treated as
bootstrap-ready. The goal is not "mostly works". The goal is to eliminate the
remaining failure modes that can cause bootstrap to collapse late.

## 1. Bootstrap Target

Thagore is bootstrap-ready only when all of the following are true:

- A non-trivial Thagore codebase can be checked, built, and run by the current
  public toolchain on Windows x64 and Linux x64.
- That codebase exercises the exact language and stdlib surface needed for
  compiler-authoring and package-tooling work.
- The produced compiler/tool artifact can be used to build the same codebase
  again without semantic drift.
- Diagnostics fail early and consistently. No user-facing source may type-check
  successfully and then fail later in lowering or code generation for a reason
  the type checker could have known.

## 2. Non-Negotiable Exit Criteria

Bootstrap work is done only when all criteria below are met.

- `bootstrap_probe` passes on Windows x64 and Linux x64.
- Rebuild-from-built-tool pass succeeds on both lanes.
- No bootstrap-required feature is in a "parsed but not end-to-end" state.
- No bootstrap-required stdlib function differs materially between native and
  interpreter/playground execution.
- Repeated builds of the same probe are stable in behavior and diagnostics.
- CI has a dedicated bootstrap workflow and it is required, not advisory.

## 3. Threat Model

The known classes of bootstrap blockers are:

1. Language surface parses but is not implemented end to end.
2. Type checking succeeds but lowering/codegen fails later.
3. Native and interpreter/playground paths disagree on semantics.
4. Stdlib is good enough for demos but not for compiler-authoring workloads.
5. Build results depend on host quirks, traversal order, or leaked state.
6. Diagnostics cascade and hide the real root cause.
7. Performance cliffs make bootstrap technically possible but operationally
   unreliable.

## 4. Bootstrap Scope

### 4.1 Required surface

The probe and all bootstrap-focused work must restrict itself to the features
below until they are fully verified.

- functions
- structs
- impl methods with valid receivers
- constants with compile-time initializers
- local `let`
- arrays and indexing
- strings and string helpers
- `if`, `while`, `for`, `break`, `continue`, `return`
- imports
- stdlib modules:
  - `std.io`
  - `std.string`
  - `std.fs`
  - `std.path`
  - `std.process`
  - `std.time`
  - `std.array`

### 4.2 Forbidden until completed

These surfaces must not be treated as bootstrap-ready until their full pipeline
is implemented and tested.

- generic structs
- generic impl blocks
- any feature currently guarded by `UnsupportedFeature`
- any stdlib API that lacks parity tests across native and interpreter paths

### 4.3 Deferred

These are useful but not required for the first bootstrap milestone.

- typestate
- advanced intent/flow ergonomics beyond current verified semantics
- optimization-focused codegen work not needed for correctness
- cross-target bootstrap beyond Windows x64 and Linux x64

## 5. Workstreams

### Workstream A: Bootstrap probe

Build a real Thagore project that behaves like a small compiler/tooling module,
not a toy Fibonacci example.

Required probe capabilities:

- file scanning
- string slicing/joining/parsing
- array traversal and indexing
- module-like decomposition across several files
- structured diagnostics formatting
- process invocation when tool integration is required
- deterministic output

Deliverables:

- `tests/bootstrap_probe/`
- `tests/bootstrap_probe/src/main.tg`
- small supporting modules for lexer-like and diagnostics-like tasks
- golden output file

Acceptance:

- `thagc check` passes
- `thagc build` passes
- `thagc run` passes
- output matches golden file

### Workstream B: Language surface closure

Audit every bootstrap-required feature and classify it as one of:

- fully implemented
- must fail early
- not in bootstrap scope

Required actions:

- remove remaining user-facing `Unknown` sentinel diagnostics on required paths
- continue moving errors from lowering/codegen into type checking where possible
- add regression tests for every class moved earlier
- explicitly reject required-surface misuse without cascaded secondary errors

Acceptance:

- no bootstrap-required source reaches lowering/codegen with a known semantic
  misuse that type checking could have rejected

### Workstream C: Lowering/codegen closure

Audit lowering and codegen for remaining user-facing failures.

Required actions:

- enumerate all `LoweringError::*` still reachable from normal user code
- classify each one:
  - backend-only and legitimate
  - should have been caught earlier
- move the second class into type checking
- add regression coverage for each moved class

Acceptance:

- lowering/codegen only emit backend-only failures on bootstrap-required paths

### Workstream D: Stdlib bootstrap-grade audit

Audit stdlib by usefulness for compiler-authoring, not by demo completeness.

Modules to close:

- `std.string`
- `std.io`
- `std.fs`
- `std.path`
- `std.process`
- `std.time`
- `std.array`

For each module:

- verify native behavior
- verify interpreter/playground behavior
- verify Windows/Linux behavior for bootstrap-required APIs
- add malformed-input tests
- add parity tests where both execution paths exist

Acceptance:

- probe never depends on an API without parity and edge-case coverage

### Workstream E: Determinism and state safety

Required actions:

- verify repeated runs do not leak interpreter/compiler state
- ensure temp file naming and output naming are safe on Windows
- ensure filesystem traversal used by bootstrap tooling is deterministic where
  needed
- ensure symbol collection does not depend on nondeterministic map iteration

Acceptance:

- same source built twice yields equivalent behavior and diagnostics

### Workstream F: Performance safety floor

The first bootstrap milestone does not require aggressive optimization, but it
does require removal of operationally dangerous cliffs.

Required actions:

- benchmark parse/check/build/run of the bootstrap probe
- record RAM ceilings for the probe
- fail CI if probe exceeds agreed memory or time thresholds
- eliminate unbounded recovery/allocation patterns on probe paths

Acceptance:

- probe stays within documented runtime and memory envelopes on CI

## 6. CI Plan

Add a dedicated workflow, for example `bootstrap-probe.yml`, with these stages:

1. build host `thagc`
2. check/build/run `bootstrap_probe`
3. rebuild probe using the produced toolchain artifact path
4. compare outputs and diagnostics
5. enforce runtime and memory thresholds if feasible on the runner

Required lanes:

- Windows x64
- Linux x64

Nice-to-have after first milestone:

- macOS x64 or arm64

## 7. Sequence Of Execution

The order matters. Do not start from optimization or feature expansion.

1. Freeze bootstrap scope
2. Build `bootstrap_probe`
3. Use the probe to expose remaining language-surface gaps
4. Close lowering/codegen escape paths
5. Audit stdlib used by the probe
6. Add determinism and repeated-build checks
7. Add bootstrap workflow to CI
8. Run full rehearsal on Windows x64 and Linux x64
9. Only then declare the repo bootstrap-ready enough to start a bootstrap line

## 8. Release Guidance

The current line should remain:

- `v0.9.6`: basic compiler hardening before bootstrap

The next bootstrap-focused line should not start until:

- the probe exists
- the CI workflow exists
- the required feature list is closed or explicitly blocked

That line can then be scoped as:

- bootstrap-focused
- release-automation-tightened
- toolchain-trust-focused

But it must still remain `indev` until the rehearsal criteria are actually met.

## 9. Tracking Table

Use this table as the live checklist.

| Area | Status | Blocking bootstrap | Notes |
| --- | --- | --- | --- |
| bootstrap probe | partial | yes | probe exists under `tests/bootstrap_probe/`, needs CI enforcement and broader coverage |
| language surface closure | in progress | yes | several fail-fast fixes already landed |
| lowering/codegen closure | in progress | yes | audit still incomplete |
| stdlib bootstrap audit | in progress | yes | `fs`, `process`, `time`, `string`, `io` parity work is underway |
| determinism/state safety | partial | yes | repeated-build discipline not formalized |
| performance safety floor | partial | yes | hard limits not yet enforced |
| bootstrap CI workflow | partial | yes | workflow exists, still needs to prove the stricter rehearsal criteria |

## 10. Practical Rule

If any required feature is:

- only partially implemented,
- only tested in one execution path,
- or only known to fail "later",

then bootstrap readiness is not met.

This plan treats that as a hard stop, not a warning.
