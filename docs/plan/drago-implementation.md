# Drago Implementation Plan

> Drago is the package manager and daily-use build tool for Thagore.
> It is written in pure Thagore (.tg), compiled by thagc, and lives in a
> separate repository. No C code in the drago repo.

---

## 1. Project Overview

### What is Drago

Drago is to Thagore what Cargo is to Rust — the tool developers use every day.
`thagc` is the low-level compiler (like `rustc`). Drago wraps it and provides
project management, dependency resolution, building, testing, and publishing.

### Repositories

```
thagore repo:  /media/lehungquangminh/QM_SSD/thagore   (compiler + runtime + stdlib, C++)
drago repo:    /media/lehungquangminh/QM_SSD/drago      (package manager, pure .tg)
```

### Architecture

```
User runs `drago build`
    ↓
drago reads drago.toml + drago.lock
    ↓
drago resolves dependencies from ~/.thagore/packages/
    ↓
drago calls thagc to compile each module
    ↓
drago calls thagc to link → single binary output
```

### Why pure .tg (no C in drago repo)?

All heavy lifting (string ops, file I/O, process execution, TOML parsing,
HTTP) is implemented in the thagore runtime (C++ in thagore repo) and exposed
via `extern func` through stdlib `.tg` wrappers. Drago imports these wrappers
and writes pure logic in Thagore.

This requires v0.9 stdlib to be complete before Drago can be built.

---

## 2. Thagore Language Capabilities (v0.9 target — What Drago Uses)

### Language features (available since v0.5-v0.8)

- `func` / `pub func` with params and return types
- `let`, assignment, `return`
- `if`/`else`, `while`, `for i in start..end:`
- `match` on integers/enums with `_` wildcard
- `break`/`continue` with labels
- `defer`
- `struct` with typed fields, construction, field access
- `enum` with variants (with i32 payloads)
- `impl Type` methods with `self`
- Closures (simple, i32 captures)
- `f"interpolated {strings}"`
- `extern func` declarations (call C runtime functions)
- `print("string")` via runtime
- Arithmetic and comparisons on i32/f32/f64
- Tuples `(a, b, c)` and arrays `[1, 2, 3]`
- `import a.b.c` / `from a.b.c import func`
- `Rc<T>` / `Arc<T>` ownership

### Stdlib (available after v0.9 is complete)

```
from std.string import concat, split, join, trim, contains, len, from_int, to_int
from std.list import append, sort, filter, map, reduce
from std.core import assert, ok, fail
from lib.fs import read, write, exists, mkdir, readdir, remove, getcwd, path_join
from lib.process import run, capture, argv, argc, env, exit
from lib.toml import parse, get_str, get_int, get_section, get_keys
from lib.http import http_get, http_post
from lib.time import now_ms, sleep_ms
from lib.map import new, put, get, free
```

These all call real C++ implementations in the thagore runtime via extern func.
No stubs.

---

## 3. Drago Project Structure

```
/media/lehungquangminh/QM_SSD/drago/
├── drago.toml                    # self-referencing manifest
├── src/
│   ├── main.tg                   # entry point — CLI dispatcher
│   ├── cli/
│   │   ├── parser.tg             # parse argv into Command enum
│   │   └── output.tg             # formatted terminal output (emoji style)
│   ├── project/
│   │   ├── manifest.tg           # read/write drago.toml
│   │   ├── lockfile.tg           # read/write drago.lock
│   │   └── scaffold.tg           # drago new — create project template
│   ├── deps/
│   │   ├── resolver.tg           # dependency resolution from lock/manifest
│   │   ├── registry.tg           # fetch package info from GitHub registry
│   │   ├── downloader.tg         # download + extract tarballs
│   │   └── cache.tg              # manage ~/.thagore/packages/ + compiled/
│   ├── build/
│   │   ├── compiler.tg           # call thagc for compilation
│   │   ├── linker.tg             # call thagc for linking
│   │   └── runner.tg             # execute built binary
│   └── test/
│       └── runner.tg             # discover + run test files
├── tests/
│   └── test_manifest.tg
└── README.md
```

**Zero C files in this repo.** Everything uses stdlib imports.

---

## 4. Storage Design

### Directory layout

```
~/.thagore/
├── bin/                # thagc + drago binaries
├── cache/              # download tarballs (offline backup)
│   ├── http-1.0.0.tar.gz
│   └── json-1.2.0.tar.gz
├── packages/           # extracted source (single copy, global)
│   ├── http/1.0.0/
│   │   ├── drago.toml
│   │   └── src/*.tg
│   └── json/1.2.0/
│       ├── drago.toml
│       └── src/*.tg
└── compiled/           # compiled cache (per target triple)
    └── http/1.0.0/
        ├── x86_64-linux/
        │   ├── http.o
        │   └── meta.json
        └── aarch64-macos/
            ├── http.o
            └── meta.json
```

### Cache invalidation (meta.json)

```json
{
  "thagc_version": "0.9.0",
  "target": "x86_64-linux",
  "source_checksum": "sha256:abc123...",
  "flags": "-O2",
  "compiled_at": "2026-03-01T10:30:00Z"
}
```

Cache hit when ALL match: thagc_version + source_checksum + target + flags.
Any mismatch → recompile from source.

### Rules

- No node_modules/ per-project copy. Ever.
- No hidden pip-style install locations.
- Single copy per version in ~/.thagore/packages/.
- Same package, different versions → both exist side-by-side.
- Same project, two versions of same package → NOT allowed (hard rule).
- User can always see what is cached: `drago cache list`.

---

## 5. Registry Design (GitHub-Based, Zero Server)

### Registry repo

```
github.com/thagore-lang/registry/
├── authentic.yaml     # official packages from thagore-foundation
├── community.yaml     # community-reviewed packages
├── publish.yaml       # newly published, not yet reviewed
└── .github/workflows/
    ├── validate-pr.yml    # auto-check on PR: repo exists, no conflict, valid toml
    └── auto-review.yml    # auto-merge if all checks pass
```

### YAML format

```yaml
# authentic.yaml
http:
  repo: thagore-lang/http
  version: 1.0.0
  description: "HTTP client for Thagore"
  checksum: sha256:abc123...
  maintainer: thagore-foundation

# community.yaml
csv:
  repo: community-member/thag-csv
  version: 0.3.0
  description: "CSV parser"
  checksum: sha256:def456...
  reviewed_by: reviewer-username

# publish.yaml
xml-parser:
  repo: someguy/thag-xml
  version: 0.1.0
  description: "XML parser"
  submitted: 2026-03-01
  status: pending-review
```

### Trust tiers (in order of search priority)

1. **authentic** — official, no warning
2. **community** — reviewed, no warning
3. **publish** — new, show warning
4. **raw github repo** — unregistered, show strong warning + confirm prompt

### Publish flow

```
Developer forks thagore-lang/registry
    → adds entry to publish.yaml
    → creates PR
    → GitHub Actions validates: repo exists, drago.toml valid, name not taken
    → auto-merge if pass
    → package live in publish.yaml
```

### Promote flow

```
publish.yaml → community maintainer reviews → PR to community.yaml
community.yaml → thagore-foundation reviews → PR to authentic.yaml
```

---

## 6. Output Log Design

### drago build

```
  🔨 Building myapp v0.1.0

  ✓ http       1.0.0  (cached)
  ✓ json       1.2.0  (cached)
  ○ csv        0.3.0  compiling...
  ✓ csv        0.3.0  (520ms)
  ○ src/main.tg       compiling...
  ✓ src/main.tg       (340ms)
  ○ linking...
  ✓ linked             (120ms)

  ✅ Built myapp → target/myapp.bin (1.2 MB, 1.4s)
```

### drago run

```
  🔨 Building myapp v0.1.0
  ✅ Built (cached, 0.1s)

  🚀 Running myapp
  ─────────────────────
  Hello World
  Server started on :8080
```

### drago add (authentic)

```
  📦 Adding json@1.2.0

  ✓ Found json 1.2.0 (authentic)
  ✓ Downloaded (24 KB, 0.3s)
  ✓ Updated drago.toml
  ✓ Updated drago.lock

  ✅ Added json@1.2.0
```

### drago add (publish — warning)

```
  📦 Adding xml-parser@0.1.0

  ⚠ xml-parser 0.1.0 (publish — chưa kiểm duyệt)
  ✓ Downloaded (18 KB, 0.2s)
  ✓ Updated drago.toml
  ✓ Updated drago.lock

  ⚠ Package chưa được kiểm duyệt. Dùng drago audit để kiểm tra.
```

### drago add (unregistered — strong warning)

```
  📦 Adding user/experiment

  ⚠⚠ user/experiment (unregistered — chưa phát hành)
     Repo: github.com/user/experiment
     Không có trên registry. Chưa được ai review.
     Tiếp tục? [y/N] y

  ✓ Cloned (156 KB, 0.8s)
  ✓ Updated drago.toml
  ✓ Updated drago.lock

  ⚠⚠ Package chưa phát hành. Dùng tự chịu rủi ro.
```

### drago test

```
  🧪 Testing myapp v0.1.0

  ✓ test_parse_json          (12ms)
  ✓ test_parse_empty         (3ms)
  ✗ test_parse_invalid       (8ms)
    │ expected: Err("invalid token")
    │ got:      Err("unexpected EOF")
    │ at: tests/test_json.tg:24
  ✓ test_serialize           (5ms)

  3 passed  1 failed  (28ms)
```

### drago new

```
  ✨ Created myapp

  myapp/
  ├── drago.toml
  └── src/
      └── main.tg

  Get started:
    cd myapp
    drago run
```

### drago cache

```
  📁 Drago Cache

  Source     12 packages    4.2 MB
  Compiled    8 packages   18.6 MB (x86_64-linux)
  Tarballs  12 files        3.1 MB
  ─────────────────────────────────
  Total                    25.9 MB

  drago cache clean     xóa compiled cache
  drago cache purge     xóa tất cả
```

### Compile errors

```
  🔨 Building myapp v0.1.0

  ✗ src/main.tg

  E0003: type mismatch
    3 │  let x: i32 = "hello"
      │               ^^^^^^^
      expected i32, got string

  E0001: undefined variable
    7 │  print(y)
      │        ^
      'y' is not defined in this scope

  ❌ Build failed (2 errors)
```

### Output design rules

| Rule | Detail |
|------|--------|
| Emoji prefix per action | 🔨 build, 🚀 run, 📦 add, 🧪 test, ✨ new, 📁 cache |
| 3 status symbols only | ✓ green (done), ✗ red (fail), ⚠ yellow (warn) |
| `───` separator | separates drago output from program output |
| Right-align time + size | easy scanning |
| Errors show source line + `^` | no need to open file |
| `--quiet` / `--verbose` flags | default covers 90% cases |
| Max 80 columns | works on any terminal |

---

## 7. drago.toml Format

```toml
[package]
name = "myapp"
version = "0.1.0"
entry = "src/main.tg"
description = "My awesome app"
license = "MIT"
authors = ["Name <email>"]

[dependencies]
http = "1.0.0"                            # authentic/community/publish
json = { version = "1.2.0" }             # explicit version
utils = { path = "../my-utils" }          # local path dependency
experiment = { git = "user/experiment" }  # raw github repo
```

### drago.lock format

```toml
[[package]]
name = "http"
version = "1.0.0"
source = "thagore-lang/http"
checksum = "sha256:abc123..."

[[package]]
name = "json"
version = "1.2.0"
source = "thagore-lang/json"
checksum = "sha256:def456..."
```

---

## 8. CLI Commands

| Command | Description |
|---------|-------------|
| `drago new <name>` | Create new project |
| `drago build` | Build project |
| `drago run` | Build + run |
| `drago test` | Discover + run tests |
| `drago add <package>` | Add dependency |
| `drago remove <package>` | Remove dependency |
| `drago install` | Install all deps from drago.toml |
| `drago update` | Update deps to latest compatible |
| `drago publish` | Publish to registry (PR to publish.yaml) |
| `drago fmt` | Format .tg files |
| `drago check` | Typecheck without compiling |
| `drago cache list` | List cached packages + sizes |
| `drago cache size` | Show total cache size |
| `drago cache clean` | Delete compiled cache |
| `drago cache purge` | Delete all cache |
| `drago cache purge --unused` | Delete packages not used by any project |
| `drago audit` | Check dependencies for known issues |

---

## 9. Implementation Checklist

### Phase 1 — CLI Parser + Output
- [ ] `src/main.tg` — entry point, read argv via `lib.process.argv/argc`, dispatch to command handler
- [ ] `src/cli/parser.tg` — parse argv into command enum: New, Build, Run, Test, Add, Remove, Install, Update, Publish, Fmt, Check, Cache
- [ ] `src/cli/output.tg` — formatted output functions: print_header, print_success, print_warning, print_error, print_step, print_separator (using emoji design from section 6)
- [ ] Verify: `drago --help` prints usage, `drago build` prints header

### Phase 2 — Project Management
- [ ] `src/project/manifest.tg` — read drago.toml via `lib.toml` + `lib.fs`: parse [package] and [dependencies] sections
- [ ] `src/project/manifest.tg` — write drago.toml: add/remove dependency entries via `lib.fs.write`
- [ ] `src/project/lockfile.tg` — read drago.lock: parse [[package]] entries
- [ ] `src/project/lockfile.tg` — write drago.lock: serialize package entries with checksums
- [ ] `src/project/scaffold.tg` — `drago new`: create directory via `lib.fs.mkdir`, write drago.toml + src/main.tg template
- [ ] Verify: `drago new myapp` creates correct project structure

### Phase 3 — Build Pipeline
- [ ] `src/build/compiler.tg` — call `thagc build` via `lib.process.run/capture` on each .tg file
- [ ] `src/build/compiler.tg` — check compiled cache before compiling dependencies
- [ ] `src/build/compiler.tg` — write meta.json via `lib.fs.write` after successful compile
- [ ] `src/build/linker.tg` — call `thagc` to link all .o files into final binary
- [ ] `src/build/runner.tg` — execute built binary via `lib.process.run`
- [ ] Verify: `drago build` compiles a simple project, `drago run` executes it

### Phase 4 — Dependency Resolution
- [ ] `src/deps/resolver.tg` — read drago.lock, map package names to ~/.thagore/packages/ paths
- [ ] `src/deps/resolver.tg` — detect version conflicts (same package, different versions in one project → error)
- [ ] `src/deps/cache.tg` — check if package exists via `lib.fs.exists` in ~/.thagore/packages/<name>/<version>/
- [ ] `src/deps/cache.tg` — check compiled cache: read meta.json, validate thagc_version + checksum + target + flags
- [ ] `src/deps/cache.tg` — drago cache list/size/clean/purge commands via `lib.fs.readdir/remove`
- [ ] Verify: build with cached deps skips recompilation

### Phase 5 — Registry + Download
- [ ] `src/deps/registry.tg` — fetch authentic.yaml/community.yaml/publish.yaml via `lib.http.http_get` from GitHub raw content
- [ ] `src/deps/registry.tg` — search package name across 3 tiers in order
- [ ] `src/deps/registry.tg` — display trust tier warning for publish/unregistered
- [ ] `src/deps/downloader.tg` — download tarball via `lib.http.http_get` from GitHub release
- [ ] `src/deps/downloader.tg` — extract tarball to ~/.thagore/packages/<name>/<version>/ (call `tar` via `lib.process.run`)
- [ ] `src/deps/downloader.tg` — verify checksum after download
- [ ] `src/deps/downloader.tg` — offline mode: if package in cache + checksum match, skip download
- [ ] Verify: `drago add http` fetches from registry, installs, updates toml + lock

### Phase 6 — Test Runner
- [ ] `src/test/runner.tg` — discover test files via `lib.fs.readdir`: tests/ directory, files matching *test*.tg
- [ ] `src/test/runner.tg` — compile + run each test via `lib.process.capture`, capture exit code + output
- [ ] `src/test/runner.tg` — format results: ✓ pass / ✗ fail with time
- [ ] `src/test/runner.tg` — on failure: show expected vs got + file:line
- [ ] Verify: `drago test` discovers and runs tests with formatted output

### Phase 7 — Publish + Audit
- [ ] `src/deps/registry.tg` — `drago publish`: validate drago.toml, create PR to publish.yaml via GitHub API (`lib.http.http_post`)
- [ ] `src/deps/registry.tg` — `drago audit`: check all deps against known issues list
- [ ] Verify: `drago publish` creates correct PR to registry repo

### Phase 8 — Polish
- [ ] `drago fmt` — call `thagc fmt` via `lib.process.run`
- [ ] `drago check` — call `thagc` with typecheck-only flag via `lib.process.run`
- [ ] `drago remove <package>` — remove from drago.toml + drago.lock
- [ ] `drago update` — check registry for newer versions, update lock
- [ ] `drago install` — install all deps from drago.toml that are not in cache
- [ ] Error messages: all errors show context + suggestion
- [ ] `--quiet` and `--verbose` flags on all commands
- [ ] `drago --version` prints version
- [ ] README.md with getting started guide

### Phase 9 — Integration Test
- [ ] Create a sample project with 2 dependencies
- [ ] `drago new sample && cd sample`
- [ ] `drago add http` — installs from registry
- [ ] `drago build` — compiles with deps
- [ ] `drago run` — runs successfully
- [ ] `drago test` — tests pass
- [ ] `drago cache list` — shows correct packages + sizes
- [ ] Full offline test: disconnect network, `drago build` still works from cache

---

## 10. Build & Release

### Build drago itself

```bash
# From thagore repo: build thagc first (must be v0.9+ with real stdlib)
cd /media/lehungquangminh/QM_SSD/thagore
cmake -S . -B build-llvm21 -G Ninja -DLLVM_DIR=/usr/lib/llvm-21/lib/cmake/llvm
cmake --build build-llvm21 -j$(nproc)

# From drago repo: compile drago with thagc (pure .tg, no C helpers needed)
cd /media/lehungquangminh/QM_SSD/drago
THAGC=/media/lehungquangminh/QM_SSD/thagore/build-llvm21/compiler/thagc
$THAGC build src/main.tg -o drago.bin
```

### Release package

```
thagore-install/
├── bin/
│   ├── thagc        ← compiler (single binary, embedded runtime + LLVM)
│   └── drago        ← package manager (compiled from pure .tg)
└── thagup-init.sh   ← installer: extract, add to PATH
```

User install:
```
curl -sSf https://thagore.dev/install | sh
```

---

## Notes

- Drago is written in pure Thagore — proving the language works for real projects.
- All I/O, string ops, file ops, process ops are in thagore's stdlib (v0.9+).
- No C code in the drago repo. Zero.
- As thagore stdlib improves, drago automatically benefits.
- The registry is GitHub-based with zero server infrastructure.
- The output log is designed for joy — every interaction should feel good.
