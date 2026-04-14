# Bootstrap Publication Gate

This gate defines when `indev-rewrite` is allowed to be treated as the public
bootstrap line.

## Promotion Branch

- Source branch: `indev-rewrite`
- Promotion target: `main`

## Promotion Commit

The promotion commit is the current `indev-rewrite` HEAD that satisfies all
required workflows below on the same revision.

Do not promote a mixed set of runs from different commits.

## Required Workflows

The promotion commit must have all of these green:

- `Bootstrap Declaration Gate`
- `Bootstrap Selfhost Stage`
- `Selfhost Compiler Driver`
- `Bootstrap Probe`
- `Bootstrap Selfhost Bench`
- `Frontend Selfhost Ownership`

## Stability Requirement

Promotion requires at least two consecutive green rounds for the same bootstrap
surface:

1. one push-triggered green round on the promotion commit
2. one manual rerun or workflow-dispatch green round after that

The second round exists to catch cache-sensitive or timing-sensitive drift.

## Known Blocker Policy

Promotion is blocked if any of the following remain true:

- a required workflow is red or cancelled on the promotion commit
- a required workflow only passes on Linux or only on Windows
- bootstrap artifact reports drift between stage1 and stage2
- declaration-gate reports drift between pass1 and pass2
- selfhost ownership audit reports legacy paths outside `bootstrap/selfhost/**`
- bootstrap execution still depends on a known flaky runner setup without an
  explicit mitigation in-tree

Promotion is not blocked by issues that are outside the bootstrap path and have
an isolated owner and workaround.

## Current Bootstrap Surface

The current gate covers these selfhost surfaces:

- frontend stage slice: `scan`, `parse`, `check`
- frontend driver slice: `main`
- compiler driver slice: `compiler`
- lowering slice: `lower`
- backend adapter contracts
- nested bootstrap artifact chain through analysis, desugar, planning, phase,
  adapter, build/run, emit/link/verify, print flows, and rebuilt stage tools

## Exit Condition

`indev-rewrite` is ready for promotion when:

- the promotion commit satisfies all required workflows
- the stability requirement is met
- no direct bootstrap blocker remains open

## Operational Audit

Before promotion, run the publication audit from a maintainer machine against
the exact `indev-rewrite` HEAD being considered.

Use:

- `powershell -File tooling/ci/run_bootstrap_publication_audit.ps1`
- or `python tooling/ci/bootstrap_publication_audit.py ...` directly

Why this is local-first:

- GitHub Actions workflow dispatch only resolves workflow files that already
  exist on the repository default branch
- the promotion gate itself lives on `indev-rewrite` before promotion, so a
  branch-local audit workflow is not a reliable control surface yet
- the gate therefore relies on the checked-in audit script plus the existing
  required workflows listed above
