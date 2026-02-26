# ADR 0001: Clean Architecture with Ports/Adapters

- Status: Accepted
- Date: 2026-02-26

## Context
The project is being rewritten in C++ with direct LLVM API usage while preserving language parity with the archived self-hosted compiler behavior.

## Decision
Use three layers:

1. Domain (pure models and language rules).
2. Application (use cases, orchestration through ports).
3. Infrastructure (LLVM, filesystem, process/linker adapters).

Dependencies point inward only. Domain does not depend on LLVM or OS process APIs.

## Consequences
- Easier backend/linker replacement.
- Better testability via port mocks.
- Lower risk of circular dependencies.

