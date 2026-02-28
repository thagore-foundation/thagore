# Thagore Compiler (`thagc`)

`thagc` is the official compiler for the Thagore language.

It compiles `.tg` source code to native executables through LLVM.

## Highlights

- Static typing and native code generation.
- LLVM direct API backend.
- Cross-platform targets: Linux, macOS, Windows.
- Deterministic and parity-oriented release gates.

## Install

### Linux / macOS

```bash
curl -fsSL https://thagore.org/thagup.sh -o /tmp/thagup.sh
bash /tmp/thagup.sh
```

### Windows (PowerShell)

```powershell
$script = Join-Path $env:TEMP "thagup.ps1"
Invoke-WebRequest https://thagore.org/thagup.ps1 -OutFile $script
powershell -ExecutionPolicy Bypass -File $script
```

Installer scripts now auto-add `~/.thagore/bin` (or `%USERPROFILE%\.thagore\bin`) to user PATH.

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

### Run

```bash
./build/compiler/thagc --help
./build/compiler/thagc build /tmp/hello.tg -o /tmp/hello_bin --emit-llvm
```

## Tests

```bash
python3 -m compileall tooling tests
python3 -m unittest discover -s tests -p "test_*.py"
```

## CI

- `CI`: lint + tests + Linux build.
- `Policy`: baseline contract and architecture boundary checks.
- `Selfhost Readiness`: deterministic gate entry.
- `Release`: packaging and GitHub release publish.

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
