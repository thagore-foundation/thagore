# v1.6 Joy Release Runbook

This runbook describes the v1.6 developer-facing features:

- GUI canvas binding (`lib/gui.tg`)
- WebAssembly build target (`wasm32-unknown-unknown`)
- Intent MVP command surface (`thagc intent`)
- Playground web server (`tooling/playground/server.py`)

## 1) Build and run drawing demo

```bash
thagc run examples/v1_6_drawing_app.tg
```

Expected output: a generated `.ppm` frame path in the current directory.

## 2) Build WebAssembly module

```bash
thagc build app.tg -o app.wasm --target=wasm32-unknown-unknown
```

For wasm target, `thagc run` builds the module and skips native execution:

```bash
thagc run app.tg --target=wasm32-unknown-unknown
```

## 3) Validate intent annotations

```bash
thagc intent doctor app.tg
thagc intent explain app.tg --json
```

`intent doctor` validates intent goal and strategy syntax.

## 4) Start browser playground

```bash
python3 tooling/playground/server.py --host 127.0.0.1 --port 8000 --thagc-bin ./build-llvm21/compiler/thagc
```

Then open:

```
http://127.0.0.1:8000
```

The page posts code to `/api/run`, compiles with `thagc`, executes, and returns output JSON.
