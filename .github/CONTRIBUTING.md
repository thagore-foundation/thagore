# Contributing to Thagore Compiler (`thagc`)

Thanks for helping improve the official compiler of Thagore.

## Engineering Standards

- Keep Domain pure and independent from infrastructure.
- Use ports/adapters for infrastructure integration.
- Avoid circular dependencies.
- Keep modules cohesive and focused.
- Follow SOLID and GRASP.

## Local Setup (Linux)

```bash
sudo apt-get update
sudo apt-get install -y clang llvm-dev cmake ninja-build python3
cmake -S . -B build -G Ninja
cmake --build build -j
```

## Test and Validation

```bash
python3 -m compileall tooling tests
python3 -m unittest discover -s tests -p "test_*.py"
python3 tooling/policy/check_baseline_branch.py --branch backup/main-archive-20260226-211152
python3 tooling/policy/check_no_circular_headers.py --root compiler/include/thagc
```

## Commit and Versioning

- Use Conventional Commits.
- Mention SemVer impact (`major` / `minor` / `patch`) in PR description.

## Pull Request Checklist

- [ ] Scope is focused and cohesive.
- [ ] Tests added/updated and passing.
- [ ] No architecture boundary violations.
- [ ] Docs updated when behavior changed.
- [ ] ADR added/updated for major design decisions.

## Required References

- ADR index: `docs/adr/`
- Ubiquitous language: `docs/domain/ubiquitous-language.md`
- PR template: `.github/PULL_REQUEST_TEMPLATE.md`
