#!/usr/bin/env python3
import argparse
import json
import subprocess
from dataclasses import dataclass, asdict


@dataclass
class PurgeResult:
    deleted_releases: list[str]
    deleted_tags: list[str]
    skipped: list[str]
    errors: list[str]


def run(cmd: list[str]) -> str:
    return subprocess.check_output(cmd, text=True).strip()


def list_release_tags(limit: int) -> list[str]:
    out = run(["gh", "release", "list", "--limit", str(limit), "--json", "tagName"])
    data = json.loads(out)
    return [item["tagName"] for item in data]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--limit", type=int, default=200)
    parser.add_argument("--keep-tag", default="")
    parser.add_argument("--report", default="purge-report.json")
    args = parser.parse_args()

    tags = list_release_tags(args.limit)
    keep = args.keep_tag.strip()
    result = PurgeResult(deleted_releases=[], deleted_tags=[], skipped=[], errors=[])

    for tag in tags:
        if keep and tag == keep:
            result.skipped.append(tag)
            continue
        if args.dry_run:
            result.skipped.append(f"dry-run:{tag}")
            continue
        try:
            subprocess.check_call(["gh", "release", "delete", tag, "--yes"])
            result.deleted_releases.append(tag)
        except subprocess.CalledProcessError as ex:
            result.errors.append(f"release delete failed for {tag}: {ex}")
            continue
        try:
            subprocess.check_call(["git", "push", "origin", f":refs/tags/{tag}"])
            result.deleted_tags.append(tag)
        except subprocess.CalledProcessError as ex:
            result.errors.append(f"tag delete failed for {tag}: {ex}")

    with open(args.report, "w", encoding="utf-8") as f:
        json.dump(asdict(result), f, indent=2, ensure_ascii=True)
        f.write("\n")
    print(json.dumps(asdict(result), indent=2, ensure_ascii=True))


if __name__ == "__main__":
    main()

