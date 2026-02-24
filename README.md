<div align="center">

<h1>
  <img src="docs/assets/thagore.svg" alt="Thagore" width="48" height="48" onerror="this.style.display='none'"/>
  Thagore
</h1>

<p><strong>A statically-typed, self-hosted compiled language powered by LLVM</strong></p>

<p>
  <a href="https://github.com/thagore-foundation/thagore/actions/workflows/core-ci.yml"><img src="https://github.com/thagore-foundation/thagore/actions/workflows/core-ci.yml/badge.svg" alt="Core CI"></a>
  <a href="https://github.com/thagore-foundation/thagore/actions/workflows/core-selfhost.yml"><img src="https://github.com/thagore-foundation/thagore/actions/workflows/core-selfhost.yml/badge.svg" alt="Core Selfhost Matrix"></a>
  <a href="https://github.com/thagore-foundation/thagore/actions/workflows/core-release.yml"><img src="https://github.com/thagore-foundation/thagore/actions/workflows/core-release.yml/badge.svg" alt="Core Release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-Apache--2.0-blue.svg" alt="License: Apache-2.0"></a>
  <img src="https://img.shields.io/badge/LLVM-21.x-orange.svg" alt="LLVM 21">
  <img src="https://img.shields.io/badge/stage-self--hosted-brightgreen.svg" alt="Self-Hosted">
</p>

<p>
  <a href="docs/starlight/src/content/docs/index.mdx"><strong>📖 Docs</strong></a> ·
  <a href="examples/"><strong>💡 Examples</strong></a> ·
  <a href=".github/CONTRIBUTING.md"><strong>🤝 Contributing</strong></a> ·
  <a href="https://github.com/thagore/thagore/issues/new"><strong>🐛 Report Bug</strong></a> ·
  <a href="https://discord.gg/zrAsA9SAfF"><strong>✨ Discord Community</strong></a>
</p>

</div>

---

## 📋 Table of Contents

- [✨ Overview](#-overview)
- [🚀 Quick Start](#-quick-start)
  - [Prerequisites](#prerequisites)
  - [Bootstrap](#bootstrap)
  - [Hello World](#hello-world)
- [🏗️ Architecture](#️-architecture)
  - [Compiler Pipeline](#compiler-pipeline)
  - [Key Modules](#key-modules)
- [📂 Project Structure](#-project-structure)
- [🧪 Tests & Benchmarks](#-tests--benchmarks)
- [📦 Standard Library](#-standard-library)
- [🔗 Integrations](#-integrations)
- [📜 Bootstrap & Release Policy](#-bootstrap--release-policy)
- [🤝 Contributing](#-contributing)
- [📄 License](#-license)

---

## ✨ Overview

**Thagore** is a statically-typed, compiled systems language that compiles to native code via LLVM IR. Its defining characteristic is a **Stage1-only bootstrap policy** — the compiler is entirely self-hosted and the build chain never falls back to a C++ stage.

| Feature | Description |
|---|---|
| 🔒 **Static Typing** | Full type inference with explicit type annotations |
| ⚡ **LLVM Backend** | Native code generation via LLVM 21, targeting x86-64, ARM64 |
| 🔄 **Self-Hosted** | Compiler written in Thagore itself (`src/`) |
| 🐍 **Python Bridge** | Seamless interop with Python/PyTorch via FFI |
| 🧩 **Intent System** | High-level intent-driven programming patterns |
| 📐 **Strict Bootstrap** | Stage1 → Stage2 → Stage2b, no C++ fallback |

---

## 🚀 Quick Start

### Prerequisites

| Tool | Version | Purpose |
|------|---------|---------|
| **LLVM / Clang** | 21.x | Compiler backend & linker |
| **Python** | 3.x | Scripting, Python bridge (optional) |
| **Stage1 seed** | `v0.3.168-stage1-seed` | Bootstrap seed binary |
| **Runtime ABI** | — | `thag_runtime.lib` / `libthag_runtime.a` |

> **Windows users:** Download the LLVM 21 release archive and ensure `clang`, `clang++`, and `llvm-link` are on your `PATH`.

### One-Command Installer (`thagup`)

Install with a single bootstrap script (Rustup-style):

```bash
curl -fsSL https://raw.githubusercontent.com/thagore-foundation/thagore/main/scripts/install/thagup-init.sh | bash

# choose profile/targets explicitly
curl -fsSL https://raw.githubusercontent.com/thagore-foundation/thagore/main/scripts/install/thagup-init.sh | bash -s -- \
  --profile custom --targets x86_64-unknown-linux-gnu,aarch64-unknown-linux-gnu
```

Canonical shortcut URL:

```bash
curl -fsSL https://thagore.org/thagup.sh | bash
```

Pin a specific release:

```bash
curl -fsSL https://raw.githubusercontent.com/thagore-foundation/thagore/main/scripts/install/thagup-init.sh | bash -s -- --tag v0.5.30
```

Windows PowerShell (existing setup bootstrap):

```powershell
powershell -ExecutionPolicy Bypass -File scripts/install/install-windows.ps1 -yes
```

Direct Windows bootstrap binary URL:

`https://thagore.org/thagup.exe`

### Bootstrap

Run the full bootstrap cycle (Stage1 → Stage2 → Stage2b):

```bash
# Windows
cmd /c scripts\bootstrap.bat

# Linux / macOS
bash scripts/bootstrap.sh
```

This will:
1. Compile `src/thagore.tg` with the Stage1 seed → `stage2.exe`
2. Self-host Stage2 to produce `stage2b.exe`
3. Validate sample output (`hello_v2`, `fib`)
4. Materialize runtime ABI libraries via `scripts/build_runtime_abi.py`

Runtime ABI artifacts are consumed from seed assets (not built from C++):

| Platform | Artifact |
|----------|----------|
| Windows  | `thagore-runtime-windows.lib` |
| Linux    | `thagore-runtime-linux.a` |
| macOS    | `thagore-runtime-macos.a` |

### Hello World

```thagore
# examples/hello.tg
func main() -> i32:
    print("Hello Self-Hosted World!")

### Target Packs

Manage installed targets from CLI:

```bash
thagore target list
thagore target installed
thagore target add x86_64-unknown-linux-gnu
thagore target ensure x86_64-unknown-linux-gnu
thagore target ensure all
thagore target doctor x86_64-unknown-linux-gnu
thagore target doctor all
thagore target remove x86_64-unknown-linux-gnu
```

`target doctor` validates manifest + embedded LLVM lane (`clang`, `lld`) + runtime candidates for the selected target pack.
Each target pack manifest now declares both `link_driver` and `lld_driver` so linker resolution is target-specific and deterministic.

Target packs are stored under `~/.thagc/targets/<triple>`.
For target builds, the linker lane is resolved from the target pack (`~/.thagc/targets/<triple>/llvm/bin`) and uses embedded `lld` in strict mode (no system-linker fallback).
Toolchain target management (`thagore target ...`, `thagup-init`) is shell-native and does not require Python.

Build with explicit target:

```bash
thagore build examples/hello.tg -o hello --target x86_64-unknown-linux-gnu
```

Release artifacts are packaged as:
- `thagc-core-<host>.tar.gz`
- `thagc-target-<triple>-<host>.tar.gz`
- `SHA256SUMS-thagc-<host>.txt`
    return 0
```

```bash
stage2.exe build examples/hello.tg -o hello.exe
./hello.exe
# Hello Self-Hosted World!
```

More examples in [`examples/`](examples/):

```thagore
# Fibonacci — examples/fib.tg
func fib(n: i32) -> i32:
    if (n < 2):
        return n
    return fib(n - 1) + fib(n - 2)

func main() -> i32:
    let result = fib(35)
    print(result)
    return 0
```

```thagore
# Structs — examples/struct.tg
struct Vector2:
    x: i32
    y: i32

func dot_product(v: Vector2) -> i32:
    return v.x * v.y

let v = Vector2(10, 5)
print(dot_product(v))
```

---

## 🏗️ Architecture

### Compiler Pipeline

```
Source (.tg)
    │
    ▼
┌─────────────────────────────────────────────────────────┐
│  Lexer  →  token stream                                 │
│  Parser →  AST (Pratt parsing)                          │
│  Typechecker → Typed-IR                                 │
│  Lowering → Core-IR                                     │
│  LLVM Emitter → LLVM IR (.ll)                           │
│  Clang/LLVM → Native binary                             │
└─────────────────────────────────────────────────────────┘
    │
    ▼
Native Executable
```

### Key Modules

| Module | Path | Responsibility |
|--------|------|----------------|
| **Lexer** | `src/syntax/native/lexer.tg` | Tokenizes source text into a token stream |
| **Parser** | `src/syntax/native/parser.tg` | Builds AST using Pratt parsing |
| **Typechecker** | `src/semantics/typecheck/program.tg` | Type inference and validation |
| **Lowering** | `src/lowering/transform/program.tg` | Typed-IR → Core-IR transformation |
| **Emitter** | `src/codegen/native/emitter.tg` | Core-IR → LLVM IR emission |
| **CLI Driver** | `src/driver/cli/main.tg` | Pipeline orchestration, imports, linking |

---

## 📂 Project Structure

```
thagore/
├── 📁 src/                   Self-hosted compiler sources
│   ├── syntax/native/        Lexer & Parser
│   ├── semantics/typecheck/  Type system
│   ├── lowering/transform/   IR lowering
│   ├── codegen/native/       LLVM IR emitter
│   └── driver/cli/           CLI entry point
├── 📁 lib/                   Standard library modules (.tg)
│   ├── env.tg                Environment access
│   ├── fs.tg                 File system
│   └── process.tg            Process control
├── 📁 examples/              Runnable Thagore programs
│   ├── hello.tg              Hello World
│   ├── fib.tg                Fibonacci (recursive)
│   ├── struct.tg             Struct & methods
│   ├── python_bridge.tg      Python interop
│   └── ...                   Intent-driven examples
├── 📁 tests/                 Test suite (227 cases)
├── 📁 scripts/               Automation utilities
│   ├── bootstrap.bat / .sh   Full bootstrap cycle
│   ├── benchmark_fib.py      Performance benchmarks
│   ├── intent_suite.py       Intent test runner
│   └── certify_bootstrap_100.py  Bootstrap certification
├── 📁 docs/                  Documentation (Starlight/Astro)
├── 📁 runtime/               Runtime library sources
├── 📁 .github/
│   ├── workflows/            Core CI/Selfhost/Release/Seed/Docs/Policy pipelines
│   ├── ISSUE_TEMPLATE/       Bug report & feature request templates
│   ├── CONTRIBUTING.md       Contribution guide
│   ├── CODE_OF_CONDUCT.md    Community standards
│   └── SECURITY.md           Security policy
└── 📄 LICENSE                Apache-2.0
```

---

## 🧪 Tests & Benchmarks

### Running Tests

```bash
# Build and run the syntax matrix test suite
stage2.exe build tests/test_syntax_matrix.tg -o syntax_matrix.exe
./syntax_matrix.exe

# Run the full intent test suite
python scripts/intent_suite.py --cli stage2.exe

# Run all language pipeline tests
stage2.exe build tests/test_language_pipeline.tg -o lang_pipeline.exe
./lang_pipeline.exe
```

### Benchmarks

```bash
# Fibonacci benchmark: Python vs Thagore native (fib(35))
python scripts/benchmark_fib.py

# Intent benchmark suite (7 runs)
python scripts/benchmark_intent.py --cli stage2.exe --runs 7
```

### Bootstrap Certification

Validate 3 consecutive green runs across all OS:

```bash
# Validate local gates + latest 3 push/main runs
python scripts/certify_bootstrap_100.py --window 3

# Dispatch 3 rounds + certification on current branch
python scripts/run_bootstrap_rounds.py --rounds 3
```

> For CI-side certification, run `python scripts/certify_bootstrap_100.py` against the `Core CI`, `Core Selfhost Matrix`, and `Core Release` workflows.

---

## 📦 Standard Library

The standard library lives in `lib/` as `.tg` modules:

| Module | Description |
|--------|-------------|
| `env` | Environment variable access |
| `fs` | File system operations (read, write, exists) |
| `process` | Process spawning and control |

Import a module in your program:

```thagore
import fs

func main() -> i32:
    let content = fs.read("data.txt")
    print(content)
    return 0
```

---

## 🔗 Integrations

### C FFI

Call native C functions directly:

```thagore
# examples/ffi_math.tg
extern func pow(base: f64, exp: f64) -> f64

func main() -> i32:
    let result = pow(2.0, 10.0)
    print(result)
    return 0
```

### Python Bridge

Interoperate with Python and PyTorch:

```thagore
# examples/python_bridge.tg
import python_bridge

func main() -> i32:
    python_bridge.eval("import math; print(math.pi)")
    return 0
```

---

## 📜 Bootstrap & Release Policy

Thagore enforces a **strict Stage1-only bootstrap policy** across all platforms:

- ❌ **No Stage0 fallback** in CI / Selfhost / Release workflows
- ❌ **No C++ runtime build path** in any workflow or script
- ❌ **No implicit fallback** (`allow_missing_output`, hidden Stage0 branches)
- ✅ **Tracked-file gate** blocks reintroduction of `legacy/`, runtime C++ sources, CMake/vcxproj artifacts
- ✅ **Runtime linking is fail-hard** when the runtime ABI library is missing
- ✅ **Merge gate** requires **3 consecutive green runs**: `Core CI` + `Core Selfhost Matrix` + `Core Release` (dry-run on `main`)
- ✅ **Core workflow suite**: `core-policy-no-stage0`, `core-ci`, `core-selfhost`, `core-release`, `core-seed-stage1`, `core-docs-pages`

### Seed Rotation

Seed tags are promoted only via a successful `Core Seed Stage1` run. After promotion, update `BOOTSTRAP_STAGE1_TAG` in:
- `.github/workflows/core-ci.yml`
- `.github/workflows/core-selfhost.yml`
- `.github/workflows/core-release.yml`
- `.github/workflows/core-seed-stage1.yml`

---

## 🤝 Contributing

We welcome contributions of all kinds! Please read our **[Contributing Guide](.github/CONTRIBUTING.md)** before opening a pull request.

Quick summary:
1. 🍴 Fork the repository and create a branch from `main`
2. 🔨 Make your changes following the [coding standards](.github/CONTRIBUTING.md#-coding-standards)
3. ✅ Ensure all tests pass and add new tests for new features
4. 📬 Open a pull request with a clear description

Please also read our [Code of Conduct](.github/CODE_OF_CONDUCT.md) — we are committed to a welcoming and respectful community.

---

## 📄 License

Thagore is released under the **Apache License 2.0**.  
See [LICENSE](LICENSE) for the full text.

```
Copyright 2025 The Thagore Foundation

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
```

---

<div align="center">
  <sub>Built with ❤️ by the Thagore Foundation and Community · <a href="https://github.com/thagore/thagore/pulls">Pull Requests</a> · <a href="https://github.com/thagore/thagore/issues">Issues</a></sub>
</div>
