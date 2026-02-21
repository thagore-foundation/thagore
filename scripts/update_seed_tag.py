import argparse
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
TARGET_FILES = [
    ".github/workflows/ci.yml",
    ".github/workflows/selfhost-matrix.yml",
    ".github/workflows/release.yml",
    ".github/workflows/bootstrap-seed.yml",
    ".github/workflows/selfhost-longhaul.yml",
    ".github/workflows/selfhost-soak-nightly.yml",
    ".github/workflows/seed-stage1.yml",
]


def _current_seed_tag() -> str:
    ci = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
    m = re.search(r'BOOTSTRAP_STAGE1_TAG:\s*"([^"]+)"', ci)
    if not m:
        m = re.search(r"BOOTSTRAP_STAGE1_TAG:\s*([vV][0-9A-Za-z._-]+)", ci)
    if not m:
        raise RuntimeError("Cannot detect current BOOTSTRAP_STAGE1_TAG from .github/workflows/ci.yml")
    return m.group(1)


def main() -> int:
    parser = argparse.ArgumentParser(description="Update bootstrap seed tag across workflow files.")
    parser.add_argument("--new-tag", required=True)
    parser.add_argument("--report", default="seed-tag-update-report.txt")
    args = parser.parse_args()

    old_tag = _current_seed_tag()
    new_tag = args.new_tag.strip()
    if not new_tag:
        raise RuntimeError("new tag is empty")
    if old_tag == new_tag:
        out = [
            "=== Seed Tag Update Report ===",
            "status=noop",
            f"old_tag={old_tag}",
            f"new_tag={new_tag}",
            "",
        ]
        (ROOT / args.report).write_text("\n".join(out), encoding="utf-8")
        print("\n".join(out))
        return 0

    rows: list[str] = []
    changed = 0
    for rel in TARGET_FILES:
        path = ROOT / rel
        if not path.exists():
            rows.append(f"MISS|{rel}")
            continue
        text = path.read_text(encoding="utf-8")
        n = text.count(old_tag)
        if n <= 0:
            rows.append(f"SKIP|{rel}|count=0")
            continue
        path.write_text(text.replace(old_tag, new_tag), encoding="utf-8")
        rows.append(f"OK|{rel}|count={n}")
        changed += 1

    status = "pass" if changed > 0 else "fail"
    out = [
        "=== Seed Tag Update Report ===",
        f"status={status}",
        f"old_tag={old_tag}",
        f"new_tag={new_tag}",
        f"changed_files={changed}",
        *rows,
        "",
    ]
    (ROOT / args.report).write_text("\n".join(out), encoding="utf-8")
    print("\n".join(out))
    return 0 if status == "pass" else 2


if __name__ == "__main__":
    raise SystemExit(main())
