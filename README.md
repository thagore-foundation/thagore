# Thagore Bootstrap Compiler (Core)

Compiler bootstrap cho Thagore viet bang C++23, kien truc tach lop:

- `thag_common`: diagnostics, source span, `std::expected`.
- `thag_frontend`: lexer indentation-aware, AST, Pratt parser, semantic.
- `thag_backend`: LLVM IR lowering, ARC hooks, optimize + emit.
- `thag_runtime`: `runtime.lib` (print + ARC hooks runtime).
- `thag_driver`: CLI `thag` dieu phoi pipeline.

## Yeu cau moi truong

- CMake `>= 3.28`
- Compiler ho tro C++23
- LLVM (da test voi 21.1.8) co `LLVMConfig.cmake`

## Build

```powershell
cmake -S . -B build -DLLVM_DIR=<path-to-llvm/lib/cmake/llvm> -DBUILD_TESTING=OFF
cmake --build build --config Release
```

## Su dung

```powershell
.\build\Release\thag.exe .\examples\hello.tg --emit-ir -o hello.ll
.\build\Release\thag.exe .\examples\hello.tg --emit-obj -o hello.obj
.\build\Release\thag.exe build .\examples\hello.tg --release -o thagore_hero.exe
.\thagore_hero.exe
```

## Builtin runtime (v1)

IR generator su dung cac ham:

- `__thg_print_i32(i32)`
- `__thg_retain(ptr)`
- `__thg_release(ptr)`

Runtime tinh (`thag_runtime.lib`) duoc link vao exe khi dung `thag build`.
