# Changelog

All notable changes to Thagore are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [v0.5.44] — 2026-02-25

### Added
- `scripts/fetch_llvm_prebuilt.py` — download LLVM 21 prebuilt binaries per target triple from
  GitHub LLVM releases; used by CI to embed LLVM into target packs (no system LLVM required by users)
- `scripts/install/thagup` — self-updater shell script (`thagup update`, `thagup install <tag>`,
  `thagup target add/remove/list`, `thagup self-update`, `thagup show`)
- `llvm_bundle` field in all 5 target `manifest.json` files — declares embedded LLVM 21.1.8
  with paths `llvm/bin/clang`, `llvm/bin/ld.lld`

### Changed
- `core-ci.yml` — rewritten: installs LLVM 21, downloads stage1 seed, runs real bootstrap
  (`scripts/bootstrap.sh` stage1→stage2→stage2b), executes test suite, uploads artifacts.
  Policy checks run in parallel `policy` job.
- `core-selfhost.yml` — rewritten: stage1→stage2→stage2b with SHA256 reproducibility check
  (stage2 vs stage2b), analogous to rustc stage3.
- `core-release.yml` — rewritten: real bootstrap → `fetch_llvm_prebuilt.py` per target →
  packages `thagc-core-<os>.tar.gz` + `thagc-target-<triple>-<os>.tar.gz` with embedded LLVM →
  generates `SHA256SUMS-thagc-<os>.txt` → publishes GitHub Release via `gh release create`.
- `core-docs-pages.yml` — builds Astro/Starlight docs and deploys to GitHub Pages.
- `scripts/install/thagup-init.sh` — completed: downloads `thagc-core-<os>.tar.gz` + target
  packs from GitHub Releases, installs to `~/.thagore/toolchains/stable/`, links
  `~/.thagore/bin/thagc`, updates `.bashrc`/`.zshrc`.
- Docs (`install/quick-start.mdx`, `install/release-installers.mdx`) — updated to reflect
  `thagc` binary name, new asset names, embedded LLVM, and `thagup` management commands.

---

## Template for future releases

```
## [vX.Y.Z] — YYYY-MM-DD

### Added
- ...

### Changed
- ...

### Fixed
- ...

### Removed
- ...
```

[Unreleased]: https://github.com/thagore-foundation/thagore/compare/HEAD...main
