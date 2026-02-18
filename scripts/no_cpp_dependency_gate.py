import argparse
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_REPORT = ROOT / "no-cpp-dependency-report.txt"

ALLOWLIST_REGEX: list[re.Pattern[str]] = []

BANNED_PATH_REGEX = [
    re.compile(r"^legacy/"),
    re.compile(r"^runtime/src/.+\.(cpp|cc|cxx|c)$"),
    re.compile(r"^runtime/.+CMakeLists\.txt$"),
    re.compile(r"^.+\.(vcxproj|sln|cmake)$"),
]

BANNED_EXTENSION_SCOPE = [
    re.compile(r"^src/.+\.(cpp|cc|cxx|c)$"),
    re.compile(r"^lib/.+\.(cpp|cc|cxx|c)$"),
    re.compile(r"^scripts/.+\.(cpp|cc|cxx|c)$"),
    re.compile(r"^\.github/.+\.(cpp|cc|cxx|c)$"),
]


def _run_git_ls_files() -> list[str]:
    proc = subprocess.run(
        ["git", "ls-files"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode != 0:
        raise SystemExit(f"FAIL: git ls-files failed: {proc.stderr.strip()}")
    return [line.strip().replace("\\", "/") for line in proc.stdout.splitlines() if line.strip()]


def _is_allowlisted(path: str) -> bool:
    return any(pat.search(path) for pat in ALLOWLIST_REGEX)


def evaluate_paths(paths: list[str]) -> list[str]:
    violations: list[str] = []
    for path in paths:
        if _is_allowlisted(path):
            continue
        if any(pat.search(path) for pat in BANNED_PATH_REGEX):
            violations.append(path)
            continue
        if any(pat.search(path) for pat in BANNED_EXTENSION_SCOPE):
            violations.append(path)
            continue
    return sorted(set(violations))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Fail if repository tracked files reintroduce C++/legacy bootstrap dependency."
    )
    parser.add_argument("--report", default=str(DEFAULT_REPORT), help="Output report path")
    args = parser.parse_args()

    tracked = _run_git_ls_files()
    violations = evaluate_paths(tracked)

    report_path = Path(args.report)
    if not report_path.is_absolute():
        report_path = ROOT / report_path
    report_path.parent.mkdir(parents=True, exist_ok=True)

    lines: list[str] = []
    lines.append("=== No C++ Dependency Gate ===")
    lines.append(f"tracked_count={len(tracked)}")
    lines.append(f"violation_count={len(violations)}")
    lines.append(f"status={'pass' if not violations else 'fail'}")
    lines.append("")
    lines.append("[violations]")
    if violations:
        lines.extend(violations)
    else:
        lines.append("(none)")
    lines.append("")
    lines.append("[allowlist_regex]")
    for pat in ALLOWLIST_REGEX:
        lines.append(pat.pattern)

    content = "\n".join(lines) + "\n"
    report_path.write_text(content, encoding="utf-8")
    print(content, end="")

    if violations:
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
