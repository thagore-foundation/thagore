<div align="center">

# Thagore

**A statically-typed, compiled programming language powered by LLVM.**

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/23)
[![LLVM](https://img.shields.io/badge/LLVM-21-262D3A?logo=llvm&logoColor=white)](https://llvm.org/)
[![CMake](https://img.shields.io/badge/CMake-3.28+-064F8C?logo=cmake&logoColor=white)](https://cmake.org/)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](https://github.com/thagore/thagore/pulls)

*Clean syntax · Native performance · Python interop*

---

[Getting Started](#-getting-started) · [Language Tour](#-language-tour) · [Architecture](#%EF%B8%8F-architecture) · [Contributing](#-contributing) · [License](#-license)

</div>

## ✨ Overview

Thagore is a modern compiled language that combines **clean, expressive syntax** with **native machine code performance**. Built on top of the LLVM compiler infrastructure, Thagore compiles directly to optimized native executables — no interpreter, no VM.

### 🎯 Design Goals

| Goal | Description |
|------|------------|
| **Readable syntax** | Python-like indentation with explicit types for clarity |
| **Zero-cost abstractions** | Structs, methods, and operator overloading compiled to efficient native code |
| **Seamless interop** | Direct C FFI and built-in Python bridge for leveraging existing ecosystems |
| **Safe memory model** | Scope-based string management with reference counting |

## 📦 Features

- 🔢 **Primitive types** — `i32`, `f32`, `f64`, `String`, `ptr`, `bool`
- 🏗️ **Structs & methods** — `struct` declarations with `impl` blocks and `self` receiver
- ⚙️ **Operator overloading** — `__add__`, `__mul__`, and more via magic methods
- 📐 **Static arrays** — Fixed-size arrays with type annotation `[T; N]`
- 📚 **Module system** — `import` for code organization and reuse
- 🔗 **C FFI** — Call any C function with `extern func` declarations
- 🐍 **Python bridge** — Import and call Python modules at runtime
- 📂 **Standard library** — `fs`, `env`, `process`, `list` modules included
- 🔄 **Control flow** — `if`/`else`, `while`, `loop`, `return`
- 🧵 **String operations** — Concatenation, equality, scope-based cleanup

## 🚀 Getting Started

### Prerequisites

| Requirement | Version | Purpose |
|------------|---------|---------|
| C++ compiler | C++23 support (MSVC 17.x, GCC 14+, Clang 18+) | Building the compiler |
| CMake | ≥ 3.28 | Build system |
| LLVM | 21.x | Code generation backend |
| Python | 3.x *(optional)* | Python bridge feature |

### Build from Source

```bash
# Clone the repository
git clone https://github.com/thagore/thagore.git
cd thagore

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Verify the installation
./build/Release/thag --help
```

### Run Your First Program

Create a file `hello.tg`:

```python
print(42)
```

Compile and run:

```bash
thag hello.tg
./hello
```

## 📖 Language Tour

### Variables & Functions

```python
let name = "Thagore"
print(name)

func add(a: i32, b: i32) -> i32:
    return a + b

let result = add(10, 32)
print(result)
```

### Structs & Methods

```python
struct Rect:
    width: i32
    height: i32

impl Rect:
    func area(self) -> i32:
        return self.width * self.height

    func is_square(self) -> i32:
        if (self.width == self.height):
            return 1
        else:
            return 0

let r = Rect(10, 20)
print(r.area())
```

### Operator Overloading

```python
struct Vec2:
    x: i32
    y: i32

impl Vec2:
    func __add__(self, other: Vec2) -> Vec2:
        return Vec2(self.x + other.x, self.y + other.y)

    func __mul__(self, other: Vec2) -> i32:
        return self.x * other.x + self.y * other.y

let v1 = Vec2(1, 2)
let v2 = Vec2(3, 4)
let v3 = v1 + v2       # Vec2(4, 6)
let dot = v1 * v2      # 11
```

### Arrays & Loops

```python
func sum_array(arr: [i32; 4]) -> i32:
    let i = 0
    let total = 0
    while (i < 4):
        total = total + arr[i]
        i = i + 1
    return total

let nums = [10, 20, 30, 40]
print(sum_array(nums))
```

### C FFI

Call any C standard library function directly:

```python
extern func sqrtf(x: f32) -> f32

func calc_hypotenuse(a: f32, b: f32) -> f32:
    return sqrtf(a * a + b * b)

let h = calc_hypotenuse(3.0, 4.0)
print(h)                # 5.0
```

### Python Bridge

Import and use Python packages at runtime:

```python
extern func __thg_py_initialize() -> void
extern func __thg_py_import(name: String) -> ptr
extern func __thg_py_getattr(obj: ptr, name: String) -> ptr
extern func __thg_py_call_2(func_obj: ptr, a1: ptr, a2: ptr) -> ptr
extern func __thg_py_from_i32(val: i32) -> ptr
extern func __thg_py_print_obj(obj: ptr) -> void

func main() -> i32:
    __thg_py_initialize()
    let torch = __thg_py_import("torch")
    let rand_func = __thg_py_getattr(torch, "rand")
    let dim1 = __thg_py_from_i32(3)
    let dim2 = __thg_py_from_i32(3)
    let tensor = __thg_py_call_2(rand_func, dim1, dim2)
    __thg_py_print_obj(tensor)
    return 0
```

### Modules & Standard Library

```python
import fs
import process

func main() -> i32:
    let f = fs.open_write("log.txt")
    f.write("System check initiated.")
    f.close()

    let code = process.run("echo Hello from Shell")
    if (code == 0):
        print("Shell command executed.")
    return 0
```

## 🏗️ Architecture

Thagore follows a classical multi-pass compiler pipeline:

```
┌──────────┐    ┌────────┐    ┌──────────┐    ┌────────────┐    ┌────────┐
│  Source  │───▶│ Lexer  │───▶│  Parser  │───▶│  Semantic  │───▶│ LLVM   │
│  (.tg)   │    │        │    │          │    │  Analyzer  │    │ IR Gen │
└──────────┘    └────────┘    └──────────┘    └────────────┘    └────────┘
                                                                     │
                                                                     ▼
                                                              ┌────────────┐
                                                              │  Native    │
                                                              │ Executable │
                                                              └────────────┘
```

### Project Structure

```
thagore/
├── src/
│   ├── main.cpp                # Entry point
│   ├── frontend/
│   │   ├── lexer.cpp           # Tokenization
│   │   ├── parser.cpp          # AST construction (Pratt parser)
│   │   └── semantic.cpp        # Type checking & validation
│   ├── backend/
│   │   └── ir_generator.cpp    # LLVM IR emission
│   ├── runtime/
│   │   ├── runtime.cpp         # Built-in functions & memory management
│   │   └── py_bridge.cpp       # Python interop via dynamic loading
│   ├── driver/
│   │   └── driver.cpp          # Compilation orchestration & linking
│   └── common/
│       └── diagnostics.cpp     # Error reporting
├── include/thagore/            # Public headers
│   ├── frontend/               # AST, Token, Lexer, Parser, Semantic
│   ├── backend/                # IRGenerator
│   ├── driver/                 # Driver
│   └── common/                 # Diagnostics, Result, SourceSpan
├── lib/                        # Standard library modules
│   ├── fs.tg                   # File system operations
│   ├── env.tg                  # CLI argument access
│   ├── process.tg              # Shell command execution
│   └── list.tg                 # Dynamic list with memory management
├── examples/                   # Example programs
├── tests/                      # Test suite
└── CMakeLists.txt              # Build configuration
```

### Compiler Passes

| Pass | File | Responsibility |
|------|------|---------------|
| **Lexing** | `lexer.cpp` | Converts source text into a stream of tokens with span tracking |
| **Parsing** | `parser.cpp` | Builds the AST using a Pratt (precedence climbing) parser |
| **Semantic Analysis** | `semantic.cpp` | Type inference, struct resolution, scope validation |
| **IR Generation** | `ir_generator.cpp` | Lowers the typed AST to LLVM IR with ABI-correct struct layout |
| **Linking** | `driver.cpp` | Invokes LLVM tools to produce native executables |

## 🧪 Running Tests

```bash
cmake -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Benchmark Fibonacci (Stage2 vs Python)

```bash
python scripts/benchmark_fib.py
```

If `stage2.exe` is blocked in your environment, use:

```bash
python scripts/benchmark_fib.py --compiler legacy\stage0.exe
```

This benchmark compares recursive `fib(35)` across:
- Python (`examples/fib.py`)
- Thagore native binaries compiled from the same emitted LLVM IR at `-O0`, `-O2`, `-O3`

The script prints median/mean/min timings and speedup ratios versus Python.

## 🤝 Contributing

Contributions are welcome and appreciated! Please read our [Contributing Guide](/.github/CONTRIBUTING.md) before submitting a pull request.

You can also check out our:
- [Code of Conduct](/.github/CODE_OF_CONDUCT.md)
- [Security Policy](/.github/SECURITY.md)

## 📄 License

This project is licensed under the **Apache License 2.0** — see the [LICENSE](/LICENSE) file for details.

```
Copyright 2025 The Thagore Authors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0
```

---

<div align="center">

**Built with 🔥 and LLVM**

</div>
