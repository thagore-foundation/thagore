#!/usr/bin/env python3
import argparse
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default="compiler/include/thagc")
    args = parser.parse_args()

    root = Path(args.root)
    bad = []
    for path in root.rglob("*.hpp"):
        rel = path.relative_to(root).as_posix()
        text = path.read_text(encoding="utf-8", errors="ignore")
        if "domain/" in rel and "#include \"thagc/infra/" in text:
            bad.append(rel)
        if "domain/" in rel and "#include \"thagc/application/" in text:
            bad.append(rel)
        if "application/" in rel and "#include \"thagc/infra/" in text:
            bad.append(rel)
    if bad:
        raise SystemExit("circular boundary violation in headers: " + ", ".join(sorted(set(bad))))
    print("header boundary check: OK")


if __name__ == "__main__":
    main()

