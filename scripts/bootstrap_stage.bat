@echo off
setlocal

if /I not "%ALLOW_STAGE0_BOOTSTRAP%"=="1" if /I not "%ALLOW_STAGE0_BOOTSTRAP%"=="true" (
  echo [BLOCKED] Stage0 bootstrap is disabled by default.
  echo [HINT] Set ALLOW_STAGE0_BOOTSTRAP=1 to run this legacy bootstrap script.
  exit /b 1
)

if not exist legacy\stage0.exe (
  echo [ERROR] Missing legacy\stage0.exe
  exit /b 1
)

echo [1/4] Building stage1.exe from legacy\stage0.exe
python scripts\stage_guard.py --timeout 180 -- legacy\stage0.exe build src\thagore.tg -o stage1.exe
if errorlevel 1 (
  echo [ERROR] Stage1 build failed.
  exit /b 1
)

if exist legacy\thag_runtime.lib (
  copy /Y legacy\thag_runtime.lib thag_runtime.lib >nul
)

echo [2/4] Building stage2 via stage1 self-host flow
python scripts\stage_guard.py --timeout 240 -- .\stage1.exe src\thagore.tg --build
if errorlevel 1 (
  echo [ERROR] Stage2 build failed.
  exit /b 1
)
if not exist thagore.exe (
  echo [ERROR] Stage2 binary was not produced.
  exit /b 1
)
copy /Y thagore.exe thagore_stage2.exe >nul

echo [3/4] Comparing stage1 and stage2 hashes
certutil -hashfile stage1.exe SHA256
certutil -hashfile thagore_stage2.exe SHA256

echo [4/4] Verifying interpolation with stage2
python scripts\stage_guard.py --timeout 120 -- .\thagore_stage2.exe test_pure_v.tg --build
if errorlevel 1 (
  echo [ERROR] Stage2 failed to build test_pure_v.tg.
  exit /b 1
)
if not exist test_pure_v.exe (
  echo [ERROR] Missing test_pure_v.exe
  exit /b 1
)
copy /Y test_pure_v.exe pure_v_stage2.exe >nul
python scripts\stage_guard.py --timeout 60 -- .\pure_v_stage2.exe

echo Bootstrap pipeline completed.
exit /b 0
