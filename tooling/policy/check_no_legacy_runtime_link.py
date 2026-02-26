#!/usr/bin/env python3
import argparse
from pathlib import Path


FORBIDDEN = (
    "thag_runtime.lib",
    "libthag_runtime.a",
    "runtime_library_name(",
)


def scan(root: Path) -> list[str]:
    hits: list[str] = []
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        if path.suffix not in {".cpp", ".hpp", ".h", ".md", ".yml", ".yaml", ".txt", ".py"}:
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except Exception:
            continue
        for token in FORBIDDEN:
            if token in text:
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

