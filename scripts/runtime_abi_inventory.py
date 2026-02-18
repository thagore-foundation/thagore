import argparse
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_RUNTIME_LIB = ROOT / "thag_runtime.lib"


def find_required_symbols(paths: list[Path]) -> set[str]:
    required: set[str] = set()
    pat = re.compile(r"\bextern\s+func\s+(__thg_[A-Za-z0-9_]+)\s*\(")
    for path in paths:
        text = path.read_text(encoding="utf-8", errors="ignore")
        required.update(pat.findall(text))
    return required


def _run(args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, capture_output=True, text=True)


def find_provided_symbols(runtime_lib: Path) -> set[str]:
    tools = [
        ["llvm-nm", "--defined-only", str(runtime_lib)],
        ["nm", "--defined-only", str(runtime_lib)],
        ["dumpbin", "/symbols", str(runtime_lib)],
    ]
    output = ""
    for cmd in tools:
        proc = _run(cmd)
        if proc.returncode == 0 and proc.stdout.strip():
            output = proc.stdout
            break
    if not output:
        raise SystemExit(
            "FAIL: unable to inspect runtime library symbols (need llvm-nm, nm, or dumpbin in PATH)."
        )
    return set(re.findall(r"(__thg_[A-Za-z0-9_]+)", output))


def collect_tg_files() -> list[Path]:
    files: list[Path] = []
    for base in [ROOT / "src", ROOT / "lib", ROOT / "tests"]:
        if not base.exists():
            continue
        files.extend(base.rglob("*.tg"))
    return files


def main() -> int:
    parser = argparse.ArgumentParser(description="Inventory required/provided runtime ABI symbols (__thg_*)")
    parser.add_argument("--runtime-lib", default=str(DEFAULT_RUNTIME_LIB), help="Path to runtime static library")
    parser.add_argument("--write", default="", help="Optional output report path")
    args = parser.parse_args()

    runtime_lib = Path(args.runtime_lib)
    if not runtime_lib.exists():
        raise SystemExit(f"FAIL: runtime library not found: {runtime_lib}")

    tg_files = collect_tg_files()
    required = find_required_symbols(tg_files)
    provided = find_provided_symbols(runtime_lib)

    missing = sorted(required - provided)
    extra = sorted(provided - required)

    lines: list[str] = []
    lines.append("=== Runtime ABI Inventory ===")
    lines.append(f"runtime_lib: {runtime_lib}")
    lines.append(f"required_count: {len(required)}")
    lines.append(f"provided_count: {len(provided)}")
    lines.append(f"missing_count: {len(missing)}")
    lines.append(f"extra_count: {len(extra)}")
    lines.append("")
    lines.append("[missing]")
    if missing:
        lines.extend(missing)
    else:
        lines.append("(none)")
    lines.append("")
    lines.append("[extra]")
    if extra:
        lines.extend(extra)
    else:
        lines.append("(none)")

    out = "\n".join(lines) + "\n"
    print(out, end="")

    if args.write:
        out_path = Path(args.write)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(out, encoding="utf-8")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
