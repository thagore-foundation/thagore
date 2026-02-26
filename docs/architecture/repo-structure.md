# Repository Structure Guide

This document maps responsibilities to folders so contributors can navigate quickly.

## Top-level

- `compiler/`: compiler pipeline modules and implementation.
- `runtime/`: runtime ABI library.
- `stdlib/`: standard library sources.
- `contracts/`: behavior parity contracts.
- `tooling/`: extraction, comparison, packaging, and policy scripts.
- `tests/`: unit/integration/e2e/parity/soak lanes.
- `docs/`: architecture, contributor guides, ADRs, and runbooks.

## Compiler pipeline map

- `compiler/frontend`: source analysis (lexer/parser/typing).
- `compiler/middleend`: IR transformation and lowering.
- `compiler/backend`: LLVM/object generation.
- `compiler/driver`: CLI and pipeline orchestration.
- `compiler/shared`: cross-cutting utilities.

## Dependency direction

- Domain -> Application -> Infrastructure (inward dependencies only).
- No circular imports/includes between modules.
- Backend-specific concerns must not leak into frontend/middleend domain models.

