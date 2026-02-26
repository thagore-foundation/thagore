# Contributing to Thagore

Thank you for your interest in contributing to Thagore! Every contribution helps make this project better for everyone.

## 📋 Table of Contents

- [Code of Conduct](#-code-of-conduct)
- [How Can I Contribute?](#-how-can-i-contribute)
- [Development Setup](#-development-setup)
- [Project Architecture](#-project-architecture)
- [Coding Standards](#-coding-standards)
- [Submitting Changes](#-submitting-changes)
- [Issue Guidelines](#-issue-guidelines)
- [Review Process](#-review-process)

## 📜 Code of Conduct

This project follows our [Code of Conduct](CODE_OF_CONDUCT.md). By participating, you are expected to uphold this code. Please report unacceptable behavior to the maintainers.

## 🤔 How Can I Contribute?

### 🐛 Reporting Bugs

Found a bug? Please [open an issue](https://github.com/thagore/thagore/issues/new?template=bug_report.yml) with:
- A clear and descriptive title
- Steps to reproduce the behavior
- The input `.tg` source code (if applicable)
- Expected vs. actual behavior
- Your environment details (OS, compiler version, LLVM version)

### 💡 Suggesting Features

Have an idea? [Open a feature request](https://github.com/thagore/thagore/issues/new?template=feature_request.yml) with:
- A clear description of the problem the feature solves
- Your proposed solution
- Code examples showing the desired syntax or behavior
- Any alternatives you have considered

### 🔧 Submitting Code

We welcome pull requests for:
- Bug fixes
- New language features
- Standard library modules
- Compiler optimizations
- Documentation improvements
- Test coverage improvements

## 🛠️ Development Setup

### Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| C++ compiler | C++23 support | MSVC 17.x, GCC 14+, or Clang 18+ |
| CMake | ≥ 3.28 | Build system generator |
| LLVM | 21.x | Must include development headers |
| Python | 3.x | Optional — only needed for the Python bridge |
| Git | Latest | Version control |

### Building from Source

```bash
# Fork and clone your fork
git clone https://github.com/<your-username>/thagore.git
cd thagore

# Create a feature branch
git checkout -b feat/your-feature-name

# Configure and build (Debug mode recommended for development)
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure
```

### Verifying Your Build

```bash
# Compile and run an example program
./build/Debug/thagore examples/hello.tg
./hello
```

## 🏗️ Project Architecture

Understanding the codebase structure helps you contribute more effectively:

```
src/
├── frontend/          Lexer, Parser, Semantic Analyzer
├── backend/           LLVM IR Generator
├── runtime/           Runtime library & Python bridge
├── driver/            Compilation orchestration
├── common/            Shared diagnostics & types
└── main.cpp           Entry point

include/thagore/       Public header files (mirrors src/ layout)
lib/                   Standard library modules (.tg files)
examples/              Example programs
tests/                 Test suite
```

### Compiler Pipeline

The compiler processes source code through these sequential stages:

1. **Lexer** (`frontend/lexer.cpp`) — Tokenizes source text into a token stream
2. **Parser** (`frontend/parser.cpp`) — Builds the AST using Pratt parsing
3. **Semantic Analyzer** (`frontend/semantic.cpp`) — Performs type checking and validation
4. **IR Generator** (`backend/ir_generator.cpp`) — Emits LLVM IR from the typed AST
5. **Driver** (`driver/driver.cpp`) — Orchestrates the pipeline, handles imports, and links the executable

### Key Data Structures

- **`Token`** — Lexer output with `TokenKind`, text content, and `SourceSpan`
- **`ModuleDecl`** — Root AST node for a translation unit
- **`FunctionDecl`** / **`StructDecl`** — Top-level declarations
- **`Stmt`** / **`Expr`** — Statement and expression AST nodes (discriminated via subclasses)
- **`TypePtr`** — Shared pointer for the type system (`BaseType`, array types, struct types)

## 📐 Coding Standards

### C++ Style

- **Standard**: C++23. Use modern features (`std::format`, `std::expected`, structured bindings, etc.)
- **Naming**:
  - `camelCase` for functions and local variables
  - `PascalCase` for types and classes
  - `UPPER_SNAKE_CASE` for macros and compile-time constants
  - All symbols reside in the `thagore` namespace
- **Headers**: Use `#pragma once` or include guards matching the file path
- **Formatting**: Follow the existing code style in the repository. When in doubt, match nearby code.
- **Error handling**: Use `Result<T, Diagnostic>` (aliased from `std::expected`) for recoverable errors
- **Includes**: Group as: project headers → LLVM headers → standard library headers

### Thagore Standard Library Style (`.tg` files)

- Use 4-space indentation
- Prefer descriptive function and variable names
- Document exported functions with comments
- Follow the existing module patterns in `lib/`

### Commit Messages

We follow [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<scope>): <description>

[optional body]

[optional footer(s)]
```

**Types:**

| Type | Description |
|------|-------------|
| `feat` | A new feature |
| `fix` | A bug fix |
| `docs` | Documentation changes |
| `refactor` | Code change that neither fixes a bug nor adds a feature |
| `test` | Adding or correcting tests |
| `perf` | Performance improvements |
| `chore` | Build process or tooling changes |

**Examples:**

```
feat(parser): add support for match expressions
fix(ir): correct struct field alignment on ARM targets
docs: update README with new examples
test(semantic): add type mismatch edge cases
```

## 📬 Submitting Changes

### Pull Request Process

1. **Fork** the repository and create your branch from `main`
2. **Write code** that follows the coding standards above
3. **Add tests** for any new functionality
4. **Update documentation** if your change affects public behavior
5. **Ensure all tests pass** locally before submitting
6. **Open a pull request** with a clear title and description

### Pull Request Checklist

Before submitting, confirm:

- [ ] Code compiles without warnings under `-Wall -Wextra` (or `/W4` on MSVC)
- [ ] All existing tests pass
- [ ] New tests are added for new features or bug fixes
- [ ] Commit messages follow the conventional commits format
- [ ] Documentation is updated (if applicable)
- [ ] The PR description explains the *what* and *why* of the change

### PR Title Format

Use the same conventional commits format for PR titles:

```
feat(lexer): support hexadecimal integer literals
```

## 🎫 Issue Guidelines

### Before Opening an Issue

1. **Search existing issues** to avoid duplicates
2. **Check the examples** in the `examples/` directory
3. **Try the latest build** — your issue may already be resolved

### Issue Labels

| Label | Meaning |
|-------|---------|
| `bug` | Confirmed bugs |
| `enhancement` | Feature requests |
| `good first issue` | Suitable for newcomers |
| `help wanted` | Extra attention needed |
| `compiler` | Relates to the compiler pipeline |
| `stdlib` | Relates to the standard library |
| `docs` | Documentation improvements |

## 🔍 Review Process

1. A maintainer will review your PR, typically within a few days
2. Feedback may be provided — please address review comments
3. Once approved, your PR will be merged using squash-and-merge
4. Your contribution will be attributed in the commit history

## 🙏 Recognition

All contributors are valued members of the Thagore community. Significant contributions may be recognized in the project's release notes.

---

Thank you for helping make Thagore better! If you have any questions, feel free to open a discussion or reach out to the maintainers.
