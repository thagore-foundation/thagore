import argparse
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent

TARGETS = [
    ROOT / "src",
    ROOT / "tests",
]

DEPRECATED_PATTERNS = [
    re.compile(r"\bemit_program_from_source\s*\("),
    re.compile(r"\bemit_program_typed\s*\("),
    re.compile(r"\bemit_program_from_native\s*\("),
]

ALLOWLIST = {
    "scripts/deprecated_api_gate.py",
}


def _iter_files() -> list[Path]:
    out: list[Path] = []
    for base in TARGETS:
        if not base.exists():
            continue
        for path in base.rglob("*.tg"):
            out.append(path)
    return sorted(out)


def main() -> int:
    parser = argparse.ArgumentParser(description="Fail when deprecated emitter APIs are used.")
    parser.add_argument("--report", default="deprecated-api-gate-report.txt")
    args = parser.parse_args()

    violations: list[str] = []
    for path in _iter_files():
        rel = path.relative_to(ROOT).as_posix()
        if rel in ALLOWLIST:
            continue
        text = path.read_text(encoding="utf-8")
        for idx, line in enumerate(text.splitlines(), 1):
            for pat in DEPRECATED_PATTERNS:
                if pat.search(line):
                    violations.append(f"{rel}:{idx}:{line.strip()}")

    report = ROOT / args.report
    report.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "=== Deprecated API Gate Report ===",
        f"status={'pass' if not violations else 'fail'}",
        f"violation_count={len(violations)}",
        "",
        "[violations]",
    ]
    if violations:
        lines.extend(violations)
    else:
        lines.append("(none)")
    lines.append("")
    content = "\n".join(lines)
    report.write_text(content + "\n", encoding="utf-8")
    print(content)
    return 0 if not violations else 2


if __name__ == "__main__":
    raise SystemExit(main())
