# Thagore Intent Engine (SIE)

Implementation approval: Approved  
Status: Implemented (MVP + CI hardening baseline)  
Owner: Thagore Compiler / Optimizer  
Last updated: 2026-02-15

## 1) Vision

`intent` is a deterministic optimization feature that lets developers describe *goal + constraints* while Thagore generates and selects an efficient implementation at build time.

Core direction:

- No AI dependency at runtime.
- No network dependency in compiler path.
- Deterministic output via lockfile.
- Existing non-intent code path keeps current build speed and behavior.

## 2) Why this is needed

Large productivity gap today:

- Developers write repetitive algorithmic boilerplate.
- Optimizing for different targets needs manual tuning.
- Teams struggle to keep code readable and still high performance.

`intent` solves this by separating:

- *what the code should achieve* (`goal`),
- from *how it should be optimized* (selected by compiler under constraints).

## 3) User-facing model

### 3.1 `intent func`

For full function synthesis/optimization.

```tg
intent func dedup_sorted(xs: [i32]) -> [i32]:
    goal: deduplicate_sorted
    examples:
        dedup_sorted([1,1,2,2,3]) == [1,2,3]
    constraints:
        time <= O(n)
        deterministic == true
```

### 3.2 `intent loop`

For local loop optimization.

```tg
intent loop i in 0..n:
    goal: reduce_sum
    constraints:
        parallel == true
        deterministic == true
```

### 3.3 `intent calc`

For heavy math expressions where vectorization/precision matter.

```tg
let y = intent calc(x):
    goal: polynomial_eval
    constraints:
        vectorize == true
        max_error <= 1e-6
```

### 3.4 `intent block`

For multi-step transformations.

```tg
intent block:
    goal: filter_map_reduce
    constraints:
        no_heap_growth == true
        stable == true
```

## 4) CLI commands

```bash
thagore intent doctor [entry.tg]
thagore intent lock <entry.tg> [-o thagore.intent.lock]
thagore intent explain <entry.tg> [--json]
thagore build <entry.tg> --intent=min
thagore build <entry.tg> --intent=max
thagore build <entry.tg> --intent=off
```

Command behavior:

- `intent doctor`: validates toolchain and intent rules availability.
- `intent lock`: materializes selected rewrite plans and checksums.
- `intent explain`: shows matched patterns, candidates, and chosen plans.
- `--intent=off`: skip intent pipeline.
- `--intent=min`: local deterministic matcher + rewrite + verify.
- `--intent=max`: deeper search in known strategy space (still deterministic).

## 5) Compiler architecture

Pipeline insertion point:

1. Parse source to AST.
2. Lower to normalized IR.
3. Detect `intent` markers.
4. Match intent pattern.
5. Generate candidate implementations from verified rule library.
6. Score with cost model for current target.
7. Verify semantic + constraints.
8. Persist decision to lockfile.
9. Lower chosen plan into backend IR.

If no `intent` markers:

- Steps 3-8 are skipped (fast no-op path).

## 6) Main components

### 6.1 Intent Matcher

Responsibilities:

- detect intent nodes (`func/loop/calc/block`),
- map each node to a known goal family,
- validate required metadata (`goal`, `constraints`, optional `examples`).

### 6.2 Rule Library

A versioned set of deterministic rewrite plans:

- data transforms (`map/filter/reduce`, dedup/group),
- numeric kernels (dot/product/reduction),
- string scan/match patterns,
- sorting/search templates.

Each rule must include:

- applicability conditions,
- asymptotic and memory profile,
- deterministic flag,
- verification hooks.

### 6.3 Cost Model

Inputs:

- target triple / CPU features,
- data type/shape hints,
- memory budget and constraints.

Output:

- ranked candidate plan set and selected winner.

### 6.4 Verifier

Mandatory checks before codegen:

- type safety and semantic preservation,
- constraints (`time`, `deterministic`, `max_error`, memory bounds),
- example assertions if provided,
- compile-time rejection on violation.

### 6.5 Lockfile Manager

Produces deterministic decision record:

- selected rule id and version,
- target fingerprint,
- constraint set hash,
- verification outcome hash.

Default file: `thagore.intent.lock`.

## 7) Lockfile schema (draft)

```json
{
  "schema_version": 1,
  "thagore_version": "x.y.z",
  "target": "x86_64-pc-windows-msvc",
  "source_digest": "sha256:...",
  "entries": [
    {
      "intent_id": "src/app.tg:42:intent_loop_1",
      "goal": "reduce_sum",
      "selected_rule": "rule.reduce_sum.simd.v2",
      "constraints_digest": "sha256:...",
      "verification_digest": "sha256:..."
    }
  ]
}
```

## 8) Determinism contract

- Same source + same lock + same target => same generated code shape.
- Build fails if lock mismatches selected rule under `--intent=max --strict-lock`.
- No nondeterministic timing/random input in selection path.

## 9) Performance contract

### Build-time

- Non-intent projects: near-zero overhead (marker detection only).
- Intent projects:
  - `min`: bounded candidate set, fast selection.
  - `max`: broader search, slower build but better plan quality.

### Runtime

- No model inference.
- No network.
- No dynamic optimizer service.
- Runtime cost equivalent to generated native code path.

## 10) Safety and fallback policy

Default policy:

- If intent cannot be proven safe or constraints fail -> hard compile error.

Optional mode:

- `--intent-fallback=allow`: fallback to canonical implementation and emit warning.

Never allowed:

- silent degradation in strict CI mode.

## 11) Initial supported goals (MVP)

- `reduce_sum`
- `map_filter_reduce`
- `deduplicate_sorted`
- `binary_search`
- `string_contains`
- `dot_product`
- `polynomial_eval`
- `fibonacci_dp` *(experimental rewrite path)*
- `factorial_iterative` *(recursive factorial -> iterative loop)*
- `power_fast` *(linear multiplication -> binary exponentiation)*
- `gcd_euclid` *(subtractive gcd -> modulo Euclid)*
- `is_prime_fast` *(naive divisor scan -> sqrt-bounded prime check)*
- `count_divisors_sqrt` *(count divisors from O(n) to O(sqrt(n)))*
- `interval_cover_greedy` *(minimum sprinkler/interval cover on sorted points via greedy two-pointer)*
- `bit_peel_iterative` *(recursive bit peel -> iterative fold)*
- `sort_ascending`
- `search_element`
- `sqrt_bounded_loop`
- `auto_plan` *(pattern inference, deterministic heuristic)*

Each goal ships with 2-4 deterministic strategies max in MVP.

Automatic detection mode:

- `THAG_AUTO_OPT=1` (default): for plain `func` without `intent`, compiler still tries deterministic body-pattern detection and applies verified rewrites.
- `THAG_AUTO_OPT=0`: disables no-marker auto rewrites (baseline behavior).

## 12) Implementation plan

### Phase A: Syntax + parser plumbing

- Add AST nodes for `intent func/loop/calc/block`.
- Add parser and semantic header checks.
- Add diagnostic messages for invalid intent declarations.

Deliverables:

- parser tests,
- syntax error snapshots,
- no codegen changes yet.

### Phase B: Intent IR + matcher

- Introduce internal intent IR representation.
- Implement goal recognition and constraint parsing.
- Add `thagore intent explain`.

Deliverables:

- explain output tests,
- goal coverage matrix.

### Phase C: Rule library + verifier

- Implement initial rule library for MVP goals.
- Implement deterministic verifier path.
- Add compile-time failure/fallback controls.

Deliverables:

- property tests for semantic equivalence,
- constraint compliance tests.

### Phase D: Cost model + lockfile

- Implement target-aware candidate scoring.
- Add `thagore intent lock`.
- Add lockfile read/validate in normal `build`.

Deliverables:

- reproducibility tests on repeated builds,
- lock mismatch diagnostics.

### Phase E: Production hardening

- performance benchmarks,
- documentation and migration guide,
- CI integration (`intent doctor`, strict lock checks).

Deliverables:

- benchmark report,
- release checklist.

## 13) Testing strategy

- Unit tests: matcher, parser, constraints, cost model.
- Differential tests: intent vs canonical implementation.
- Property tests: randomized inputs for equivalence.
- Golden tests: `intent explain` and lockfile output.
- Performance tests: before/after for each goal family.

## 14) Risks and mitigations

Risk: rule explosion and maintenance cost.  
Mitigation: strict MVP goal list + versioned rule registry.

Risk: incorrect optimization under complex constraints.  
Mitigation: compile-time verifier + strict mode default.

Risk: build-time regression for intent-heavy projects.  
Mitigation: `min|max` modes and lock reuse.

## 15) Non-goals (initially)

- Free-form natural language algorithm synthesis.
- Auto-inventing unknown algorithms outside approved rule space.
- Runtime adaptive optimization service.

## 16) Relationship with Drago

- `intent` optimizes internal algorithm strategy.
- `drago` handles package/distribution workflows.

Combined workflow:

1. `thagore intent lock src/app.tg`
2. `drago pack src/app.tg --lock drago.lock`

Both remain optional and independent from default `build`.
