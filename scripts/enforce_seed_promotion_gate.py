import argparse
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_WORKFLOW = ROOT / ".github" / "workflows" / "seed-stage1.yml"

REQUIRED_MARKERS = [
    "name: verify-bundle (${{ matrix.asset_tag }})",
    "scripts/seed_bundle.py verify",
    "scripts/stage1_provenance.py verify",
    "thg_build_compiler \"$STAGE1_BIN\" ../src/thagore.tg \"$STAGE2_STEM\"",
    "thg_build_compiler \"$STAGE2_BIN\" ../src/thagore.tg stage2b_from_bundle",
    "thg_build_compiler \"$STAGE2B_BIN\" ../src/thagore.tg stage2c_from_bundle",
    "[[ -n \"$STAGE2_BIN\" ]] || (echo \"CRITICAL: packaged stage1 produced no standalone stage2 artifact\"; exit 1)",
    "[[ -n \"$STAGE2B_BIN\" ]] || (echo \"CRITICAL: stage2 produced no standalone stage2b artifact\"; exit 1)",
    "[[ -n \"$STAGE2C_BIN\" ]] || (echo \"CRITICAL: stage2b produced no standalone stage2c artifact\"; exit 1)",
    "build_and_assert_output_with_cc \"$STAGE2_BIN\" ../examples/hello.tg hello_from_stage2 \"Hello Self-Hosted World!\"",
    "build_and_assert_output_with_cc \"$STAGE2_BIN\" ../examples/logic.tg logic_from_stage2 \"100\"",
    "build_and_assert_output_with_cc \"$STAGE2B_BIN\" ../examples/hello.tg hello_from_stage2b \"Hello Self-Hosted World!\"",
    "build_and_assert_output_with_cc \"$STAGE2B_BIN\" ../examples/fib.tg fib_from_stage2b \"9227465\"",
    "build_and_assert_output_with_cc \"$STAGE2C_BIN\" ../examples/string_ops.tg string_ops_from_stage2c $'Hello Thagore\\nString equality works!'",
    "build_and_assert_output_with_cc \"$STAGE2C_BIN\" ../examples/loop.tg loop_from_stage2c $'0\\n1\\n2\\n3\\n4\\n100'",
    "build_and_assert_output_with_cc \"$STAGE2C_BIN\" ../examples/concat.tg concat_from_stage2c \"Hello Vietrix\"",
    "build_and_assert_output_with_cc \"$STAGE2C_BIN\" ../examples/function.tg function_from_stage2c $'42\\nHello Thagore'",
    "build_and_assert_output_with_cc \"$STAGE2C_BIN\" ../examples/array.tg array_from_stage2c $'30\\n1089'",
    "build_and_assert_output_with_cc \"$STAGE2C_BIN\" ../examples/method.tg method_from_stage2c $'200\\n1'",
    "needs: [discord-notify-init, build-seed, verify-bundle-before-publish]",
]

FORBIDDEN_VERIFY_FIB_WARNING = (
    "unable to emit fib sample with stage1/stage2 compiler chain; hello smoke already passed."
)

FORBIDDEN_LEGACY_MARKERS = [
    "build_and_assert_output ../examples/hello.tg hello_bundle \"Hello Self-Hosted World!\"",
    "build_and_assert_output ../examples/logic.tg logic_bundle \"100\"",
    "build_and_assert_output ../examples/loop.tg loop_bundle $'0\\n1\\n2\\n3\\n4\\n100'",
    "build_and_assert_output ../examples/string_ops.tg string_ops_bundle $'Hello Thagore\\nString equality works!'",
    "build_and_assert_output ../examples/concat.tg concat_bundle \"Hello Vietrix\"",
    "build_and_assert_output ../examples/function.tg function_bundle $'42\\nHello Thagore'",
    "build_and_assert_output ../examples/fib.tg fib_bundle \"9227465\"",
]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Enforce mandatory atomic seed bundle + expanded smoke promotion gate."
    )
    parser.add_argument(
        "--workflow",
        default=str(DEFAULT_WORKFLOW),
        help="Path to seed-stage1 workflow file.",
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

        # Allow the fib warning in other jobs, but forbid it inside verify-bundle job.
        verify_anchor = "name: verify-bundle (${{ matrix.asset_tag }})"
        verify_pos = text.find(verify_anchor)
        if verify_pos != -1:
            verify_tail = text[verify_pos:]
            if FORBIDDEN_VERIFY_FIB_WARNING in verify_tail:
                errors.append(
                    "forbidden fallback marker in verify-bundle job: "
                    "fib optional warning is not allowed in promotion gate"
                )
            else:
                rows.append("OK|forbidden_marker_absent|verify_bundle_fib_optional_warning")
            for marker in FORBIDDEN_LEGACY_MARKERS:
                if marker in verify_tail:
                    errors.append(
                        "forbidden legacy marker in verify-bundle job: "
                        f"{marker}"
                    )
                else:
                    rows.append(f"OK|forbidden_legacy_marker_absent|{marker}")

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
