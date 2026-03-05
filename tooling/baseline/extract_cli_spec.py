#!/usr/bin/env python3
import argparse
import json
import re
import subprocess
from pathlib import Path
from typing import Iterable


def resolve_ref(branch: str) -> str:
    candidates = [
        branch,
        f"origin/{branch}",
        f"refs/remotes/origin/{branch}",
        f"refs/heads/{branch}",
        "HEAD",
    ]
    for ref in candidates:
        if subprocess.run(["git", "rev-parse", "--verify", "--quiet", ref], check=False).returncode == 0:
            return ref
    return "HEAD"


def git_show(branch: str, path: str) -> str:
    return subprocess.check_output(["git", "show", f"{branch}:{path}"], text=True)


def git_file_exists(branch: str, path: str) -> bool:
    return (
        subprocess.run(
            ["git", "cat-file", "-e", f"{branch}:{path}"],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ).returncode
        == 0
    )


def first_existing_path(branch: str, candidates: Iterable[str]) -> str:
    for path in candidates:
        if git_file_exists(branch, path):
            return path
    raise RuntimeError("unable to resolve baseline source path for CLI extraction")


def parse_help_lines(source: str) -> list[str]:
    lines: list[str] = []
    for match in re.finditer(r'print\("([^"]+)"\)', source):
        text = match.group(1)
        if "Usage:" in text or text.strip().startswith("thagore") or text.strip().startswith("thagc"):
            lines.append(text)
        if text.strip().startswith("  "):
            lines.append(text)
    for match in re.finditer(r'"([^"\\]*(?:\\.[^"\\]*)*)"', source):
        text = bytes(match.group(1), "utf-8").decode("unicode_escape")
        stripped = text.strip()
        if "Usage:" in text or stripped.startswith("thagore") or stripped.startswith("thagc") or text.startswith("  "):
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

    source_ref = resolve_ref(args.branch)
    entry_path = first_existing_path(source_ref, ("src/driver/cli/main.tg", "compiler/src/driver/help.cpp"))
    source = git_show(source_ref, entry_path)
    help_lines = parse_help_lines(source)
    spec = {
        "source_branch": args.branch,
        "source_ref": source_ref,
        "entry": entry_path,
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
