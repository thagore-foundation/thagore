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

echo [2/3] Building stage2.exe from stage1.exe
.\stage1.exe build src\thg.tg -o stage2.exe
if errorlevel 1 (
  echo [ERROR] Stage2 build failed.
  exit /b 1
)
if not exist stage2.exe (
  echo [ERROR] Stage2 binary was not produced.
  exit /b 1
)

echo [3/3] Comparing stage1 and stage2 hashes
certutil -hashfile stage1.exe SHA256
certutil -hashfile stage2.exe SHA256

echo Bootstrap pipeline completed.
exit /b 0
