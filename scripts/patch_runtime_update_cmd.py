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
RUNTIME_FN_START = "static auto cliDetectRuntimeLib() -> std::string {"
RUNTIME_NEXT_ANCHOR = "static auto cliReadText(const std::string &path) -> std::string {"

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

    const std::string cmdArg = std::string("powershell -NoProfile -ExecutionPolicy Bypass -File \"")
      + scriptPath.string()
      + "\" "
      + mode;
    return std::system(cmdArg.c_str());

#else

    std::fprintf(stderr, "Error: update is only supported on Windows.\n");

    return 2;

#endif

  }

'''

RUNTIME_BLOCK = r'''
static auto cliDetectRuntimeLib() -> std::string {
  const char *envRuntime = std::getenv("THAGORE_RUNTIME_LIB");
  if (envRuntime == nullptr || envRuntime[0] == '\0') {
    envRuntime = std::getenv("THAG_RUNTIME_LIB");
  }
  if (envRuntime != nullptr && envRuntime[0] != '\0') {
    const std::filesystem::path configuredPath {envRuntime};
    std::error_code configuredEc {};
    if (std::filesystem::exists(configuredPath, configuredEc) && !configuredEc) {
      return configuredPath.string();
    }
  }

  std::vector<std::filesystem::path> roots {};
  const auto selfPath = resolveSelfExecutablePath();
  if (!selfPath.empty()) {
    const auto selfDir = selfPath.parent_path();
    if (!selfDir.empty()) {
      roots.push_back(selfDir);
      const auto installRoot = selfDir.parent_path();
      if (!installRoot.empty()) {
        roots.push_back(installRoot);
      }
    }
  }
  std::error_code cwdEc {};
  const auto cwd = std::filesystem::current_path(cwdEc);
  if (!cwdEc && !cwd.empty()) {
    roots.push_back(cwd);
  }

  const std::vector<std::filesystem::path> relCandidates {
    "thag_runtime.lib",
    "libthag_runtime.a",
    std::filesystem::path("lib") / "thag_runtime.lib",
    std::filesystem::path("lib") / "libthag_runtime.a",
    std::filesystem::path("runtime") / "thag_runtime.lib",
    std::filesystem::path("runtime") / "libthag_runtime.a",
    std::filesystem::path("runtime") / "build" / "thag_runtime.lib",
    std::filesystem::path("runtime") / "build" / "Release" / "thag_runtime.lib",
    std::filesystem::path("runtime") / "build" / "libthag_runtime.a",
    std::filesystem::path("build") / "thag_runtime.lib",
    std::filesystem::path("build") / "libthag_runtime.a",
  };

  for (const auto &root : roots) {
    for (const auto &rel : relCandidates) {
      const auto candidate = root.empty() ? rel : (root / rel);
      std::error_code ec {};
      if (std::filesystem::exists(candidate, ec) && !ec) {
        return candidate.string();
      }
    }
  }

  return "";
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

    idx = text.find(EMIT_ANCHOR)
    if idx < 0:
        raise RuntimeError(f"missing anchor: {EMIT_ANCHOR}")
    existing = text.find(UPDATE_MARK)
    if existing >= 0 and existing < idx:
        text = text[:existing] + text[idx:]
        idx = text.find(EMIT_ANCHOR)
    text = text[:idx] + UPDATE_BLOCK + text[idx:]

    runtime_start = text.find(RUNTIME_FN_START)
    runtime_next = text.find(RUNTIME_NEXT_ANCHOR)
    if runtime_start < 0 or runtime_next < 0 or runtime_next <= runtime_start:
        raise RuntimeError(f"missing runtime detect anchors: {RUNTIME_FN_START} / {RUNTIME_NEXT_ANCHOR}")
    text = text[:runtime_start] + RUNTIME_BLOCK + text[runtime_next:]

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
