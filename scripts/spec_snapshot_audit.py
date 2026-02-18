import argparse
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def _read_features(summary_path: Path) -> list[str]:
    features: list[str] = []
    for raw in summary_path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("feature="):
            features.append(line.split("=", 1)[1])
    return features


def _require_file(path: Path) -> None:
    if not path.exists():
        raise SystemExit(f"CRITICAL: missing required snapshot file: {path}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Aggregate typed/core/spec snapshot artifacts.")
    parser.add_argument("--os-tag", required=True, help="OS tag used in artifact file names")
    parser.add_argument("--out-dir", default=".", help="Output directory for audit files")
    args = parser.parse_args()

    summary = ROOT / "tests" / "snapshots" / "spec-summary.txt"
    _require_file(summary)
    features = _read_features(summary)
    if not features:
        raise SystemExit("CRITICAL: tests/snapshots/spec-summary.txt has no feature rows")

    typedir = ROOT / "tests" / "snapshots" / "typedir"
    coreir = ROOT / "tests" / "snapshots" / "coreir"
    for feature in features:
        _require_file(typedir / f"{feature}.txt")
        _require_file(coreir / f"{feature}.txt")

    out_dir = ROOT / args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    typed_out = out_dir / f"typed-ir-{args.os_tag}.txt"
    core_out = out_dir / f"core-ir-{args.os_tag}.txt"
    spec_out = out_dir / f"spec-summary-{args.os_tag}.txt"

    typed_lines: list[str] = []
    core_lines: list[str] = []
    for feature in features:
        typed_lines.append((typedir / f"{feature}.txt").read_text(encoding="utf-8").strip())
        core_lines.append((coreir / f"{feature}.txt").read_text(encoding="utf-8").strip())

    typed_out.write_text("\n".join(typed_lines) + "\n", encoding="utf-8")
    core_out.write_text("\n".join(core_lines) + "\n", encoding="utf-8")
    spec_out.write_text(summary.read_text(encoding="utf-8"), encoding="utf-8")

    print(f"typed_ir={typed_out}")
    print(f"core_ir={core_out}")
    print(f"spec_summary={spec_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
