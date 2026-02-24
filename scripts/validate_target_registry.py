import argparse
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
REGISTRY = ROOT / "targets" / "registry" / "targets.json"


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate target registry and standard profile contract.")
    parser.add_argument("--report", default="target-registry-report.txt")
    args = parser.parse_args()

    report_path = Path(args.report)
    if not report_path.is_absolute():
        report_path = ROOT / report_path

    rows: list[str] = []
    errors: list[str] = []

    if not REGISTRY.exists():
        errors.append(f"missing registry file: {REGISTRY}")
    else:
        data = json.loads(REGISTRY.read_text(encoding="utf-8"))
        schema = str(data.get("schema", "")).strip()
        rows.append(f"schema={schema}")
        if schema != "thagc.targets.registry.v1":
            errors.append(f"invalid schema: {schema}")

        targets = data.get("targets", [])
        if not isinstance(targets, list) or not targets:
            errors.append("targets list is missing or empty")
            targets = []

        triples: list[str] = []
        for row in targets:
            triple = str(row.get("triple", "")).strip()
            pack = str(row.get("pack", "")).strip()
            if not triple:
                errors.append("target triple is empty")
                continue
            triples.append(triple)
            if not pack:
                errors.append(f"{triple}: pack path missing")
                continue
            pack_path = ROOT / pack
            if not pack_path.exists():
                errors.append(f"{triple}: pack path missing on disk: {pack}")
            else:
                rows.append(f"target={triple}|pack={pack}")
                manifest = pack_path / "manifest.json"
                if not manifest.exists():
                    errors.append(f"{triple}: missing manifest.json in pack")
                else:
                    try:
                        m = json.loads(manifest.read_text(encoding="utf-8"))
                    except Exception as exc:
                        errors.append(f"{triple}: invalid manifest.json ({exc})")
                        m = {}
                    schema = str(m.get("schema", "")).strip()
                    if schema != "thagc.target.pack.v1":
                        errors.append(f"{triple}: unexpected manifest schema: {schema}")
                    runtime_candidates = m.get("runtime_candidates", [])
                    if not isinstance(runtime_candidates, list) or len(runtime_candidates) == 0:
                        errors.append(f"{triple}: runtime_candidates missing in manifest")
                    rows.append(f"manifest={triple}")

        standard = data.get("standard_profile", [])
        if not isinstance(standard, list):
            errors.append("standard_profile must be a list")
            standard = []

        if len(standard) != 5:
            errors.append(f"standard_profile must contain 5 targets, got {len(standard)}")
        for triple in standard:
            if triple not in triples:
                errors.append(f"standard_profile target not found in registry: {triple}")
            else:
                rows.append(f"standard={triple}")

    status = "pass" if not errors else "fail"
    out = [
        "=== Target Registry Report ===",
        f"status={status}",
        *rows,
    ]
    for err in errors:
        out.append(f"FAIL|{err}")
    out.append("")

    report_path.write_text("\n".join(out), encoding="utf-8")
    print("\n".join(out))
    return 0 if not errors else 2


if __name__ == "__main__":
    raise SystemExit(main())
