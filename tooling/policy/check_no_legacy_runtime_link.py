#!/usr/bin/env python3
import argparse
from pathlib import Path


FORBIDDEN = (
    "thag_runtime.lib",
    "libthag_runtime.a",
    "runtime_library_name(",
)

SCANNED_SUFFIXES = {".cpp", ".hpp", ".h", ".cmake"}


def is_cmake_file(path: Path) -> bool:
    return path.name == "CMakeLists.txt" or path.suffix == ".cmake"


def scan(root: Path) -> list[str]:
    hits: list[str] = []
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        if path.name != "CMakeLists.txt" and path.suffix not in SCANNED_SUFFIXES:
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except Exception:
            continue
        for token in FORBIDDEN:
            if token in text:
                # CMake is allowed to reference runtime archive names for the embed step.
                if is_cmake_file(path):
                    continue
                hits.append(f"{path}: contains '{token}'")
    return hits


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default="compiler")
    args = parser.parse_args()
    root = Path(args.root)
    if not root.exists():
        raise SystemExit(f"missing root: {root}")
    hits = scan(root)
    if hits:
        raise SystemExit("legacy runtime link pattern detected:\n" + "\n".join(hits))
    print("legacy runtime link check: OK")


if __name__ == "__main__":
    main()
