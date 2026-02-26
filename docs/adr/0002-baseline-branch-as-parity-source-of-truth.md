# ADR 0002: Baseline Branch as Source of Truth

- Status: Accepted
- Date: 2026-02-26

## Context
We need full parity 1:1 for syntax, semantics, and CLI behavior.

## Decision
Freeze parity against `backup/main-archive-20260226-211152`.

Compatibility snapshots in `compatibility/` are generated and versioned from this branch.

## Consequences
- Explicit contract for migration and regression checks.
- Repeatable parity extraction in CI.
- Prevents accidental drift from the legacy behavior baseline.

