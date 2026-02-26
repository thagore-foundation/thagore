# Thagore Flow (Saga as Language Primitive)

Implementation approval: Approved  
Status: Implemented (Phase A/B/C + tooling MVP; parallel/barrier semantic checks)  
Owner: Thagore Compiler / Runtime Reliability  
Last updated: 2026-02-19

## 1) Vision

`flow` is a first-class language construct for orchestrating multi-step side effects with built-in compensation, retry, timeout, and recovery.

Core promise:

- Express reliability logic directly in source code.
- Enforce safety with compile-time checks.
- Recover predictably at runtime after failures or crashes.

## 2) Problem

Production failures often come from partial success across systems:

- step 1 succeeds (cloud resource),
- step 2 succeeds (artifact upload),
- step 3 fails (database migration),
- system remains inconsistent without manual rollback.

Current languages mostly rely on framework-level saga patterns. Thagore can move this into compiler-checked language semantics.

## 3) Syntax proposal

```tg
flow deploy_release(input: DeployInput) -> Result<DeployOut, DeployErr>:
    step vm = cloud.create_vm(input.spec)
        undo cloud.delete_vm(vm.id)
        retry 2 backoff exp(200ms, 2.0)
        timeout 20s
        idempotent

    step pkg = artifact.upload(input.bundle)
        undo artifact.delete(pkg.id)
        timeout 30s

    step mig = db.migrate(input.version)
        undo db.rollback(mig.prev_version)
        timeout 60s

    return Ok(DeployOut(vm.id, pkg.id))
```

Optional forms:

- `irreversible reason "..."`
- `parallel` block + `barrier`
- `on_error` local handler for domain-specific mapping.

## 4) Execution semantics

1. `flow` lowers to a deterministic state machine.
2. Steps run in declaration order unless inside `parallel` block.
3. If a step fails:
   - forward execution stops,
   - compensations execute in reverse order for completed steps,
   - result is returned as structured flow error.
4. If compensation fails:
   - runtime marks `compensation_failed`,
   - returns error including failed compensation step id.
5. On process crash:
   - runtime loads journal,
   - resumes from last stable checkpoint,
   - continues compensation or recovery policy.

## 5) Compile-time checks

Compiler validations:

- side-effect step missing `undo` (unless explicitly `irreversible`),
- invalid `undo` scope/type usage,
- `retry` declared without idempotency contract,
- impossible timeout/retry policy combinations,
- unbounded execution path in strict mode,
- unknown external step capability metadata.

Capability metadata contract is now versioned in:

- `docs/idea/flow_capability_registry.txt`
- override via `THAG_FLOW_CAPABILITY_REGISTRY=<path>`

Strict mode target:

```bash
thagore build app.tg --flow=strict
```

## 6) Runtime model

Required runtime primitives:

- step journal (append-only minimal metadata),
- compensation executor,
- retry scheduler,
- timeout cancellation hooks,
- crash recovery loader.

Minimal journal fields:

- flow id,
- step id,
- status (`started|ok|failed|compensated|comp_failed`),
- timestamp,
- idempotency key hash.

## 7) CLI design

```bash
thagore flow doctor <entry.tg>
thagore flow explain <entry.tg> [--json]
thagore flow simulate <entry.tg> [--fail-at <step-id>] [--fail-once-at <step-id>] [--timeout-at <step-id>] [--durability <high|normal|low>] [--journal-format <compact|jsonl|binary>]
thagore flow recover <session-id> [--json]
thagore build <entry.tg> --flow=<off|on|strict>
```

Command roles:

- `flow doctor`: checks feature readiness and metadata coverage.
- `flow explain`: prints generated state machine and compensation graph.
- `flow simulate`: chaos-style failure injection in build/test context.
- `flow recover`: resume rollback from a failed/crashed session (`session-id` defaults to latest when omitted in CLI).

## 8) Reliability contracts

`flow` contracts should be explicit and typed:

- idempotency declaration per step,
- compensation guarantee level (`best_effort|strict`),
- timeout budget,
- retry budget.

Error surface:

- `FlowStepFailed`,
- `FlowCompensationFailed`,
- `FlowRecoveryFailed`,
- all with step id, causal chain, and partial-complete metadata.

## 9) Performance and overhead control

This section is mandatory to avoid global burden.

### 9.1 Zero-cost for non-flow code

- Do not instrument functions without `flow`.
- Keep default pipeline identical when no `flow` AST nodes exist.
- Fast path: single marker check then bypass flow passes.

### 9.2 Build-time optimization

- Incremental cache for flow graph analysis.
- Stable hash of flow blocks to avoid recomputation.
- Separate pass scheduling: parse/typecheck unaffected units first.
- `--flow=on` only runs baseline checks; `strict` runs full path analysis.

### 9.3 Runtime optimization

- Compact binary journal format (fixed-size records).
- Batch fsync policy with configurable durability levels:
  - `durability=high` for critical flows,
  - `durability=normal` default,
  - `durability=low` for non-critical local dev.
- Lock-free step state updates where safe.
- Reuse timer wheel for retries/timeouts instead of per-step threads.

### 9.4 Deployment optimization

- Feature gating: runtime flow module linked only when needed.
- LTO/GC of unused flow handlers in release builds.
- Optional compile flag to strip explain/debug metadata:
  - `--flow-strip-debug`

## 10) Safety policy

Default:

- local development: `--flow=on`,
- CI and release: `--flow=strict`.

Hard fail conditions:

- compensation coverage below policy threshold,
- unresolved idempotency for retried external calls,
- recovery journal corruption in strict mode.

## 11) Implementation phases

### Phase A: Syntax and AST

- add `flow`/`step`/`undo` syntax,
- parser + diagnostics,
- no runtime changes yet.

### Phase B: Semantic checker

- compensation coverage validation,
- retry/idempotency checks,
- timeout policy checks.

### Phase C: Runtime engine MVP

- sequential state machine execution,
- journal + recovery,
- basic retry/timeout.

### Phase D: Performance hardening

- parallel step groups,
- compact journal and timer optimization,
- incremental compile caching.

### Phase E: Tooling

- `flow explain`,
- `flow simulate`,
- CI policy templates.

## 14) Implementation snapshot (2026-02-19)

Done:

- Phase A/B parser + semantic checks for `flow/step/undo/retry/timeout/idempotent/irreversible`.
- Runtime MVP supports journaled simulate + compensation + crash/recover workflow.
- Optional step metadata (`on_error`, `irreversible reason`) is validated and surfaced in `flow explain` JSON/human output; simulate journal notes now carry mapped failure context for these declarations.
- Runtime simulate executes `parallel` groups as barrier-bounded batches (group sibling steps are evaluated before flow fail decision).
- Parallel batch scheduler uses round-robin attempt interleaving to model concurrent progress while preserving deterministic replay.
- Retry scheduling now uses deterministic tick delays (`retry_in_ticks`) with bounded backoff to mimic timer-wheel semantics without per-step threads.
- Timeout and retry now share step-level tick deadlines (`deadline_tick`), so retries are cut off when timeout budget is exhausted.
- Scheduler now runs concurrent tick waves: all due steps in a tick are started before result evaluation, then resolved together.
- Scheduler skips idle ticks by jumping directly to next due tick in batch state, reducing replay overhead.
- Simulate supports injected fail paths (`fail-at`, `fail-once-at`, `timeout-at`) and retry attempts.
- Simulate supports durability levels (`high|normal|low`) with different journal flush policies.
- Journal now supports compact record format by default, with `jsonl` compatibility mode and binary (`.bin`) mode for compact persistence.
- High durability journal writes now append incrementally (instead of rewriting full buffer each event), improving runtime write cost.
- Strict recovery now hard-fails on corrupted journal events.
- `parallel:` / `barrier` headers are validated semantically (including missing-barrier and empty-parallel diagnostics).
- Strict mode rejects unknown external step capabilities; doctor reports capability known/unknown coverage.
- Capability checks now read from a versioned registry contract (`flow_capability_registry.txt`) shared by validator and runtime doctor.
- `flow doctor` now has incremental cache (`.thagore/flow/cache`) keyed by entry/mode/source-hash with hit/miss reporting.
- `flow explain --json` now reuses cached flow graph output (`.thagore/flow/cache`) keyed by entry/mode/source-hash.
- Doctor/explain cache fingerprint now includes transitive `import` dependencies, so cache invalidates on dependency source changes.
- Validation cache is now shared across doctor/explain/simulate/build gate, reducing repeated strict checks in the full flow pipeline.
- CLI and tests cover doctor/explain/simulate/recover + strict/off/on build modes.

Remaining (roadmap scope):

- none.

## 12) Testing strategy

- Unit: parser, semantic rules, policy checks.
- Integration: multi-step success/fail/compensation paths.
- Crash recovery: kill-and-resume scenarios.
- Performance: overhead vs non-flow baseline.
- Determinism: repeat build and replay tests with fixed inputs.

## 13) Risks and mitigations

Risk: developer complexity from too many flow options.  
Mitigation: safe defaults and layered modes (`off|on|strict`).

Risk: runtime overhead in high-throughput services.  
Mitigation: feature gating + compact journal + batching.

Risk: false confidence from weak compensation logic.  
Mitigation: strict compile checks and simulation tooling.

## 14) Relationship with other ideas

- Complements `build-autofix`: autofix handles code hygiene; flow handles reliability semantics.
- Complements `intent`: intent optimizes algorithms; flow secures side-effect orchestration.
- Complements `drago`: flow metadata can be packaged for production diagnostics/distribution.
