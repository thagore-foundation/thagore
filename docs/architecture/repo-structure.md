# Repository Structure Guide

This document maps responsibilities to folders so contributors can navigate quickly.

## Top-level

- `crates/`: Rust library crates for the compiler pipeline and runtime-adjacent services.
- `stdlib/`: standard library sources.
- `tooling/`: packaging metadata, release installers, and community policy docs.
- `tests/fixtures/`: end-to-end compiler fixtures.
- `docs/`: architecture, contributor guides, ADRs, and runbooks.
- `playground/`: static browser playground plus the WASM bridge crate.
- `tools/`: binaries and developer-facing command-line tools.
- `.github/workflows/`: CI, deploy, release, and Pages automation.

## Compiler pipeline map (current Rust layout)

- `crates/lexer`: tokenisation, interning, lexer diagnostics.
- `crates/parser`: recursive-descent parsing and AST construction.
- `crates/ast`: arena-allocated AST nodes and pretty-printers.
- `crates/typeck`: type inference, module-aware checking, diagnostics.
- `crates/module_graph`: import resolution, dependency graph, import/export tables.
- `crates/ir`: typed IR and lowering.
- `crates/codegen`: LLVM 14 backend, object emission, linker orchestration.
- `crates/interpreter`: browser-safe interpreter for the playground.
- `tools/thagore-cli`: session pipeline, `thagc`, `thagore`.
- `tools/thagore-fmt`: formatter binary and library surface.
- `tools/thagore-lsp`: LSP server.

## Dependency direction

- Domain -> Application -> Infrastructure (inward dependencies only).
- No circular imports/includes between modules.
- Backend-specific concerns must not leak into frontend/middleend domain models.

## Release and installation layout

- `tooling/packaging/targets.json`: canonical release-target matrix.
- `tooling/release/package_release.py`: archive packager.
- `tooling/release/generate_release_manifest.py`: release manifest and checksum generator.
- `tooling/release/thagup.sh`: POSIX installer.
- `tooling/release/thagup.ps1`: PowerShell installer.
- `tooling/community/release-support-policy.md`: stable/extended/nightly support policy.
- `.github/workflows/release.yml`: semver release pipeline.
- `.github/workflows/nightly.yml`: prerelease nightly pipeline.
