# Thagore Compiler (`thagc`)

`thagc` is the official compiler for the Thagore language.

It compiles `.tg` source code to native executables through LLVM.

## Highlights

- Static typing and native code generation.
- LLVM direct API backend.
- Cross-platform targets: Linux, macOS, Windows.
- `thagc` links LLVM statically by default (single compiler binary deploy baseline).
- Deterministic and parity-oriented release gates.

## Install

### Linux / macOS

```bash
curl -fsSL https://thagore.org/thagup-init.sh | bash
```

### Windows (PowerShell)

```powershell
$script = Join-Path $env:TEMP "thagup.ps1"
Invoke-WebRequest https://thagore.org/thagup.ps1 -OutFile $script
powershell -ExecutionPolicy Bypass -File $script
```

Installer scripts now install both `thagc` and `drago`, and auto-add `~/.thagore/bin` (or `%USERPROFILE%\.thagore\bin`) to user PATH.

```bash
thagc --version
drago --version
```

First-run:

```bash
drago new myapp
cd myapp
drago run
```

Release archives are self-contained:
- no separate LLVM installation is required on end-user machines.
- runtime archive is embedded into `thagc` (no external `libthag_runtime.a` lookup at build time).

## Updates

```bash
drago update
thagc --version
drago --version
```

Package and toolchain update flows are managed through `drago`.

## Project Layout

```text
compiler/
  frontend/      # lexer/parser/type rules
  middleend/     # typed/core IR + lowering
  backend/       # LLVM emission + object generation
  driver/        # CLI and pipeline orchestration
  shared/        # diagnostics, fs/process abstractions
  include/thagc/ # public/internal module headers
  src/           # module implementations
runtime/
contracts/
tooling/
tests/
docs/
.github/workflows/
```

## Architecture

The compiler uses Clean Architecture boundaries:

- Domain: core language model/rules.
- Application: use-cases and ports.
- Infrastructure: adapters (LLVM, process, filesystem).

Dependency direction is inward only to keep modules cohesive and swappable.

## Build

### Linux

```bash
sudo apt-get update
sudo apt-get install -y clang llvm-dev cmake ninja-build python3
cmake -S . -B build -G Ninja
cmake --build build -j
```

`thagc` now links LLVM statically by default. Disable only when needed:

```bash
cmake -S . -B build -G Ninja -DTHAGC_STATIC_LLVM=OFF
```

### Run

```bash
./build/compiler/thagc --help
./build/compiler/thagc build /tmp/hello.tg -o /tmp/hello_bin --emit-llvm
```

### One-command cross-compile

```bash
# build for target in one command (toolchain config auto-initializes)
thagc build app.tg -o app-aarch64 --target=aarch64-unknown-linux-gnu
```

### FFI and C library linking

```bash
# Example: link libm for extern math symbols
thagc build ffi_math.tg -o ffi_math.bin --link-lib=m

# Extra linker search directory and raw linker arg
thagc build app.tg -o app.bin --link-dir=/opt/mylib/lib --link-lib=mylib --link-arg=-Wl,-rpath,/opt/mylib/lib
```

FFI safety checklist:
- keep `extern func` signatures exact with C ABI types and widths.
- do not pass dangling pointers across FFI boundaries.
- isolate unsafe pointer manipulation in small wrappers.
- validate ownership conventions (who allocates/frees).

Detailed guide: `docs/runbooks/ffi-safety-guidelines.md`.

## Tests

```bash
python3 -m compileall tooling tests
python3 -m unittest discover -s tests -p "test_*.py"
```

## CI

- `CI`: lint + tests + Linux build.
- `Policy`: baseline contract and architecture boundary checks.
- `Selfhost Readiness`: deterministic gate entry.
- `Release`: packaging, GitHub release publish, and installer sync.

## Versioning and Commits

- SemVer.
- Conventional Commits.

## Documentation

- Contribution guide: `.github/CONTRIBUTING.md`
- Security policy: `.github/SECURITY.md`
- ADRs: `docs/adr/`
- Architecture map: `docs/architecture/repo-structure.md`
- Rewrite status: `docs/architecture/rewrite-status.md`
- v1.5 roadmap: `docs/architecture/v1.5-roadmap.md`
- New contributor map: `docs/contributor-guide/where-to-change.md`
- v1.0 release runbook (Thagore + Drago): `docs/runbooks/v1-0-release-thagore-drago.md`
