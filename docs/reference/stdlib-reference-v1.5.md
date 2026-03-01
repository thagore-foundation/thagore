# Thagore Standard Library Reference v1.5

## Core modules

- `std/core.tg`
  - `ok`, `fail`, `assert`
- `std/string.tg`
  - `concat`, conversion helpers, string utilities
- `std/list.tg`
  - list operations used by language tests/contracts

## Runtime-facing lib modules

- `lib/time.tg`
- `lib/fs.tg`
- `lib/process.tg`
- `lib/http.tg`
- `lib/ws.tg`
- `lib/db.tg`
- `lib/json.tg`
- `lib/env.tg`
- `lib/crypto.tg`
- `lib/tensor.tg`

## Usage style

Use project-level package workflows via `drago`, then compile through `thagc` pipeline.

## Stability note

v1.5 marks these modules as stable for production guidance and tutorial flows.
