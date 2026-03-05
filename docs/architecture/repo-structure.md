# Repository Structure Guide

This document maps responsibilities to folders so contributors can navigate quickly.

## Top-level

- `compiler/`: compiler pipeline modules and implementation.
- `runtime/`: runtime ABI library.
- `stdlib/`: standard library sources.
- `contracts/`: behavior parity contracts.
- `tooling/`: extraction, comparison, packaging, and policy scripts.
- `tests/`: unit/integration/e2e/parity/soak lanes plus milestone-specific suites (`tests/inference`, `tests/generics`, `tests/ownership`).
- `docs/`: architecture, contributor guides, ADRs, and runbooks.
- `examples/`: reference app templates (CLI, REST API, bot, algorithm visualizer).

## Compiler pipeline map

- `compiler/frontend`: source analysis (lexer/parser/typing).
- `compiler/middleend`: IR transformation and lowering.
- `compiler/mir`: mid-level IR model and ownership/borrow analyses.
- `compiler/hir`: typed high-level expression IR and inference/check helpers.
- `compiler/ty`: shared type model used by HIR/typechecking layers.
- `compiler/include/thagc/query`: query-cache contracts used by incremental compilation lanes.
- `compiler/backend`: LLVM/object generation.
- `compiler/driver`: CLI and pipeline orchestration.
- `compiler/shared`: cross-cutting utilities.

## Dependency direction

- Domain -> Application -> Infrastructure (inward dependencies only).
- No circular imports/includes between modules.
- Backend-specific concerns must not leak into frontend/middleend domain models.
