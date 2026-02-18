# Thagore Build AutoFix (Guardian)

Implementation approval: Approved  
Status: Approved for implementation  
Owner: Thagore Build System / Diagnostics  
Last updated: 2026-02-15

## 1) Vision

`Build AutoFix` is a build-time system that can automatically repair safe, common source issues when compilation fails, then continue build.

Core promise:

- Keep developer flow unblocked for routine mistakes.
- Apply only deterministic and safe fixes.
- Never hide risky semantic changes.

## 2) Problem

Current build flow fails fast on syntax/type/import issues and requires manual patch loops.

Common repetitive failures:

- missing imports for std modules,
- trivial header/syntax mismatch (`:` blocks, malformed signatures),
- deprecated syntax forms that can be rewritten,
- strict parser/semantic formatting mismatches.

These are expensive in iteration time but usually trivial to repair.

## 3) Product principles

- Safe by default: only low-risk transformations auto-applied.
- Deterministic: same input + rules => same output.
- Transparent: every fix is logged with before/after diff.
- Reversible: patches can be rolled back.
- Opt-in strictness: CI can forbid auto-modification.

## 4) CLI design

Primary entry:

```bash
thagore build <entry.tg> --autofix=<off|safe|aggressive>
  [--autofix-workspace]
  [--autofix-max-iterations N]
  [--autofix-max-files N]
  [--autofix-exclude a,b,c]
```

Companion commands:

```bash
thagore fix doctor <entry.tg>
thagore fix dry-run <entry.tg>
thagore fix apply <entry.tg> [--level safe|aggressive]
thagore fix explain <entry.tg> [--json]
thagore fix rollback <fix-session-id>
```

Modes:

- `off`: current behavior (no source changes).
- `safe`: only verified non-semantic rewrites.
- `aggressive`: includes higher-confidence semantic rewrites with explicit warnings.

## 5) User workflow

### 5.1 Normal local development

```bash
thagore build src/app.tg --autofix=safe
```

Behavior:

1. Build fails on first pass.
2. AutoFix analyzes diagnostics.
3. Applies safe patches.
4. Rebuilds automatically.
5. Emits fix report.

### 5.2 CI strict mode

```bash
thagore build src/app.tg --autofix=off
thagore fix dry-run src/app.tg
```

CI can require zero suggested fixes or require a pre-committed fix lock.

## 6) System architecture

Pipeline integration:

1. Parse/semantic/typecheck fails.
2. Compiler diagnostics may provide machine-applicable span suggestions.
3. If compiler suggestion API is unavailable, AutoFix emits a built-in per-entry suggestion feed (`.thagore/fix/suggestions/<entry>.builtin.jsonl`) for deterministic typo fixes.
4. Suggestion loader consumes per-entry feeds only (no global `latest.jsonl` fallback) to avoid cross-file contamination.
5. Diagnostic normalizer maps raw errors to canonical error codes.
6. Rule matcher selects applicable fix rules (fallback after compiler suggestions).
7. Span/patch planner resolves ordering and conflict (overlap-safe, descending span apply).
8. Patch applier edits source.
9. Rebuild and re-verify.
10. Persist fix session log + patch metadata.

Current built-in span suggestions include:
- `PARSE_TYPO_RETURN` for common `return` typos,
- `PARSE_DEPRECATED_FN_KEYWORD` (`fn` -> `func`),
- `PARSE_DUPLICATE_MODIFIER` (duplicate `pub` cleanup),
- `PARSE_MISSING_BLOCK_COLON` (header colon insertion).

Guard rails:

- max iterations per build (default 3),
- max files touched (default 10),
- abort on conflict or ambiguous patch.

## 7) Fix rule taxonomy

### 7.1 Safe rules (auto-apply in `safe`)

- Insert missing `:` for block headers when unambiguous.
- Normalize deprecated keyword/header forms to supported forms.
- Add missing std import when symbol resolution is exact.
- Remove duplicate/conflicting trivial modifiers.
- Canonical whitespace/indentation repairs required by parser.

### 7.2 Aggressive rules (only `aggressive`)

- Infer function return annotation from clear return literals.
- Rewrite ambiguous API calls to best-known signatures.
- Replace unknown symbols with nearest validated symbol by distance + scope checks.

### 7.3 Never auto-apply

- Data model changes (`struct` field mutation),
- algorithmic rewrites,
- control-flow rewrites changing business logic,
- security-sensitive API replacements.

## 8) Fix session record

Each autofix run produces a session artifact:

- default path: `.thagore/fix/sessions/<timestamp>-<id>.json`

Required fields:

- session id,
- entry file,
- build target,
- mode (`safe/aggressive`),
- diagnostics seen,
- applied rules list,
- file patches with hashes,
- rebuild outcome.

## 9) Optional fix lockfile

Purpose:

- deterministic replay of approved fixes in team/CI environments.

Default name:

- `thagore.fix.lock`

Behavior:

- `thagore fix apply --lock thagore.fix.lock` replays only allowed fixes.
- Build fails if required fix is missing from lock under strict policy.

## 10) Diagnostics and explainability

`thagore fix explain` must provide:

- error code mapping,
- candidate rules with confidence,
- why a rule was selected/rejected,
- expected semantic risk class.

Output modes:

- human text (default),
- JSON for IDE/LSP/CI ingestion.

Detailed notification and reporting contract is specified in:

- `docs/idea/autofix-notification-contract.md`

## 11) Safety policy

Default local policy:

- `safe` allowed.
- `aggressive` requires explicit flag.

Default CI policy:

- `off` or `dry-run` only.
- optional allowlist via lockfile.

Hard stop conditions:

- patch touches protected paths,
- rule confidence below threshold,
- rebuild introduces new higher-severity errors.

## 12) Performance expectations

When `--autofix=off`:

- no measurable regression from existing build.

When enabled:

- overhead bounded by iteration cap and file-touch limits.
- most safe-fix flows should finish within one additional build pass.

## 13) IDE/LSP integration (future)

- real-time `fix preview` from compiler diagnostics,
- one-click “apply safe fixes”,
- rule-level enable/disable per workspace.

## 14) Implementation plan

### Phase A: Foundations

- Define canonical diagnostic codes.
- Build patch engine and unified diff writer.
- Add `fix dry-run` and `fix explain`.

### Phase B: Safe rule pack

- Implement 10-15 safe rules.
- Add conflict resolver and deterministic ordering.
- Integrate `--autofix=safe` into `build`.

### Phase C: Lock + rollback

- Add session persistence and rollback command.
- Add `thagore.fix.lock` replay mode.
- Add CI strict policy flags.

### Phase D: Aggressive mode

- Add conservative semantic inference rules.
- Add mandatory warnings and confidence thresholds.
- Add richer risk reporting.

## 15) Test strategy

- Rule unit tests for each diagnostic -> patch mapping.
- Golden tests for generated diffs.
- Idempotency tests (applying same fix twice yields no change).
- Safety tests (forbidden files/rules never modified).
- Regression tests for build speed in `autofix=off`.
- Local regression gate scripts: `scripts/autofix_off_regression.py` and `scripts/autofix_off_regression.ps1`.

## 16) Risks and mitigations

Risk: hidden behavior changes from auto edits.  
Mitigation: safe-only default, explicit risk class, mandatory logs.

Risk: flaky fix ordering.  
Mitigation: deterministic planner with stable sort and tie-break keys.

Risk: developer distrust.  
Mitigation: dry-run mode, explain output, rollback support.

## 17) Relationship with other ideas

- Works with `intent`: AutoFix cleans syntax/semantic friction before intent optimization.
- Works with `drago`: AutoFix stabilizes build input before packaging/distribution.

Recommended combined pipeline:

1. `thagore fix dry-run src/app.tg`
2. `thagore build src/app.tg --autofix=safe`
3. `thagore intent lock src/app.tg`
4. `drago pack src/app.tg --lock drago.lock`
