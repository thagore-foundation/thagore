# Drago Package Publish Runbook

This runbook documents a practical publish/install workflow for reusable Thagore libraries.

## 1) Package naming rules

`drago.toml` package names must use only:

- letters (`a-z`, `A-Z`)
- digits (`0-9`)
- underscore (`_`)
- dash (`-`)

Do not use `.` in package names. Dots are reserved for module path separators.

Example:

- valid: `zalo`, `zalo-tg`
- invalid: `zalo.tg`

## 2) Library surface for `import <package>`

When resolving a package import, resolver checks package entry files in order:

1. `main.tg`
2. `src/main.tg`
3. `<package>.tg`

For a library package, provide a stable public facade in `main.tg` (and optionally `<package>.tg`).

## 3) Prepare metadata

Minimal `drago.toml`:

```toml
[package]
name = "zalo"
version = "0.1.0"
entry = "src/main.tg"

[dependencies]
```

## 4) Pre-publish checks

```bash
drago check
drago test
```

## 5) Publish request

`drago publish` requires `GITHUB_TOKEN`:

```bash
GITHUB_TOKEN=<token> drago publish
```

Expected output:

```text
📦 Publishing package metadata
publish request created
```

## 6) Consumer workflow

After package is available in registry:

```bash
drago add zalo
drago install
```

And in code:

```tg
import zalo
```

No source cloning is required for consumers.
