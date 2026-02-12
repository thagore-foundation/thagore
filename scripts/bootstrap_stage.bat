@echo off
setlocal

if not exist legacy\stage0.exe (
  echo [ERROR] Missing legacy\stage0.exe
  exit /b 1
)

echo [1/3] Building stage1.exe from legacy\stage0.exe
legacy\stage0.exe build src\thg.tg -o stage1.exe
if errorlevel 1 (
  echo [ERROR] Stage1 build failed.
  exit /b 1
)

if exist legacy\thag_runtime.lib (
  copy /Y legacy\thag_runtime.lib thag_runtime.lib >nul
)

echo [2/4] Building stage2 via stage1 self-host flow
.\stage1.exe src\thg.tg --build
if errorlevel 1 (
  echo [ERROR] Stage2 build failed.
  exit /b 1
)
if not exist thg.exe (
  echo [ERROR] Stage2 binary was not produced.
  exit /b 1
)
copy /Y thg.exe thg_stage2.exe >nul

echo [3/4] Comparing stage1 and stage2 hashes
certutil -hashfile stage1.exe SHA256
certutil -hashfile thg_stage2.exe SHA256

echo [4/4] Verifying interpolation with stage2
.\thg_stage2.exe test_pure_v.tg --build
if errorlevel 1 (
  echo [ERROR] Stage2 failed to build test_pure_v.tg.
  exit /b 1
)
if not exist test_pure_v.exe (
  echo [ERROR] Missing test_pure_v.exe
  exit /b 1
)
copy /Y test_pure_v.exe pure_v_stage2.exe >nul
.\pure_v_stage2.exe

echo Bootstrap pipeline completed.
exit /b 0
