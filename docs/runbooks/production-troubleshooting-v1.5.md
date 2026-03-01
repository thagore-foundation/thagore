# Production Troubleshooting Runbook (v1.5)

## Scope

Use this runbook for production incidents involving:

- compiler pipeline failures,
- runtime concurrency regressions,
- memory model diagnostics,
- IO client behavior,
- deployment/startup/binary-size regressions.

## Fast triage

1. Confirm compiler and toolchain versions.
2. Reproduce with:
   - `thagc check`
   - `thagc state explain`
3. Run focused test suites for affected area.

## Area-specific checks

### Concurrency

- Run integration + soak concurrency suites.
- Confirm no open P0/P1 in `contracts/concurrency/p0_p1_registry.json`.

### Memory model

- Run send/sync integration suite.
- Verify diagnostics include actionable `Rc`/`Arc` fix suggestions.

### IO

- Run runtime behavior and IO integration suites.
- Validate cancellation/timeout expectations.

### Deploy UX

- Check startup and binary-size budgets.
- Verify packaging + cross-platform smoke workflows.

## Escalation

- open incident issue with minimal repro,
- attach failing command output,
- include target platform and release tag.
