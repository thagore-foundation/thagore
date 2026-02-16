# Thagore Static Intent Engine (SIE)

Implementation approval: Approved  
Status: Implemented (MVP + CI hardening baseline)  
Owner: Thagore Compiler / Optimizer  
Last updated: 2026-02-15

## 1) Vision

`SIE` = `Static Intent Engine`.

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

Control extensions (implemented in Stage0 intent preprocessor path):

```tg
intent func cover_plants(...) -> i32:
    goal: interval_cover_greedy
    strategy: greedy.sweep.v1
    constraints:
        deterministic == true
```

- `goal`: choose algorithm family (or `auto_plan`).
- `strategy`: optional strategy pinning; when present, strategy selection is locked to that rule id.
  - runtime CLI enforces this pin deterministically and fails if strategy is unknown or incompatible with the selected goal.
- disable per function:

```tg
intent func slow_path(...) -> i32:
    goal: off
```

or inside constraints:

```tg
constraints:
    intent == false
```

Runtime CLI treats both forms as deterministic disable controls (`selected_rule = rule.intent.off`).

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
thagore build <entry.tg> --intent-policy=safe|fast|debug
```

Command behavior:

- `intent doctor`: validates toolchain and intent rules availability.
- `intent lock`: materializes selected rewrite plans and checksums.
- `intent explain`: shows matched patterns, candidates, and chosen plans.
- `--intent=off`: skip intent pipeline.
- `--intent=min`: local deterministic matcher + rewrite + verify.
- `--intent=max`: deeper search in known strategy space (still deterministic).
- `--intent-policy=safe|fast|debug`: preset wrapper around mode/fallback/strictness.
  - `safe`: defaults to `intent=max`, `fallback=deny`, `strict-lock=on`.
  - `fast`: defaults to `intent=min`, `fallback=allow`, `strict-lock=off`.
  - `debug`: defaults to `intent=off`, `fallback=allow`, `strict-lock=off`.
  - explicit flags still win (`--intent=...`, `--intent-fallback=...`, `--strict-lock`, `--no-strict-lock`).
- environment: `THAG_INTENT_POLICY=safe|fast|debug` applies the same preset defaults.

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
- `binary_search_sorted` *(linear scan on sorted data -> binary search rewrite)*
- `lower_bound_sorted` *(linear first-`>=x` scan on sorted data -> binary lower_bound rewrite)*
- `upper_bound_sorted` *(linear first-`>x` scan on sorted data -> binary upper_bound rewrite)*
- `count_less_sorted` *(sorted count `< x` via binary lower_bound)*
- `count_less_equal_sorted` *(sorted count `<= x` via binary upper_bound)*
- `count_greater_sorted` *(sorted count `> x` via `n - upper_bound`)*
- `count_greater_equal_sorted` *(sorted count `>= x` via `n - lower_bound`)*
- `count_equal_sorted` *(sorted count `== x` via two binary bounds)*
- `count_not_equal_sorted` *(sorted count `!= x` via `n - count_equal`)*
- `count_range_sorted` *(sorted count `[lo..hi]` via two binary bounds)*
- `count_outside_range_sorted` *(sorted count outside `[lo..hi]` via `n - count_range`)*
- `two_sum_sorted_exists` *(nested-loop pair-sum on sorted data -> two-pointer scan)*
- `string_contains`
- `dot_product`
- `polynomial_eval`
- `fibonacci_dp` *(experimental rewrite path)*
- `tribonacci_dp` *(recursive tribonacci -> iterative DP)*
- `factorial_iterative` *(recursive factorial -> iterative loop)*
- `power_fast` *(linear multiplication -> binary exponentiation)*
- `gcd_euclid` *(subtractive gcd -> modulo Euclid)*
- `is_prime_fast` *(naive divisor scan -> sqrt-bounded prime check)*
- `count_divisors_sqrt` *(count divisors from O(n) to O(sqrt(n)))*
- `interval_cover_greedy` *(minimum sprinkler/interval cover on sorted points via greedy two-pointer)*
- `bit_peel_iterative` *(recursive bit peel -> iterative fold)*
- `sum_squares_formula` *(looped sum of squares -> closed form)*
- `sum_cubes_formula` *(looped sum of cubes -> closed form)*
- `sum_even_squares_formula` *(sum of even squares up to `n` -> closed form)*
- `sum_odd_squares_formula` *(sum of odd squares up to `n` -> closed form)*
- `sum_even_cubes_formula` *(sum of even cubes up to `n` -> closed form)*
- `sum_odd_cubes_formula` *(sum of odd cubes up to `n` -> closed form)*
- `sum_even_formula` *(sum of even numbers up to `n` -> closed form)*
- `sum_odd_formula` *(sum of odd numbers up to `n` -> closed form)*
- `sort_ascending`
- `search_element`
- `sqrt_bounded_loop`
- `auto_plan` *(pattern inference, deterministic heuristic)*

Each goal ships with 2-4 deterministic strategies max in MVP.

Automatic detection mode:

- `THAG_AUTO_OPT=1` (default): for plain `func` without `intent`, compiler still tries deterministic body-pattern detection and applies verified rewrites.
- `THAG_AUTO_OPT=0`: disables no-marker auto rewrites (baseline behavior).
- `THAG_INTENT_EXPLAIN=1`: emit human-readable per-function explain lines for explicit `intent func` blocks (applied/skipped, selected/candidate rule, reason).
- `THAG_INTENT_TRACE=1`: full trace mode (directive parsing + applied/skipped diagnostics).

Strategy pinning examples currently recognized in Stage0 intent preprocessor:

- `dp.fib.v1`
- `dp.trib.v1`
- `math.factorial.loop.v1`
- `math.pow.binary_exp`
- `math.gcd.euclid`
- `number.prime.sqrt.v1`
- `number.divisors.sqrt.v1`
- `greedy.sweep.v1`
- `search.binary.v1`
- `search.lower_bound.v1`
- `search.upper_bound.v1`
- `search.count_less.v1`
- `search.count_less_equal.v1`
- `search.count_greater.v1`
- `search.count_greater_equal.v1`
- `search.count_equal.v1`
- `search.count_not_equal.v1`
- `search.count_range.v1`
- `search.count_outside_range.v1`
- `search.two_sum.v1`
- `math.sum_squares.formula.v1`
- `math.sum_cubes.formula.v1`
- `math.sum_even_squares.formula.v1`
- `math.sum_odd_squares.formula.v1`
- `math.sum_even_cubes.formula.v1`
- `math.sum_odd_cubes.formula.v1`
- `math.sum_even.formula.v1`
- `math.sum_odd.formula.v1`

### 11.0 Adaptive Matching Layer (new)

To scale beyond strict MVP templates, Stage0 now includes a deterministic matcher-normalization layer:

- condition normalization for `if (...)` / `while (...)` (strip redundant outer parentheses),
- comparator equivalence matching (e.g. `arr[i] >= x` and `x <= arr[i]` are treated as same intent shape),
- tolerant `return` matching (`return x` and `return(x)`),
- loop-bound extraction tolerant to reversed form (`i < n` and `n > i`).

This significantly improves `auto_plan` behavior on real-world style variance without introducing non-determinism.

Reference smoke:

- source: `examples/intent_adaptive_style_auto_plan.tg`
- checker: `scripts/intent_adaptive_smoke.py`
- command: `py -3 scripts/intent_adaptive_smoke.py --compiler legacy/build/Release/thag.exe`

Runtime auto-plan addition:

- runtime intent planner now also has deterministic **function-name heuristics** when body-pattern inference is not matched.
- this expands practical auto-plan coverage for goals like:
  - bounds/count search families (`lower_bound`, `count_range`, ...),
  - math/number families (`gcd`, `pow`, `prime`, `divisor`, ...),
  - interval cover (`sprinkler/cover`),
  - sum formula families (`sum_even`, `sum_squares`, ...).

### 11.0.1 Rule Budget And Registry Gate (new)

To prevent unbounded rule growth, Stage0 now supports a deterministic rule registry gate:

- registry file: `docs/idea/intent_rule_registry.txt`
- controls:
  - `budget.total=<N>` global rewrite-family budget,
  - `budget.family.<family>=<N>` per-family cap,
  - `rule=<rule.id>` allowlist (only listed rules are eligible).
- runtime behavior:
  - if selected rule is not in registry -> rewrite skipped with `registry-rule-disabled`,
  - if global/family budget is exhausted -> rewrite skipped with explicit reason.

Environment override:

- `THAG_INTENT_REGISTRY=<path>` to use a custom registry file.

Runtime CLI note:

- runtime intent planner now supports the same registry format as an **opt-in gate** via `THAG_INTENT_REGISTRY`.
- if set and `enabled=1`, `intent lock/build` will fail on:
  - rule not in allowlist (`registry-rule-disabled`),
  - `budget.total` exhausted,
  - `budget.family.<name>` exhausted.
- if unset, runtime planner keeps default behavior (no registry enforcement).

CI/static gate script:

- `scripts/intent_budget_gate.py`
- command: `python scripts/intent_budget_gate.py`

The gate verifies:

- driver rule set matches registry allowlist,
- total and per-family caps are not exceeded.

### 11.1 Visual Example: Sprinkler Cover

Problem shape:

- Given sorted tree positions and sprinkler intervals `[left, right]`.
- Return minimal number of sprinklers to cover all trees, or `-1`.

Before intent (naive scan each step):

```tg
func min_sprinklers_scan(trees: [i32; 128], n: i32, lefts: [i32; 128], rights: [i32; 128], m: i32) -> i32:
    let i = 0
    let used = 0
    while (i < n):
        let need = trees[i]
        let best = need - 1
        let j = 0
        while (j < m):
            if (lefts[j] <= need):
                if (rights[j] > best):
                    best = rights[j]
            j = j + 1
        if (best < need):
            return -1
        used = used + 1
        while ((i < n) and (trees[i] <= best)):
            i = i + 1
    return used
```

With intent (same function body, pinned strategy):

```tg
intent func min_sprinklers_scan(trees: [i32; 128], n: i32, lefts: [i32; 128], rights: [i32; 128], m: i32) -> i32:
    goal: interval_cover_greedy
    strategy: greedy.sweep.v1
    constraints:
        deterministic == true
```

Compiler-selected rewrite uses two-pointer greedy sweep:

- pointer `j` only moves forward once (no full re-scan for each tree),
- choose farthest `bestRight` among intervals starting before current tree,
- jump covered tree block in one step.

Measured benchmark in this repo:

- command: `py -3 scripts/benchmark_sprinkler_intent.py --compiler legacy/stage0.exe --runs 3`
- native median: `1.422901s`
- intent median: `0.076150s`
- speedup: `18.69x` (median)

Notes:

- exact numbers depend on machine/OS/load,
- this example is intended as a direct, reproducible visual comparison.

### 11.2 Visual Example: Bounds Pack (`lower/upper/count`)

Files:

- `examples/intent_real_bounds_naive.tg`
- `examples/intent_real_bounds_intent.tg`

Intent variant pins:

- `goal: lower_bound_sorted` + `strategy: search.lower_bound.v1`
- `goal: upper_bound_sorted` + `strategy: search.upper_bound.v1`
- `goal: count_equal_sorted` + `strategy: search.count_equal.v1`

Benchmark command:

- `py -3 scripts/benchmark_bounds_intent.py --compiler legacy/build/Release/thag.exe --runs 3`

Measured benchmark in this repo (same machine/session):

- native median: `5.959336s`
- intent median: `4.274823s`
- speedup: `1.39x` (median)

This pack is useful to validate that one `intent` build can optimize multiple sorted-query kernels in the same program.

### 11.3 Visual Example: Advanced Pack (`greater/range + square formulas`)

Files:

- `examples/intent_real_advanced_pack_naive.tg`
- `examples/intent_real_advanced_pack_intent.tg`

Intent variant pins:

- `goal: count_greater_sorted` + `strategy: search.count_greater.v1`
- `goal: count_greater_equal_sorted` + `strategy: search.count_greater_equal.v1`
- `goal: count_range_sorted` + `strategy: search.count_range.v1`
- `goal: sum_even_squares_formula` + `strategy: math.sum_even_squares.formula.v1`
- `goal: sum_odd_squares_formula` + `strategy: math.sum_odd_squares.formula.v1`

Benchmark command:

- `py -3 scripts/benchmark_advanced_pack_intent.py --compiler legacy/build/Release/thag.exe --runs 3`

Measured benchmark in this repo (same machine/session):

- native median: `1.071554s`
- intent median: `0.521441s`
- speedup: `2.05x` (median)

This pack demonstrates multi-rule composition across sorted counting kernels and closed-form numeric rewrites in one deterministic intent build.

### 11.4 Visual Example: Ultra Pack (`not-equal/outside-range + parity cubes`)

Files:

- `examples/intent_real_ultra_pack_naive.tg`
- `examples/intent_real_ultra_pack_intent.tg`

Intent variant pins:

- `goal: count_not_equal_sorted` + `strategy: search.count_not_equal.v1`
- `goal: count_outside_range_sorted` + `strategy: search.count_outside_range.v1`
- `goal: sum_even_cubes_formula` + `strategy: math.sum_even_cubes.formula.v1`
- `goal: sum_odd_cubes_formula` + `strategy: math.sum_odd_cubes.formula.v1`

Benchmark command:

- `py -3 scripts/benchmark_ultra_pack_intent.py --compiler legacy/build/Release/thag.exe --runs 3`

Measured benchmark in this repo (same machine/session):

- native median: `0.827634s`
- intent median: `0.343419s`
- speedup: `2.41x` (median)

This pack stresses mixed sorted-count and parity-cube formulas and confirms deterministic rewrite composition with matching runtime outputs.

### 11.5 Visual Example: Two-Sum Sorted (`O(n^2)` -> `O(n)`)

Files:

- `examples/intent_real_two_sum_naive.tg`
- `examples/intent_real_two_sum_intent.tg`

Intent variant pin:

- `goal: two_sum_sorted_exists` + `strategy: search.two_sum.v1`

Benchmark command:

- `python scripts/benchmark_twosum_intent.py --compiler legacy/build/Release/thag.exe --runs 3`

Measured benchmark in this repo (same machine/session):

- native median: `0.322789s`
- intent median: `0.083727s`
- speedup: `4.24x` (median)

This example shows a real asymptotic rewrite from nested scan (`O(n^2)`) to two pointers (`O(n)`) on sorted data.

### 11.6 Visual Example: Tribonacci DP Rewrite

Files:

- `examples/intent_real_tribonacci_naive.tg`
- `examples/intent_real_tribonacci_intent.tg`

Intent variant pin:

- `goal: tribonacci_dp` + `strategy: dp.trib.v1`

Expected output parity:

- both variants print `755476` for `n = 24`

This extends the DP family beyond Fibonacci while keeping deterministic compile-time rewrite behavior.

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
