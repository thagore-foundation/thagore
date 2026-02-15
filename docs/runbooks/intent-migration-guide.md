# Intent Migration Guide

This guide describes how to roll existing Thagore projects into deterministic `intent` mode.

## 1) Prerequisites

- Use a compiler build that supports:
  - `thagore intent doctor`
  - `thagore intent explain`
  - `thagore intent lock`
  - `thagore build --intent=off|min|max`
  - `--intent-fallback=deny|allow`
  - `--strict-lock` + `--intent-lock <path>`

## 2) Introduce intent blocks gradually

Start from low-risk hotspots:

1. Add one `intent` block/loop/function at a time.
2. Include:
   - `goal: ...`
   - `constraints: ...`
3. Optionally add `examples:` assertions for stronger verifier checks.

Example:

```tg
intent loop i in 0..n:
    goal: reduce_sum
    constraints:
        deterministic == true
        time <= O(n)
```

## 3) Validate before locking

```bash
thagore intent doctor src/app.tg
thagore intent explain src/app.tg --mode=max
```

Check that:

- goals are matched,
- selected rules are expected,
- verification is `ok`.

## 4) Materialize lockfile

```bash
thagore intent lock src/app.tg -o thagore.intent.lock --mode=max
```

Commit `thagore.intent.lock` with source changes.

## 5) Enable strict CI gate

For CI/release branches:

```bash
thagore build src/app.tg --intent=max --strict-lock --intent-lock thagore.intent.lock
```

Behavior:

- mismatch or missing lock entry => hard fail,
- stable source + lock + target => reproducible plan selection.

## 6) Rollout policy

- Local dev:
  - `--intent=max --intent-fallback=allow` during active migration.
- Protected branch / release:
  - `--intent=max --strict-lock` with committed lockfile.

## 7) Troubleshooting

- `Intent validation failed`: check header/goal/constraints/examples format.
- `unsupported goal`: migrate to one of MVP goals.
- `selected_rule mismatch`: regenerate lock after intentional intent change.
- `source digest mismatch`: lockfile is stale for current source content.
