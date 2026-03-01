# Contributor Guide (Complete)

This guide is the canonical onboarding path for contributors working on Thagore compiler/runtime/tooling.

## 1. Prerequisites

- C++20 toolchain
- LLVM 21
- CMake + Ninja
- Python 3.11+
- GitHub account for pull request flow

## 2. Local setup

```bash
cmake -S . -B build-llvm21 -G Ninja -DLLVM_DIR=/usr/lib/llvm-21/lib/cmake/llvm
cmake --build build-llvm21 -j"$(nproc)"
```

## 3. Validation before commit

```bash
THAGC_BIN=build-llvm21/compiler/thagc python3 -m unittest discover -s tests -p "test_*.py"
rg -n --hidden --glob '!.git' -g '!AGENTS.md' -P '\b(TODO|FIXME|TBD|XXX)(\([^)]+\))?:' .
```

## 4. Architecture boundaries

- `frontend`: lexer/parser/AST/type rules only
- `middleend`: typed/core IR and lowering only
- `backend`: LLVM/object emission only
- `driver`: CLI orchestration only
- `shared`: diagnostics and common utility only

Do not introduce cross-layer shortcuts that break these boundaries.

## 5. Commit policy

- Conventional Commit message required.
- One logical change per commit.
- Keep repository runnable after each commit.

## 6. Pull request policy

- Include problem statement and approach.
- Include tests for behavior changes.
- Include docs updates for user-facing changes.
- Ensure CI is green before merge.

## 7. Release-impact changes

If your change affects release/install/update behavior:

1. update release workflows under `.github/workflows/`,
2. update runbooks under `docs/runbooks/`,
3. update `ROADMAP.md` milestones if gate status changed.

## 8. Community channels

- GitHub Discussions for design/support threads.
- Discord for release announcements and contributor updates.
- Registry contributions through pull requests to `thagore-foundation/registry`.
