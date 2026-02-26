@echo off
setlocal

set STAGE1_BOOTSTRAP_BIN=%THAG_BOOTSTRAP_STAGE1_BIN%
if "%STAGE1_BOOTSTRAP_BIN%"=="" (
  if exist .tmp_seed_release\bin\stage1.exe (
    set STAGE1_BOOTSTRAP_BIN=.tmp_seed_release\bin\stage1.exe
  ) else (
    set STAGE1_BOOTSTRAP_BIN=stage1.exe
  )
)

if not exist stage1.exe (
  echo [FAIL] Missing stage1.exe bootstrap compiler.
  echo [HINT] Provide Stage1 seed binary first.
  exit /b 1
)
if not exist "%STAGE1_BOOTSTRAP_BIN%" (
  echo [FAIL] Missing selected bootstrap compiler: %STAGE1_BOOTSTRAP_BIN%
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

echo [1/5] Build stage2 from stage1...
echo [INFO] bootstrap_stage1=%STAGE1_BOOTSTRAP_BIN%
if exist stage2.exe del /f /q stage2.exe >nul 2>&1
"%STAGE1_BOOTSTRAP_BIN%" build src/thagore.tg -o stage2.exe
if errorlevel 1 (
  echo [FAIL] Stage2 build failed.
  exit /b 1
)
if not exist stage2.exe (
  echo [FAIL] stage2.exe was not created.
  exit /b 1
)

echo [2/5] Rebuild stage2b from stage2...
if exist stage2b.exe del /f /q stage2b.exe >nul 2>&1
stage2.exe build src/thagore.tg -o stage2b.exe
if errorlevel 1 (
  echo [FAIL] Stage2b build failed.
  exit /b 1
)
if not exist stage2b.exe (
  echo [FAIL] stage2b.exe was not created.
  exit /b 1
)

echo [3/5] Reproducibility check (stage2 == stage2b SHA256)...
for /f "tokens=1" %%A in ('certutil -hashfile stage2.exe SHA256 ^| findstr /R "^[0-9A-Fa-f][0-9A-Fa-f]"') do set STAGE2_SHA=%%A
for /f "tokens=1" %%A in ('certutil -hashfile stage2b.exe SHA256 ^| findstr /R "^[0-9A-Fa-f][0-9A-Fa-f]"') do set STAGE2B_SHA=%%A
if "%STAGE2_SHA%"=="" (
  echo [FAIL] Could not compute SHA256 for stage2.exe
  exit /b 1
)
if "%STAGE2B_SHA%"=="" (
  echo [FAIL] Could not compute SHA256 for stage2b.exe
  exit /b 1
)
echo [INFO] stage2_sha256=%STAGE2_SHA%
echo [INFO] stage2b_sha256=%STAGE2B_SHA%
if /I not "%STAGE2_SHA%"=="%STAGE2B_SHA%" (
  echo [FAIL] Non-reproducible bootstrap: stage2.exe and stage2b.exe differ.
  exit /b 1
)

echo [4/5] Build hello_v2 from stage2b...
if exist hello_v2.exe del /f /q hello_v2.exe >nul 2>&1
stage2b.exe build examples/hello.tg -o hello_v2.exe
if errorlevel 1 (
  echo [FAIL] hello_v2 build failed.
  exit /b 1
)
if not exist hello_v2.exe (
  echo [FAIL] hello_v2.exe was not created.
  exit /b 1
)

echo [5/5] Run hello_v2...
hello_v2.exe
if errorlevel 1 (
  echo [FAIL] hello_v2 execution failed.
  exit /b 1
)

echo [OK] Stage1-only bootstrap cycle completed.
exit /b 0
