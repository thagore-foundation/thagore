@echo off
setlocal

if not exist stage1.exe (
  echo [FAIL] Missing stage1.exe bootstrap compiler.
  echo [HINT] Provide Stage1 seed binary first.
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
if not exist stage2b.exe (
  echo [FAIL] stage2b.exe was not created.
  exit /b 1
)

echo [3/4] Build hello_v2 from stage2b...
stage2b.exe build examples/hello.tg -o hello_v2.exe
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
