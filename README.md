# Thagore Compiler (`thagc`)

`thagc` is the official compiler for the Thagore language.

It compiles `.tg` source code to native executables through LLVM.

## Highlights

- Static typing and native code generation.
- LLVM direct API backend.
- Cross-platform targets: Linux, macOS, Windows.
- Deterministic and parity-oriented release gates.

## Project Layout

```text
compiler/
  include/thagc/
    domain/
    application/
    infra/
    syntax/
    semantics/
    lowering/
    codegen/
    cli/
    support/
  src/
runtime/
compatibility/
tools/
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
./build/compiler/thagc build examples/hello.tg -o hello --emit-llvm
```

## Tests

```bash
python3 -m compileall tools tests
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
