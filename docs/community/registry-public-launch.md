# Drago Registry Public Launch

This document defines the production launch posture for the public Drago registry.

## Launch scope

- Registry repository: `thagore-foundation/registry`
- Package manager client: `drago`
- Public install/update flows:
  - `drago add <package>`
  - `drago update`
  - `drago audit`

## Registry tiers

- authentic: curated and verified packages
- publish: community-submitted packages awaiting stricter review
- unregistered: direct source references outside curated registry sets

## Operational checklist

1. Release channels point to signed `thagc` + `drago` binaries.
2. Registry index files are versioned and reviewed in pull requests.
3. `drago` warns users when package trust tier is lower than authentic.
4. `drago audit` is available in standard project workflow.
5. Release announcement references registry usage guide and trust model.

## Security baseline

- No executable package hooks at install time.
- Hash-locked dependency snapshots in lock file.
- Package provenance visible from registry metadata.
- CI checks run on registry pull requests before merge.

## Public launch status

- Registry client integration in `drago`: active.
- Public release communication workflow: active via `.github/workflows/community-ops.yml`.
- Documentation for users and contributors: active in repository docs.
