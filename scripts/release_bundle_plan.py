import argparse
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
REGISTRY = ROOT / "targets" / "registry" / "targets.json"


def _normalize_os(text: str) -> str:
    t = text.strip().lower()
    if t in {"linux", "ubuntu", "ubuntu-latest"}:
        return "linux"
    if t in {"windows", "windows-latest"}:
        return "windows"
    if t in {"macos", "macos-latest", "darwin"}:
        return "macos"
    return t or "unknown"


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate release artifact plan for thagc core and target packs.")
    parser.add_argument("--host-os", default="unknown")
    parser.add_argument("--dry-run", default="true")
    parser.add_argument("--report", default="release-bundle-plan.txt")
    args = parser.parse_args()

    report_path = Path(args.report)
    if not report_path.is_absolute():
        report_path = ROOT / report_path

    host = _normalize_os(args.host_os)
    dry_run = str(args.dry_run).strip().lower() in {"1", "true", "yes", "y"}

    data = json.loads(REGISTRY.read_text(encoding="utf-8"))
    targets = data.get("targets", [])

    rows: list[str] = []
    rows.append("=== Release Bundle Plan ===")
    rows.append(f"host_os={host}")
    rows.append(f"dry_run={str(dry_run).lower()}")
    rows.append(f"core=thagc-core-{host}.tar.gz")
    for row in targets:
        triple = str(row.get("triple", "")).strip()
        if not triple:
            continue
        rows.append(f"target_pack=thagc-target-{triple}-{host}.tar.gz")
    rows.append("")

    report_path.write_text("\n".join(rows), encoding="utf-8")
    print("\n".join(rows))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
