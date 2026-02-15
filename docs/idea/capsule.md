# Thagore Capsule

Status: Draft proposal (not approved for implementation)  
Implementation approval: Not approved  
Owner: Thagore CLI / Tooling  
Last updated: 2026-02-15

## 1) Goal

`capsule` is a packaging mode that produces a self-contained deployable artifact from a Thagore app.

Primary promise:

- `thagore build` = compile/link for development and fast iteration.
- `thagore capsule build` = ship-ready artifact with runtime dependencies resolved and locked.

One-line product message:

`Write once, ship one file.`

## 2) Problem statement

Current `build` output is usually enough for local execution, but deployment can still fail because of:

- missing dynamic libraries (`.dll`, `.so`, `.dylib`),
- Python runtime/package mismatch when Python bridge is used,
- host machine differences (toolchain/runtime variance),
- ad-hoc scripts required to copy assets and dependencies,
- non-reproducible packaging between CI and local machines.

`capsule` solves this by making packaging a first-class compiler workflow.

## 3) Command model

Top-level namespace:

```bash
thagore capsule <subcommand> [options]
```

Recommended alias:

```bash
thagore build app.tg --capsule
# internally maps to:
thagore capsule build app.tg
```

## 4) Subcommands

### 4.1 `capsule build`

Build and package an application.

```bash
thagore capsule build <entry.tg> [-o <artifact>] [options]
```

Core options:

- `-o, --output <path>`: output artifact path.
- `--target <triple>`: target platform triple.
- `--mode <slim|full>`:
  - `slim`: bundle required native/runtime deps only.
  - `full`: include Python runtime + selected site packages when needed.
- `--python <auto|embed|external>`:
  - `auto`: embed only if Python bridge is detected.
  - `embed`: force embedded Python runtime.
  - `external`: do not embed Python; runtime must exist on host.
- `--asset <path>` (repeatable): include files/directories as runtime assets.
- `--lock <path>`: use explicit lockfile.
- `--deterministic`: fail if reproducibility cannot be guaranteed.
- `--sign <key-ref>`: sign output artifact.
- `--manifest <path>`: write resolved capsule manifest.

Output:

- Single executable when supported.
- Otherwise executable + sidecar capsule image with stable layout.

### 4.2 `capsule lock`

Resolve and lock packaging inputs without producing artifact.

```bash
thagore capsule lock <entry.tg> [-o thagore.capsule.lock]
```

Includes:

- dependency graph fingerprints,
- target and ABI expectations,
- python package/runtime resolution,
- asset digests.

### 4.3 `capsule doctor`

Preflight checks for packaging readiness.

```bash
thagore capsule doctor [<entry.tg>] [--target <triple>]
```

Checks:

- compiler/runtime availability,
- linker/toolchain availability,
- Python embedding prerequisites,
- forbidden non-portable dynamic deps,
- licensing metadata completeness (if enabled).

Exit code is non-zero on blocking issues.

### 4.4 `capsule inspect`

Print metadata from an existing capsule artifact.

```bash
thagore capsule inspect <artifact>
```

Shows:

- capsule version/schema,
- build target, build time, source hash,
- dependency list with hashes,
- signature status,
- embedded asset inventory.

### 4.5 `capsule verify`

Verify integrity/signature/reproducibility claims.

```bash
thagore capsule verify <artifact> [--lock <path>] [--strict]
```

Checks:

- content hash,
- lockfile match,
- signature validity,
- manifest consistency.

### 4.6 `capsule run`

Run a capsule artifact with isolated temp workspace.

```bash
thagore capsule run <artifact> [-- <args...>]
```

Useful for:

- testing exactly what will be shipped,
- smoke tests in CI without extraction scripts.

### 4.7 `capsule extract`

Extract artifact contents for debug/forensics.

```bash
thagore capsule extract <artifact> -o <dir>
```

### 4.8 `capsule publish`

Publish capsule artifact + metadata to release registry/storage.

```bash
thagore capsule publish <artifact> [--channel <stable|beta|nightly>]
```

## 5) UX examples

### Basic production package

```bash
thagore capsule build src/app.tg -o dist/app
```

### Build with lock + strict reproducibility

```bash
thagore capsule lock src/app.tg -o thagore.capsule.lock
thagore capsule build src/app.tg --lock thagore.capsule.lock --deterministic -o dist/app
```

### Python bridge app with embedded runtime

```bash
thagore capsule build src/py_app.tg --python embed --mode full -o dist/py_app
```

### CI preflight

```bash
thagore capsule doctor src/app.tg --target x86_64-pc-windows-msvc
```

## 6) Artifact contract

Capsule artifact must contain:

- compiled executable payload,
- runtime dependency bundle metadata,
- optional embedded dependency blobs (mode-dependent),
- manifest (`capsule.manifest.json`),
- optional signature block.

Minimum manifest fields:

- `schema_version`,
- `thagore_version`,
- `entry`,
- `target`,
- `build_mode`,
- `build_timestamp_utc`,
- `source_digest`,
- `deps[]` with `name`, `kind`, `version`, `digest`,
- `assets[]` with `path`, `digest`, `size`,
- `python` block (`mode`, `version`, `packages[]`),
- `signature` block.

## 7) Reproducibility and lockfile

Lockfile name:

- default: `thagore.capsule.lock`

Lockfile requirements:

- stable sorting of entries,
- normalized paths,
- cryptographic digest per entry,
- target-specific markers,
- no host-only absolute paths in strict mode.

`--deterministic` behavior:

- fail build if any unresolved nondeterminism remains,
- print exact offending inputs and paths.

## 8) Compatibility and migration

`build` remains unchanged by default.

Compatibility policy:

- no breaking change in existing `thagore build` behavior,
- `--capsule` is opt-in and maps to `capsule build`,
- capsule metadata schema is versioned.

## 9) Security model

Recommended baseline:

- checksum verification always on,
- optional artifact signing (`--sign`),
- `capsule verify` supported in CI and release gates.

Future additions:

- SBOM export,
- provenance attestation (SLSA-style),
- policy file for allowed/blocked dependencies.

## 10) Suggested implementation phases

### Phase 1 (MVP)

- `capsule build`, `capsule doctor`, `capsule inspect`
- native runtime dependency capture
- manifest generation

### Phase 2

- `capsule lock`, `capsule verify`
- deterministic mode
- CI integration examples

### Phase 3

- Python embed mode (`auto|embed|external`)
- signing + publish workflow
- extract/run debug tooling

## 11) Non-goals (initially)

- universal cross-compilation for every target from every host,
- full container replacement,
- dynamic plugin sandboxing.

## 12) CLI help text draft

```text
Thagore Capsule CLI

Usage:
  thagore capsule build <entry.tg> [-o output] [options]
  thagore capsule lock <entry.tg> [-o thagore.capsule.lock]
  thagore capsule doctor [entry.tg] [--target triple]
  thagore capsule inspect <artifact>
  thagore capsule verify <artifact> [--lock path] [--strict]
  thagore capsule run <artifact> [-- args...]
  thagore capsule extract <artifact> -o <dir>
  thagore capsule publish <artifact> [--channel stable|beta|nightly]
```
