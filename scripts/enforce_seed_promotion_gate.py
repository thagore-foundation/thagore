import argparse
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_WORKFLOW = ROOT / ".github" / "workflows" / "core-seed-stage1.yml"

REQUIRED_MARKERS = [
    "name: Core Seed Stage1",
    "BOOTSTRAP_STAGE1_TAG",
    "python scripts/enforce_seed_promotion_gate.py",
    "core-seed-stage1-metadata.txt",
    "seed-promotion-gate-core.txt",
    "thagc-core-*",
    "thagc-target-*",
]

FORBIDDEN_MARKERS = [
    "stage0",
    "allow_missing_output",
    "fallback to existing compiler binary",
    "fallback to previous stage2 binary",
]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Enforce mandatory seed promotion policy for core-seed-stage1 workflow."
    )
    parser.add_argument(
        "--workflow",
        default=str(DEFAULT_WORKFLOW),
        help="Path to core-seed-stage1 workflow file.",
    )
    parser.add_argument(
        "--report",
        default="seed-promotion-gate-report.txt",
        help="Report output path.",
    )
    args = parser.parse_args()

    workflow_path = Path(args.workflow)
    report_path = ROOT / args.report
    rows: list[str] = []
    errors: list[str] = []

    if not workflow_path.exists():
        errors.append(f"missing workflow: {workflow_path}")
    else:
        text = workflow_path.read_text(encoding="utf-8")
        for marker in REQUIRED_MARKERS:
            if marker in text:
                rows.append(f"OK|required_marker|{marker}")
            else:
                errors.append(f"missing required marker: {marker}")

        lowered = text.lower()
        for marker in FORBIDDEN_MARKERS:
            if marker in lowered:
                errors.append(f"forbidden marker present: {marker}")
            else:
                rows.append(f"OK|forbidden_marker_absent|{marker}")

    status = "pass" if not errors else "fail"
    out_lines = [
        "=== Seed Promotion Gate Report ===",
        f"status={status}",
        f"workflow={workflow_path}",
        *rows,
    ]
    for err in errors:
        out_lines.append(f"FAIL|{err}")
    out_lines.append("")

    report_path.write_text("\n".join(out_lines), encoding="utf-8")
    print("\n".join(out_lines))
    return 0 if status == "pass" else 2


if __name__ == "__main__":
    raise SystemExit(main())
