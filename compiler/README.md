# Compiler Module Map

This directory is organized by compiler pipeline roles so contributors can quickly find the right area.

- `frontend/`: lexer, parser, AST, and type rules.
- `middleend/`: typed/core IR transformations and lowering contracts.
- `backend/`: LLVM emission and object-generation adapters.
- `driver/`: CLI command handling and build pipeline orchestration.
- `shared/`: diagnostics and shared utilities (filesystem/process abstractions).
- `include/thagc/`: headers grouped by the same module responsibilities.
- `src/`: implementation files grouped by the same module responsibilities.

