import argparse
import hashlib
import os
import shutil
from pathlib import Path


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def _ensure_runtime_libs(root: Path, target_os: str) -> tuple[Path | None, Path | None]:
    lib_win = root / "thag_runtime.lib"
    lib_unix = root / "libthag_runtime.a"

    has_win = lib_win.exists()
    has_unix = lib_unix.exists()
    if (not has_win) and (not has_unix):
        raise SystemExit(
            "CRITICAL: missing runtime ABI library. expected thag_runtime.lib or libthag_runtime.a"
        )

    os_norm = target_os.strip().lower()
    if os_norm == "windows":
        if (not has_win) and has_unix:
            shutil.copyfile(lib_unix, lib_win)
            has_win = True
    else:
        if (not has_unix) and has_win:
            shutil.copyfile(lib_win, lib_unix)
            has_unix = True
        if has_unix and (not has_win):
            shutil.copyfile(lib_unix, lib_win)
            has_win = True

    return (lib_win if has_win else None, lib_unix if has_unix else None)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Ensure runtime ABI static libraries are present for bootstrap/link steps."
    )
    parser.add_argument("--root", default=".", help="Repository root path")
    parser.add_argument("--target-os", default=os.getenv("RUNNER_OS", ""), help="windows|linux|macos")
    parser.add_argument(
        "--summary",
        default="runtime-abi-summary.txt",
        help="Summary report path (relative to root or absolute)",
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()
    lib_win, lib_unix = _ensure_runtime_libs(root, args.target_os)

    summary_path = Path(args.summary)
    if not summary_path.is_absolute():
        summary_path = root / summary_path
    summary_path.parent.mkdir(parents=True, exist_ok=True)

    lines: list[str] = []
    lines.append("runtime_abi_status=ok")
    lines.append(f"target_os={args.target_os}")
    lines.append(f"repo_root={root}")
    if lib_win and lib_win.exists():
        lines.append(f"thag_runtime.lib.path={lib_win}")
        lines.append(f"thag_runtime.lib.size={lib_win.stat().st_size}")
        lines.append(f"thag_runtime.lib.sha256={_sha256(lib_win)}")
    else:
        lines.append("thag_runtime.lib.path=missing")
    if lib_unix and lib_unix.exists():
        lines.append(f"libthag_runtime.a.path={lib_unix}")
        lines.append(f"libthag_runtime.a.size={lib_unix.stat().st_size}")
        lines.append(f"libthag_runtime.a.sha256={_sha256(lib_unix)}")
    else:
        lines.append("libthag_runtime.a.path=missing")
    lines.append("")

    summary_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"runtime_abi_summary={summary_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
