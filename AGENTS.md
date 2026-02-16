# Repository Guidelines

## Project Structure & Module Organization
- `src/`: Self-hosted Thagore compiler sources (preferred location for new language features).
  - `src/frontend/`: lexer, parser, semantic, lowering.
  - `src/frontend/native/`: native parser/typechecker/typed IR helpers.
  - `src/backend/llvm/`: LLVM-facing emitter adapter.
  - `src/backend/native/`: native LLVM IR emitter core.
  - `src/cli/`: CLI entry/compat layer.
- `lib/`: standard library modules used by compiler/runtime (`env`, `fs`, `process`, etc.).
- `examples/`: runnable Thagore programs (`hello.tg`, `fib.tg`, etc.).
- `scripts/`: automation utilities (`bootstrap.bat`, `benchmark_fib.py`).
- `.github/workflows/`: CI, selfhost matrix, release, and bootstrap seed pipelines.
- `legacy/`: C++ Stage0 launcher/toolchain. Treat as frozen except critical bootstrap/runtime fixes.

## Build, Test, and Development Commands
- `cmd /c scripts\bootstrap.bat`: full bootstrap cycle (Stage0 -> Stage1 -> Stage2 -> hello_v2).
- `cmake --build legacy/build --config Debug`: build legacy Stage0 binaries/libraries.
- `legacy\stage0.exe build src/thagore.tg -o stage1.exe`: produce Stage1 compiler.
- `stage1.exe build src/thagore.tg -o stage2.exe`: self-host Stage2 compiler.
- `python scripts/benchmark_fib.py`: benchmark Python vs Thagore native on `fib(35)`.
- `cmake -B legacy/build -DBUILD_TESTING=ON && ctest --test-dir legacy/build --output-on-failure`: run C++ tests.

## Bootstrap & Release Policy
- CI/Release is **Stage1-only bootstrap** on all OS. Do not add Stage0 fallback branches.
- Local scripts may use Stage0 only when `ALLOW_STAGE0_BOOTSTRAP=true` is explicitly set.
- Seed tag for bootstrap assets: `v0.3.25-stage1-seed`.
- Stage1 seed archives must support both binaries:
  - `bin/thagore`
  - `bin/thag` (legacy-compatible path used by existing macOS seed tar).
- Keep workflow traces explicit (`[CMD] ...`) so Stage1 -> Stage2 execution is auditable.

## Bootstrap Governance (Mandatory)
- `legacy/` stays in repo only as emergency fallback inventory, not default bootstrap path.
- Any change touching `.github/workflows/` that impacts bootstrap must pass all:
  - `Policy No Stage0`,
  - `Stability Policy`,
  - `CI` (3 OS),
  - `Selfhost Matrix` (3 OS).
- Never reintroduce implicit fallback logic:
  - no `gh release download` without explicit `BOOTSTRAP_STAGE1_TAG`,
  - no hidden Stage0 branch in CI/Release jobs,
  - no compiler-output fallback path (`fallback to existing compiler binary`, `fallback to previous stage2 binary`, `allow_missing_output`).
- Seed rotation rule:
  - promote seed tag only via successful `Seed Stage1 Assets` run,
  - then update `BOOTSTRAP_STAGE1_TAG` in `ci.yml`, `selfhost-matrix.yml`, `release.yml`, and `bootstrap-seed.yml`.
- Release discipline:
  - release build must consume Stage1 seed asset of the active seed tag,
  - generated stage trace artifact is required for audit.

## Coding Style & Naming Conventions
- Thagore (`*.tg`): 4-space indentation, colon-based blocks, clear function boundaries.
- Use `snake_case` for variables/functions.
- File names must be lowercase alnum only, no `_` or `-` (example: `semantic/pass.tg`, `backend/native/emitter.tg`).
- Keep changes minimal and localized; avoid broad refactors in the same PR.
- Prefer implementing language features in `src/frontend/` and `src/backend/`, not `legacy/`.

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
- CI must pass both:
  - `Stability Policy` workflow,
  - `CI` workflow (all matrix OS),
  - `Selfhost Matrix` workflow (Stage1 -> Stage2 -> Stage2).
