import argparse
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
WORKFLOWS = [
    ".github/workflows/ci.yml",
    ".github/workflows/selfhost-matrix.yml",
    ".github/workflows/release.yml",
    ".github/workflows/bootstrap-seed.yml",
]


def _extract_tags(text: str) -> list[str]:
    tags = re.findall(r'BOOTSTRAP_STAGE1_TAG:\s*"([^"]+)"', text)
    tags += re.findall(r"BOOTSTRAP_STAGE1_TAG:\s*([vV][0-9A-Za-z._-]+)", text)
    return tags


def main() -> int:
    parser = argparse.ArgumentParser(description="Ensure BOOTSTRAP_STAGE1_TAG is synchronized across workflows.")
    parser.add_argument("--report", default="seed-tag-sync-report.txt")
    args = parser.parse_args()

    report_path = ROOT / args.report
    rows: list[str] = []
    tags_by_file: dict[str, list[str]] = {}
    all_tags: set[str] = set()
    for rel in WORKFLOWS:
        path = ROOT / rel
        if not path.exists():
            rows.append(f"FAIL|missing_file|{rel}")
            continue
        tags = _extract_tags(path.read_text(encoding="utf-8"))
        tags_by_file[rel] = tags
        if not tags:
            rows.append(f"FAIL|missing_tag|{rel}")
        else:
            for tag in tags:
                all_tags.add(tag)
            rows.append(f"OK|{rel}|{','.join(tags)}")

    status = "pass"
    if len(all_tags) != 1:
        status = "fail"
        rows.append(f"FAIL|tag_mismatch|values={','.join(sorted(all_tags))}")
    if any(r.startswith("FAIL|") for r in rows):
        status = "fail"

    out = [
        "=== Seed Tag Sync Report ===",
        f"status={status}",
        *rows,
        "",
    ]
    report_path.write_text("\n".join(out), encoding="utf-8")
    print("\n".join(out))
    return 0 if status == "pass" else 2


if __name__ == "__main__":
    raise SystemExit(main())
