import argparse
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "examples" / "intent_adaptive_style_auto_plan.tg"


def detect_compiler() -> Path:
    allow_stage0 = os.environ.get("ALLOW_STAGE0_BOOTSTRAP", "").strip().lower() in {"1", "true", "yes"}
    candidates = [
        ROOT / "stage2.exe",
        ROOT / "stage2",
        ROOT / "legacy" / "stage0.exe",
        ROOT / "legacy" / "build" / "Release" / "thag.exe",
        ROOT / "legacy" / "stage0",
    ]
    if not allow_stage0:
        candidates = [ROOT / "stage2.exe", ROOT / "stage2"]
    for c in candidates:
        if c.exists():
            return c
    if allow_stage0:
        raise SystemExit("FAIL: compiler not found (expected stage2(.exe) or legacy/stage0(.exe))")
    raise SystemExit(
        "FAIL: compiler not found (expected stage2(.exe)). "
        "Set ALLOW_STAGE0_BOOTSTRAP=1 to allow legacy/stage0 fallback."
    )


def run_checked(cmd: list[str], env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, env=env)
    if proc.returncode != 0:
        raise SystemExit(
            "\n".join(
                [
                    "FAIL: command failed",
                    "CMD: " + " ".join(cmd),
                    "--- stdout ---",
                    proc.stdout,
                    "--- stderr ---",
                    proc.stderr,
                ]
            )
        )
    return proc


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Adaptive matcher smoke test")
    parser.add_argument("--compiler", default="", help="Compiler path")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    compiler = Path(args.compiler) if args.compiler else detect_compiler()

    exe = ROOT / "intent_adaptive_style_auto_plan.exe"
    env = os.environ.copy()
    env["THAG_INTENT_EXPLAIN"] = "1"
    env["THAG_AUTO_OPT"] = "1"
    build = run_checked([str(compiler), "build", str(SRC), "-o", str(exe)], env=env)

    required = [
        "goal=lower_bound_sorted",
        "goal=count_not_equal_sorted",
        "goal=count_outside_range_sorted",
    ]
    for token in required:
        if token not in build.stdout:
            raise SystemExit(f"FAIL: expected adaptive match token not found: {token}")

    run = run_checked([str(exe)])
    if "adaptive checksum:\n53" not in run.stdout:
        raise SystemExit(f"FAIL: unexpected runtime output: {run.stdout!r}")

    print("PASS: adaptive intent matcher smoke")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
