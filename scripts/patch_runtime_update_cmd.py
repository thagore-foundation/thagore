#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path


HELP_INSERT = '    std::printf("  thagore update <check|apply|rollback>\\n");\n'
HELP_ANCHORS = (
    '    std::printf("  thagore install toolchain',
    '    std::printf("  thagore intent lock <entry.tg>',
    '    std::printf("  thagore --version\\n");',
)
UPDATE_MARK = 'if (arg1 == "update") {'
EMIT_ANCHOR = '  if (arg1 == "--emit-llvm-internal") {'

UPDATE_BLOCK = r'''
  if (arg1 == "update") {

    std::string mode {"check"};

    if (argc >= 3) {

      mode = cstrOrEmpty(__thg_arg_get(2));

    }

    if (mode != "check" && mode != "apply" && mode != "rollback") {

      std::fprintf(stderr, "Unknown update mode '%s' (expected: check|apply|rollback).\n", mode.c_str());

      return 2;

    }

#if defined(_WIN32)

    const auto selfPath = resolveSelfExecutablePath();

    std::filesystem::path scriptPath {};

    if (!selfPath.empty()) {

      scriptPath = selfPath.parent_path() / "installer" / "update-windows.ps1";

      if (!std::filesystem::exists(scriptPath)) {

        scriptPath = selfPath.parent_path().parent_path() / "installer" / "update-windows.ps1";

      }

    }

    if (scriptPath.empty() || !std::filesystem::exists(scriptPath)) {

      scriptPath = std::filesystem::path("installer") / "update-windows.ps1";

    }

    if (!std::filesystem::exists(scriptPath)) {

      scriptPath = std::filesystem::path("scripts") / "install" / "update-windows.ps1";

    }

    const std::string cmd = std::string("powershell -NoProfile -ExecutionPolicy Bypass -File ")

      + quotePowerShellLiteral(scriptPath.string())

      + " "

      + quotePowerShellLiteral(mode);

    return __process_run(cmd.c_str());

#else

    std::fprintf(stderr, "Error: update is only supported on Windows.\n");

    return 2;

#endif

  }

'''


def patch_runtime(path: Path) -> int:
    text = path.read_text(encoding="utf-8")

    out_lines: list[str] = []
    lines = text.splitlines(keepends=True)
    for idx, line in enumerate(lines):
        out_lines.append(line)
        if any(anchor in line for anchor in HELP_ANCHORS):
            nxt = lines[idx + 1] if (idx + 1) < len(lines) else ""
            if HELP_INSERT not in nxt:
                out_lines.append(HELP_INSERT)
    text = "".join(out_lines)

    if UPDATE_MARK not in text:
        idx = text.find(EMIT_ANCHOR)
        if idx < 0:
            raise RuntimeError(f"missing anchor: {EMIT_ANCHOR}")
        text = text[:idx] + UPDATE_BLOCK + text[idx:]

    path.write_text(text, encoding="utf-8")
    return 0


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_runtime_update_cmd.py <runtime.cc>", file=sys.stderr)
        return 2
    target = Path(sys.argv[1])
    if not target.exists():
        print(f"error: file not found: {target}", file=sys.stderr)
        return 2
    return patch_runtime(target)


if __name__ == "__main__":
    raise SystemExit(main())
