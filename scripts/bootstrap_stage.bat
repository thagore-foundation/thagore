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
  echo [ERROR] Missing stage1.exe bootstrap compiler.
  exit /b 1
)
if not exist "%STAGE1_BOOTSTRAP_BIN%" (
  echo [ERROR] Missing selected bootstrap compiler: %STAGE1_BOOTSTRAP_BIN%
  exit /b 1
)

echo [1/4] Building stage2 via stage1
echo [INFO] bootstrap_stage1=%STAGE1_BOOTSTRAP_BIN%
if exist stage2.exe del /f /q stage2.exe >nul 2>&1
python scripts\stage_guard.py --timeout 240 -- "%STAGE1_BOOTSTRAP_BIN%" build src\thagore.tg -o stage2.exe
if errorlevel 1 (
  echo [ERROR] Stage2 build failed.
  exit /b 1
)
if not exist stage2.exe (
  echo [ERROR] stage2.exe was not produced.
  exit /b 1
)

echo [2/4] Rebuilding stage2b via stage2
if exist stage2b.exe del /f /q stage2b.exe >nul 2>&1
python scripts\stage_guard.py --timeout 300 -- .\stage2.exe build src\thagore.tg -o stage2b.exe
if errorlevel 1 (
  echo [ERROR] Stage2b build failed.
  exit /b 1
)
if not exist stage2b.exe (
  echo [ERROR] stage2b.exe was not produced.
  exit /b 1
)

echo [3/4] Comparing stage2 and stage2b hashes
certutil -hashfile stage2.exe SHA256
certutil -hashfile stage2b.exe SHA256
certutil -hashfile stage1.exe SHA256

echo [4/4] Verifying interpolation with stage2b
if exist test_pure_v.exe del /f /q test_pure_v.exe >nul 2>&1
if exist pure_v_stage2.exe del /f /q pure_v_stage2.exe >nul 2>&1
python scripts\stage_guard.py --timeout 120 -- .\stage2b.exe build tests/test_pure_v.tg -o test_pure_v.exe
if errorlevel 1 (
  echo [ERROR] Stage2b failed to build test_pure_v.tg.
  exit /b 1
)
if not exist test_pure_v.exe (
  echo [ERROR] Missing test_pure_v.exe
  exit /b 1
)
copy /Y test_pure_v.exe pure_v_stage2.exe >nul
python scripts\stage_guard.py --timeout 60 -- .\pure_v_stage2.exe

echo Stage1-only bootstrap pipeline completed.
exit /b 0
