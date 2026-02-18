@echo off
setlocal

if not exist stage1.exe (
  echo [FAIL] Missing stage1.exe bootstrap compiler.
  echo [HINT] Provide Stage1 seed binary first.
  exit /b 1
)
if not exist thag_runtime.lib (
  if not exist libthag_runtime.a (
    echo [FAIL] Missing runtime ABI library: thag_runtime.lib or libthag_runtime.a
    echo [HINT] Download runtime seed asset: thagore-runtime-windows.lib or thagore-runtime-*.a from BOOTSTRAP_STAGE1_TAG.
    exit /b 1
  )
)
python scripts\build_runtime_abi.py --target-os Windows --summary runtime-abi-summary-local.txt
if errorlevel 1 (
  echo [FAIL] Runtime ABI validation failed.
  exit /b 1
)

echo [1/4] Build stage2 from stage1...
stage1.exe build src/thagore.tg -o stage2.exe
if errorlevel 1 (
  echo [FAIL] Stage2 build failed.
  exit /b 1
)
if not exist stage2.exe (
  echo [FAIL] stage2.exe was not created.
  exit /b 1
)

echo [2/4] Rebuild stage2b from stage2...
stage2.exe build src/thagore.tg -o stage2b.exe
if errorlevel 1 (
  echo [FAIL] Stage2b build failed.
  exit /b 1
)
set STAGE2B_BIN=stage2b.exe
if not exist stage2b.exe (
  if exist stage2.exe (
    echo [INFO] stage2b.exe not produced, using in-place compiler stage2.exe
    set STAGE2B_BIN=stage2.exe
  ) else (
    echo [FAIL] stage2b.exe was not created.
    exit /b 1
  )
)

echo [3/4] Build hello_v2 from stage2b...
%STAGE2B_BIN% build examples/hello.tg -o hello_v2.exe
if errorlevel 1 (
  echo [FAIL] hello_v2 build failed.
  exit /b 1
)
if not exist hello_v2.exe (
  echo [FAIL] hello_v2.exe was not created.
  exit /b 1
)

echo [4/4] Run hello_v2...
hello_v2.exe
if errorlevel 1 (
  echo [FAIL] hello_v2 execution failed.
  exit /b 1
)

echo [OK] Stage1-only bootstrap cycle completed.
exit /b 0
