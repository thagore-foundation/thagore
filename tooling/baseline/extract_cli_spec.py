#!/usr/bin/env python3
import argparse
import json
import re
import subprocess
from pathlib import Path


def git_show(branch: str, path: str) -> str:
    return subprocess.check_output(["git", "show", f"{branch}:{path}"], text=True)


def parse_help_lines(source: str) -> list[str]:
    lines: list[str] = []
    for match in re.finditer(r'print\("([^"]+)"\)', source):
        text = match.group(1)
        if "Usage:" in text or text.strip().startswith("thagore") or text.strip().startswith("thagc"):
            lines.append(text)
        if text.strip().startswith("  "):
            lines.append(text)
    unique: list[str] = []
    seen = set()
    for line in lines:
        if line in seen:
            continue
        seen.add(line)
        unique.append(line)
    return unique


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--branch", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    source = git_show(args.branch, "src/driver/cli/main.tg")
    help_lines = parse_help_lines(source)
    spec = {
        "source_branch": args.branch,
        "entry": "src/driver/cli/main.tg",
        "help_lines": help_lines,
        "commands_expected": [
            "build",
            "run",
            "check",
            "fmt",
        ],
    }
    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(spec, indent=2, ensure_ascii=True) + "\n")


if __name__ == "__main__":
    main()
