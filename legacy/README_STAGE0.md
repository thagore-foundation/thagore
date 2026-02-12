# Stage0 Bootstrap Seed

- File: `legacy/stage0.exe`
- Purpose: trusted seed compiler for self-host bootstrap.

## Bootstrap flow

1. `legacy\stage0.exe build src\thg.tg -o stage1.exe`
2. `stage1.exe build src\thg.tg -o stage2.exe`
3. Compare hashes of `stage1.exe` and `stage2.exe`.

Use `scripts/bootstrap_stage.bat` to run the full flow automatically.
