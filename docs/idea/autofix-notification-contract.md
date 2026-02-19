# Thagore AutoFix Notification Contract

Implementation approval: Approved  
Status: Implemented (CLI + report contract complete)  
Owner: Thagore Build System / Diagnostics  
Last updated: 2026-02-19

## 1) Purpose

Define exactly what AutoFix must show to users whenever code is changed automatically.

Goals:

- full transparency for every applied fix,
- easy review of old/new code,
- deterministic machine-readable reports for CI and IDE.

## 2) Scope

Applies to:

- `thagore build ... --autofix=safe|aggressive`,
- `thagore fix apply`,
- `thagore fix dry-run` (proposed changes, not applied),
- `thagore fix explain`.

## 3) Required user-facing summary

After each run, CLI must print a summary block:

- session id,
- mode (`safe` or `aggressive`),
- files scanned,
- files changed,
- total fixes applied,
- total fixes skipped,
- rebuild result.

Example:

```text
[autofix] session=20260215-113412-a91f
[autofix] mode=safe files_scanned=12 files_changed=2
[autofix] fixes_applied=3 fixes_skipped=1
[autofix] rebuild=success
```

## 4) Required per-fix detail

For every applied fix, CLI output must include:

- fix id (`AFX-<rule>-<index>`),
- rule name and risk class (`safe|aggressive`),
- reason (mapped diagnostic code),
- file path,
- location:
  - start line/column,
  - end line/column,
- old snippet,
- new snippet,
- unified diff hunk,
- rollback hint.

Example:

```text
[autofix] AFX-missing-colon-0003 (safe)
  reason: PARSE_MISSING_BLOCK_COLON
  file: src/app.tg:42:1
  old: "if (x > 0)"
  new: "if (x > 0):"
  diff:
    @@ -42,1 +42,1 @@
    -if (x > 0)
    +if (x > 0):
  rollback: thagore fix rollback 20260215-113412-a91f
```

## 5) Old/new snippet rules

- `old` and `new` are mandatory for every modification.
- Snippets should be trimmed but preserve meaningful whitespace.
- For multi-line patches:
  - include first 5 changed lines in CLI,
  - include full patch in report file.
- For file creation/deletion:
  - `old` or `new` may be empty, but diff is still required.

## 6) Report artifact (JSON)

Each session writes:

- `.thagore/fix/reports/<session-id>.json`

JSON fields (minimum):

```json
{
  "session_id": "20260215-113412-a91f",
  "mode": "safe",
  "entry": "src/app.tg",
  "summary": {
    "files_scanned": 12,
    "files_changed": 2,
    "fixes_applied": 3,
    "fixes_skipped": 1,
    "rebuild": "success"
  },
  "fixes": [
    {
      "fix_id": "AFX-missing-colon-0003",
      "rule": "missing_colon_block_header",
      "risk_class": "safe",
      "diagnostic_code": "PARSE_MISSING_BLOCK_COLON",
      "file": "src/app.tg",
      "range": {
        "start_line": 42,
        "start_col": 1,
        "end_line": 42,
        "end_col": 12
      },
      "old_snippet": "if (x > 0)",
      "new_snippet": "if (x > 0):",
      "diff_unified": "@@ -42,1 +42,1 @@ ...",
      "applied": true
    }
  ]
}
```

## 7) Dry-run behavior

`thagore fix dry-run` must show:

- the same per-fix detail as apply mode,
- `applied=false` status,
- explicit line: `no source files were modified`.

## 8) Explain command behavior

`thagore fix explain` must include for each candidate:

- candidate rule,
- confidence score,
- why selected or rejected,
- semantic risk label,
- exact old/new preview for selected rule.

## 9) Redaction and safety

- Secrets in snippets must be masked when matched by secret patterns.
- Long lines should be truncated in CLI output (full content still in local report if safe).
- Binary files are never auto-patched; report as skipped with reason.

## 10) Exit code and failure reporting

If any fix is applied but rebuild fails:

- exit non-zero,
- print:
  - applied fix count,
  - failing diagnostic after fixes,
  - path to session report.

If patch conflict occurs:

- no partial silent success,
- print conflicting fix ids and files,
- include conflict details in report.

## 11) Rollback UX

Every successful apply run must print:

- session id,
- rollback command:
  - `thagore fix rollback <session-id>`

Rollback output must include:

- files restored count,
- any files that could not be restored and why.

## 12) IDE/LSP contract (optional phase)

For editor integration, publish a structured event per fix:

- `fix_id`,
- file uri/path,
- range,
- old/new text,
- risk class,
- applied status.

This contract should mirror session JSON fields for consistency.

