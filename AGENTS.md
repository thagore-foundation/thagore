# Repository Guidelines

## Project Structure & Module Organization
- `src/`: Self-hosted Thagore compiler sources (preferred location for new language features).
  - `src/syntax/`: lexer/parser/token logic.
  - `src/compiler/`: AST, evaluator, and codegen-facing modules.
- `lib/`: standard library modules used by compiler/runtime (`env`, `fs`, `process`, etc.).
- `examples/`: runnable Thagore programs (`hello.tg`, `fib.tg`, etc.).
- `scripts/`: automation utilities (`bootstrap.bat`, `benchmark_fib.py`).
- `legacy/`: C++ Stage0 launcher/toolchain. Treat as frozen except critical bootstrap fixes.

## Build, Test, and Development Commands
- `cmd /c scripts\bootstrap.bat`: full bootstrap cycle (Stage0 -> Stage1 -> Stage2 -> hello_v2).
- `cmake --build legacy/build --config Debug`: build legacy Stage0 binaries/libraries.
- `legacy\stage0.exe build src/thg.tg -o stage1.exe`: produce Stage1 compiler.
- `stage1.exe build src/thg.tg -o stage2.exe`: self-host Stage2 compiler.
- `python scripts/benchmark_fib.py`: benchmark Python vs Thagore native on `fib(35)`.
- `cmake -B legacy/build -DBUILD_TESTING=ON && ctest --test-dir legacy/build --output-on-failure`: run C++ tests.

## Coding Style & Naming Conventions
- Thagore (`*.tg`): 4-space indentation, colon-based blocks, clear function boundaries.
- Use `snake_case` for variables/functions; keep module names lowercase (`codegen_llvm.tg`).
- Keep changes minimal and localized; avoid broad refactors in the same PR.
- Prefer implementing language features in `src/syntax/` and `src/compiler/`, not `legacy/`.

## Testing Guidelines
- Add or update an example under `examples/` for every language/runtime change.
- Validate both compile and runtime behavior:
  - compile: `stage2.exe build examples/<case>.tg -o <case>.exe`
  - run: `<case>.exe` and verify deterministic output.
- For performance-sensitive work, include `scripts/benchmark_fib.py` results in PR notes.

## Commit & Pull Request Guidelines
- Use descriptive Conventional-style messages:
  - `feat: ...`, `fix: ...`, `refactor: ...`, `test: ...`
- One logical change per commit; avoid mixing feature work and unrelated cleanup.
- PRs should include:
  - problem statement and scope,
  - files/modules touched,
  - verification commands + outputs,
  - risk notes (especially if `legacy/` is touched).

## Architecture Notes
- Current direction: freeze `legacy/` as Stage0 launcher.
- All new language features (e.g., new syntax/control flow) must be implemented in self-hosted Thagore under `src/`.
