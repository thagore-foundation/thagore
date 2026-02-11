# Thagore Bootstrap Compiler (Core)

Compiler bootstrap cho Thagore, viết bằng C++23, theo kiến trúc tách lớp:

- `thag_common`: diagnostics, source span, `std::expected` alias.
- `thag_frontend`: indentation-aware lexer, AST, Pratt parser, semantic analyzer.
- `thag_backend`: LLVM IR lowering, ARC hooks insertion, optimize + emit IR/object.
- `thag_driver`: CLI `thag` điều phối toàn pipeline.

## Yêu cầu môi trường

- CMake `>= 3.28`
- Compiler hỗ trợ C++23 (`std::expected`, `std::format`)
- LLVM 18 development package (`LLVMConfig.cmake`)

## Build

```powershell
cmake -S . -B build -DLLVM_DIR=<path-to-llvm/lib/cmake/llvm>
cmake --build build --config Release
ctest --test-dir build -C Release
```

## Chạy

```powershell
.\build\Release\thag.exe .\examples\hello.tg --emit-ir -o hello.ll
.\build\Release\thag.exe .\examples\hello.tg --emit-obj -o hello.obj
```

## Ngôn ngữ bootstrap hỗ trợ

- Khai báo hàm: `func name(...):`
- Block theo indentation (`INDENT` / `DEDENT` ảo)
- `let`, assignment, `return`, `if`, `loop`
- Biểu thức nhị phân theo Pratt parser (`+ - * / == != < <= > >=`)
- Type inference tối thiểu (`i32`, `bool`, `string`, `void`)

## ARC runtime contract (v1)

IR generator tự chèn lời gọi:

- `declare void @__thg_retain(ptr)`
- `declare void @__thg_release(ptr)`

ARC elision cơ bản bỏ `retain/release` cho temporary values an toàn trong scope.
