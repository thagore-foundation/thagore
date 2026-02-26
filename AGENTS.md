# AGENTS.md

This file defines mandatory engineering rules for all contributors and coding agents in this repository.

## 1. Architecture & Design (Mandatory)

- Follow OOSAD and iterative RUP.
- Apply DDD:
  - define domain boundaries explicitly,
  - maintain a shared ubiquitous language in code/docs/PRs.
- Use Clean Architecture with ports/adapters.
- Keep domain pure:
  - no LLVM/process/filesystem/network/platform concerns in domain layer.
- Keep infrastructure swappable through interfaces.
- Apply SOLID and GRASP.
- Avoid circular dependencies.
- Keep modules cohesive and responsibilities focused.

## 2. Quality & Testing (Mandatory)

- Use TDD where feasible.
- Follow test pyramid:
  - unit tests for domain/application rules,
  - integration tests for adapters/pipeline,
  - e2e/parity tests for compiler behavior.
- Add CI-ready tests for all meaningful changes.
- Enforce formatting and linting in CI and local workflow.
- Do not merge changes without passing relevant tests/checks.

## 3. Versioning & Change Discipline

- Use SemVer for releases.
- Use Conventional Commits for all commits.
- Record major architectural/design decisions as ADRs under `docs/adr/`.
- Keep one logical change per commit.

## 4. OSS Hygiene (Mandatory)

Keep these files present and updated:

- `README.md`
- `.github/CONTRIBUTING.md`
- `.github/CODEOWNERS`
- `.github/SECURITY.md`
- issue templates under `.github/ISSUE_TEMPLATE/`
- `.github/PULL_REQUEST_TEMPLATE.md`

Documentation must support fast onboarding for new contributors.

## 5. Build & Reproducibility

- Ensure fast local setup with clear commands.
- Keep builds reproducible and deterministic where applicable.
- Keep CI workflow definitions aligned with repository architecture and quality gates.

## 6. Delivery Expectations

- Clear folder structure.
- Minimal coupling between modules.
- Strong docs for contributors.
- No hidden architectural shortcuts that break layering or contracts.

## 7. Enforcement Priority

When instructions conflict, follow this order:

1. Explicit user request in current conversation.
2. This `AGENTS.md`.
3. Other repository defaults/policies.

