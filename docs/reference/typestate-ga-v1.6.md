# Typestate GA (v1.6)

v1.6 promotes typestate checks from preview to GA usage in normal compiler flows.

## Supported diagnostics

- `E_TYPESTATE_001`
- `E_TYPESTATE_002`
- `E_STATE_UNKNOWN_SET`
- `E_STATE_UNKNOWN_VARIANT`
- `E_STATE_MISMATCH_ARG`
- `E_STATE_MISMATCH_RETURN`
- `E_STATE_INVALID_TRANSITION`
- `E_STATE_AMBIGUOUS`
- `W_STATE_AMBIGUOUS`

All state diagnostics include fix suggestions via the shared diagnostic hint registry.

## CLI workflows

Analyze state issues:

```bash
thagc state explain app.tg
thagc state explain app.tg --json
thagc state doctor app.tg
```

`state doctor` provides:

- code-level counts by diagnostic code
- full finding list with `file:line:column`
- remediation hints

## Example state declaration

```tg
state Session: Init | Ready | Closed
type Session = i32

func boot() -> Session[Init]:
  let s: Session[Init] = 1
  return s
```

Use state-annotated types at function boundaries to keep transitions explicit.
