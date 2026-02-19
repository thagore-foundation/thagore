# RFC: Thagore Typestate

Implementation approval: Proposed  
Status: MVP implemented (initial)  
Owner: Thagore Compiler / Language Safety  
Last updated: 2026-02-19

## 1) Problem Statement

Many production incidents come from lifecycle errors, not algorithm errors:

- use before ready,
- use after close/release,
- invalid call order (`begin -> begin`, `commit -> commit`),
- branch merges that produce unknown state but still call strict APIs.

Impact:

- server/cloud: invalid external calls and retry storms,
- AI: wasted token/GPU cycles on invalid session state,
- game: frame spikes/crashes from invalid resource lifecycle.

## 2) Goal

Add a compile-time lifecycle safety primitive that is:

- simple for end users,
- optional and backward compatible,
- low/no runtime overhead.

Core idea: annotate API boundaries with `Type[State]`, let compiler reject invalid transitions.

## 3) Simplicity Principles

The feature must preserve Thagore's "simple by default" direction:

- do not require annotations everywhere,
- no ownership/lifetime syntax system in MVP,
- existing non-annotated code keeps current behavior,
- diagnostics should suggest direct next fix, not theory.

## 4) Language Surface (MVP)

### 4.1 State Set Declaration

```tg
state Session:
    Init
    Ready
    Closed
```

### 4.2 State-Tagged Type Slots

```tg
func open(cfg: Config) -> Session[Ready]:
    ...

func send(s: Session[Ready], msg: String) -> Session[Ready]:
    ...

func close(s: Session[Ready]) -> Session[Closed]:
    ...
```

### 4.3 Backward Compatibility

```tg
func legacy_send(s: Session, msg: String) -> i32:
    ...
```

If a type slot has no `[State]`, typestate checks are skipped for that slot.

## 5) Semantic Rules (MVP)

Compiler validation:

1. referenced state set exists,
2. referenced variant exists in that set,
3. call-site arg state matches parameter state tag,
4. returned expression matches declared return state tag,
5. variable reassignment must respect resulting state contract.

Example:

```tg
let s = open(cfg)        # Session[Ready]
s = close(s)             # Session[Closed]
s = send(s, "ping")      # error: needs Session[Ready]
```

## 6) Control Flow and Ambiguity

Tracking is flow-sensitive per symbol.

When two control-flow paths produce different states for the same variable:

- in `--state=on`: emit warning (`W_STATE_AMBIGUOUS`),
- in `--state=strict`: emit error (`E_STATE_AMBIGUOUS`).

Current MVP ambiguity sources:

- `if/else` merge with different resulting states,
- loop body (`while`/`for`) that mutates a stateful variable to a different state (because loop may run zero or more times).

Example:

```tg
if (need_close):
    s = close(s)         # Closed
else:
    s = send(s, "ok")    # Ready

send(s, "next")          # ambiguous
```

## 7) CLI and Build Modes

```bash
thagore build app.tg --state=off|on|strict
thagore state explain app.tg --json
thagore state explain app.tg --out .thagore/state/reports/custom.json
thagore state doctor app.tg --mode on
```

- `off`: ignore typestate rules.
- `on`: enforce hard mismatches, ambiguity as warning.
- `strict`: ambiguity is hard error.

`state explain` should be the debugging surface for teams adopting typestate.

## 8) Diagnostics Contract

Recommended codes:

- `E_STATE_UNKNOWN_SET`
- `E_STATE_UNKNOWN_VARIANT`
- `E_STATE_MISMATCH_ARG`
- `E_STATE_MISMATCH_RETURN`
- `E_STATE_INVALID_TRANSITION`
- `E_STATE_AMBIGUOUS`
- `W_STATE_AMBIGUOUS`

Every finding should include:

- symbol/call site,
- required state vs actual state,
- file and line/column,
- one-line fix hint.

## 9) Explain JSON Contract

Canonical schema and output constraints are defined in:

- `docs/idea/typestate-notification-contract.md`

This keeps CLI/IDE/CI aligned on one stable artifact format.

## 10) Why This Is a Core Language Feature

`typestate` can be a defining Thagore property like:

- "safe lifecycle by compile-time contract,"
- but without forcing complex syntax on every user.

Unlike lint-only approaches, typestate is part of semantic checking and build outcome.

## 11) Non-Goals (MVP)

- borrow checker / lifetime algebra,
- state unions (`State[A|B]`),
- typestate theorem proving across module graph,
- runtime typestate engine.

## 12) Performance Expectations

- compile-time only checks in parser/semantic stages,
- no runtime state machine inserted in MVP,
- no extra cost for projects in `--state=off`,
- bounded overhead by module-local state symbol tables.

## 13) Rollout Plan

1. Phase 1: opt-in on boundary APIs with `--state=on`.
2. Phase 2: enable `--state=strict` for critical modules.
3. Phase 3: enforce in CI with `state explain --json` artifact review.

## 14) Success Metrics

- fewer lifecycle-related bugs in production,
- fewer invalid calls/retries to external systems,
- higher compile-time catch rate for transition mistakes,
- no meaningful build/runtime regression for non-typestate code.
