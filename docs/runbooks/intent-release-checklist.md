# Intent Release Checklist

Use this checklist before cutting a release that includes `intent`.

## A) Feature and CLI surface

- [ ] `thagore intent doctor` returns `engine=ready`.
- [ ] `thagore intent explain <file> --json --mode=max` works.
- [ ] `thagore intent lock <file> --mode=max` writes valid lockfile.
- [ ] `thagore build <file> --intent=off|min|max` works.
- [ ] `--intent-policy=safe|fast|debug` preset behavior is correct.
- [ ] `--intent-fallback=deny|allow` behavior matches policy.
- [ ] `--strict-lock` requires `--intent=max` and fails on mismatch (`--no-strict-lock` override works).

## B) Determinism and lock contracts

- [ ] Repeated `intent lock` for the same source produces identical lockfile.
- [ ] Strict lock mode fails for:
  - missing lock,
  - missing entry,
  - selected rule mismatch,
  - digest mismatch.
- [ ] Determinism contract validated on target(s) in release matrix.

## C) Test coverage gates

- [ ] Unit tests cover parser/matcher/typecheck paths for intent syntax and goals.
- [ ] Golden tests cover `intent explain --json` and lockfile shape.
- [ ] Differential tests compare runtime behavior (intent vs canonical path).
- [ ] Property tests validate deterministic selected rule across repeated runs.
- [ ] `scripts/intent_suite.py` passes in CI.

## D) CI and workflow checks

- [ ] CI lane runs `intent doctor` and strict-lock gate.
- [ ] Linux CI lane blocks merge on intent suite failures (other OS may warn only).
- [ ] Runtime registry gate path (`THAG_INTENT_REGISTRY`) is covered by suite.
- [ ] Workflow enforces no C++ runtime build path (`clang++`, `cmake -S runtime` absent).

## E) Docs and migration readiness

- [ ] Migration guide is updated (`docs/runbooks/intent-migration-guide.md`).
- [ ] Benchmark report is updated (`docs/runbooks/intent-benchmark-report.md`).
- [ ] Known limitations and fallback policy documented.

## F) Sign-off

- [ ] Compiler owner sign-off.
- [ ] CI owner sign-off.
- [ ] Release manager sign-off.
