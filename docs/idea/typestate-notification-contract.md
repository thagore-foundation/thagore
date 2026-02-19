# Thagore Typestate Notification Contract

Implementation approval: Proposed  
Status: Draft (contract)  
Owner: Thagore Compiler / Diagnostics  
Last updated: 2026-02-19

## 1) Purpose

Define exactly what `thagore state explain` must emit for humans and machines.

Goals:

- transparent typestate analysis results,
- stable JSON shape for CI and IDE integration,
- minimal, deterministic output that scales to large projects.

## 2) Scope

Applies to:

- `thagore state explain <entry>`,
- `thagore state explain <entry> --json`,
- `thagore state explain <entry> --out <file>`.

Out of scope:

- build/autofix patch logs,
- runtime telemetry.

## 3) Required CLI summary

Each run must print:

- session id,
- mode (`off|on|strict`),
- files analyzed,
- functions analyzed,
- errors count,
- warnings count,
- ambiguous-state count.

Example:

```text
[state] session=20260219-134520-c1a2
[state] mode=on files=6 functions=42
[state] errors=1 warnings=3 ambiguous=2
```

## 4) Required finding detail

For each rejected or risky site, CLI must include:

- finding id (`TSE-<kind>-<index>`),
- code (`E_STATE_*` or `W_STATE_*`),
- file and range,
- symbol (function/variable),
- required state,
- actual state,
- short reason,
- one-line fix hint.

Example:

```text
[state] TSE-mismatch-0007 E_STATE_MISMATCH_ARG
  at: src/session.tg:48:12
  symbol: send(s, msg)
  required: Session[Ready]
  actual: Session[Closed]
  hint: call open_session() or avoid close_session() before send()
```

## 5) Explain graph payload requirements

`state explain` must describe transitions per function:

- input state tags (params),
- output state tag (return),
- explicit edges (`from -> to`) when inferable from signatures/calls,
- blocked edges with reason.

If edge inference is not possible, emit empty edge list, never omit the key.

## 6) JSON report artifact

Default output path:

- `.thagore/state/reports/<session-id>.json`

Minimum JSON schema:

```json
{
  "schema_version": "1.0",
  "session_id": "20260219-134520-c1a2",
  "entry": "src/app.tg",
  "mode": "on",
  "summary": {
    "files_analyzed": 6,
    "functions_analyzed": 42,
    "errors": 1,
    "warnings": 3,
    "ambiguous": 2
  },
  "state_sets": [
    {
      "name": "Session",
      "variants": ["Init", "Ready", "Closed"]
    }
  ],
  "functions": [
    {
      "name": "send",
      "file": "src/session.tg",
      "range": {
        "start_line": 21,
        "start_col": 1,
        "end_line": 24,
        "end_col": 1
      },
      "params": [
        {
          "name": "s",
          "type": "Session",
          "state": "Ready"
        }
      ],
      "return": {
        "type": "Session",
        "state": "Ready"
      },
      "edges": [
        {
          "from": "Ready",
          "to": "Ready",
          "kind": "self"
        }
      ]
    }
  ],
  "findings": [
    {
      "finding_id": "TSE-mismatch-0007",
      "severity": "error",
      "code": "E_STATE_MISMATCH_ARG",
      "file": "src/session.tg",
      "range": {
        "start_line": 48,
        "start_col": 12,
        "end_line": 48,
        "end_col": 16
      },
      "symbol": "send(s, msg)",
      "required_state": "Session[Ready]",
      "actual_state": "Session[Closed]",
      "reason": "argument state mismatch",
      "hint": "ensure s is Ready before calling send"
    }
  ]
}
```

## 7) Mode behavior contract

- `off`: emit summary, skip typestate findings, set counts to zero.
- `on`: emit findings; ambiguous sites are warnings (`W_STATE_AMBIGUOUS`).
- `strict`: ambiguous sites are errors (`E_STATE_AMBIGUOUS`).

Mode must be explicit in both CLI and JSON.

## 8) Determinism and ordering

To keep CI diffs stable:

- sort findings by file, start_line, start_col, finding_id,
- keep stable state-set and function ordering per source order,
- use fixed key names and value casing.

## 9) Exit code contract

- `off`: always `0` unless parser/build infrastructure fails.
- `on`: non-zero only on `severity=error`.
- `strict`: non-zero on any error, including ambiguous-state errors.

## 10) IDE/LSP alignment

Diagnostics emitted to editor should map directly from `findings[*]` fields:

- code,
- severity,
- range,
- reason,
- hint.

No hidden transformation layer should change semantic meaning.
