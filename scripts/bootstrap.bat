@echo off
setlocal

echo [1/5] Build legacy stage0...
cmake --build legacy\build --config Debug
if errorlevel 1 (
  echo [FAIL] Legacy build failed.
  exit /b 1
)

copy /Y legacy\build\Debug\thag.exe legacy\stage0.exe >nul
if errorlevel 1 (
  echo [FAIL] Cannot copy stage0 executable.
  exit /b 1
)
copy /Y legacy\build\Debug\thag_runtime.lib legacy\thag_runtime.lib >nul
if errorlevel 1 (
  echo [FAIL] Cannot copy runtime library.
  exit /b 1
)
copy /Y legacy\build\Debug\thag_runtime.lib thag_runtime.lib >nul
if errorlevel 1 (
  echo [FAIL] Cannot copy root runtime library.
  exit /b 1
)

echo [2/5] Build stage1 from stage0...
legacy\stage0.exe build src/thagore.tg -o stage1.exe
if errorlevel 1 (
  echo [FAIL] Stage1 build failed.
  exit /b 1
)
if not exist stage1.exe (
  echo [FAIL] stage1.exe was not created.
  exit /b 1
)

echo [3/5] Build stage2 from stage1...
stage1.exe build src/thagore.tg -o stage2.exe
if errorlevel 1 (
  echo [FAIL] Stage2 build failed.
  exit /b 1
)
if not exist stage2.exe (
  echo [FAIL] stage2.exe was not created.
  exit /b 1
)

echo [4/5] Build hello_v2 from stage2...
stage2.exe build examples/hello.tg -o hello_v2.exe
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

echo [OK] Bootstrap cycle completed.
exit /b 0
