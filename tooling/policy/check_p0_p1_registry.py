#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def has_open_critical(issue: dict) -> bool:
    severity = str(issue.get("severity", "")).upper()
    status = str(issue.get("status", "")).lower()
    return severity in {"P0", "P1"} and status not in {"closed", "resolved", "done"}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--registry", required=True, help="Path to JSON registry file")
    args = parser.parse_args()

    path = Path(args.registry)
    if not path.exists():
        raise SystemExit(f"registry not found: {path}")

    data = json.loads(path.read_text())
    issues = data.get("issues", [])
    open_critical = [issue for issue in issues if has_open_critical(issue)]
    if open_critical:
        print("open P0/P1 issues detected:")
        for issue in open_critical:
            print(json.dumps(issue, ensure_ascii=False))
        return 1
    print(f"P0/P1 registry gate passed: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
