# PyTorch Interop PoC (v1.3)

Date: March 2, 2026

This PoC demonstrates a Thagore `extern` call into a C++ kernel implemented with PyTorch C++ (`libtorch`).

## Files

- Thagore caller: `tooling/bench/pytorch/pytorch_interop.tg`
- C++ bridge/kernel: `tooling/bench/pytorch/kernel.cpp`
- Stdlib preview wrapper: `stdlib/lib/pytorch.tg`

## Build Steps

Build shared bridge (example with `libtorch` installed):

```bash
clang++ -O2 -std=c++17 -shared -fPIC \
  tooling/bench/pytorch/kernel.cpp \
  -o libthag_pytorch_bridge.so \
  -I"$LIBTORCH/include" \
  -I"$LIBTORCH/include/torch/csrc/api/include" \
  -L"$LIBTORCH/lib" \
  -ltorch -ltorch_cpu -lc10
```

Build Thagore caller and link the bridge:

```bash
thagc build tooling/bench/pytorch/pytorch_interop.tg \
  -o pytorch_interop.bin \
  --opt-level=3 \
  --link-dir=. \
  --link-lib=thag_pytorch_bridge
```

Run:

```bash
./pytorch_interop.bin
```

Expected output: `7.000000`.
