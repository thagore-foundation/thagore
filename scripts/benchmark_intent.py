import argparse
import json
import shutil
import statistics
import subprocess
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
FIXTURE = ROOT / "tests" / "intent" / "fixtures" / "intent_demo_golden.tg"


def detect_cli() -> Path:
    candidates = [
        ROOT / "stage2.exe",
        ROOT / "stage2",
        ROOT / "stage1.exe",
        ROOT / "stage1",
        ROOT / "thagore.exe",
        ROOT / "thagore",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise SystemExit("FAIL: unable to find thagore CLI binary (stage2/stage1/thagore). Pass --cli explicitly.")


def run_checked(cmd: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if proc.returncode != 0:
        msg = "\n".join(
            [
                "FAIL: benchmark command failed",
                "CMD: " + " ".join(cmd),
                "--- stdout ---",
                proc.stdout,
                "--- stderr ---",
                proc.stderr,
            ]
        )
        raise SystemExit(msg)
    return proc


def measure(label: str, cmd: list[str], cwd: Path, runs: int) -> dict[str, float]:
    run_checked(cmd, cwd)
    samples: list[float] = []
    for _ in range(runs):
        start = time.perf_counter()
        run_checked(cmd, cwd)
        samples.append(time.perf_counter() - start)
    return {
        "label": label,
        "mean": statistics.mean(samples),
        "median": statistics.median(samples),
        "min": min(samples),
        "max": max(samples),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Benchmark intent build overhead")
    parser.add_argument("--cli", default="", help="Path to thagore CLI binary")
    parser.add_argument("--runs", type=int, default=7, help="Runs per mode")
    parser.add_argument("--json-out", default="", help="Optional path to write JSON results")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    cli = Path(args.cli) if args.cli else detect_cli()
    if not cli.exists():
        raise SystemExit(f"FAIL: cli not found: {cli}")
    if not FIXTURE.exists():
        raise SystemExit(f"FAIL: fixture missing: {FIXTURE}")

    temp_root = Path(tempfile.mkdtemp(prefix="thagore_intent_bench_", dir=str(ROOT)))
    try:
        lock_path = temp_root / "intent_demo.lock"
        run_checked([str(cli), "intent", "lock", str(FIXTURE), "-o", str(lock_path), "--mode=max"], ROOT)

        results = []
        variants = [
            ("intent_off", ["build", str(FIXTURE), "--intent=off", "--emit-llvm", "-o", str(temp_root / "off.ll")]),
            ("intent_min", ["build", str(FIXTURE), "--intent=min", "--emit-llvm", "-o", str(temp_root / "min.ll")]),
            ("intent_max", ["build", str(FIXTURE), "--intent=max", "--emit-llvm", "-o", str(temp_root / "max.ll")]),
            (
                "intent_max_strict_lock",
                [
                    "build",
                    str(FIXTURE),
                    "--intent=max",
                    "--strict-lock",
                    "--intent-lock",
                    str(lock_path),
                    "--emit-llvm",
                    "-o",
                    str(temp_root / "max_strict.ll"),
                ],
            ),
        ]
        for label, argv in variants:
            results.append(measure(label, [str(cli), *argv], ROOT, args.runs))

        baseline = next(v for v in results if v["label"] == "intent_off")["median"]
        for row in results:
            row["overhead_vs_off_pct"] = ((row["median"] / baseline) - 1.0) * 100.0

        lock_a = temp_root / "det_a.lock"
        lock_b = temp_root / "det_b.lock"
        run_checked([str(cli), "intent", "lock", str(FIXTURE), "-o", str(lock_a), "--mode=max"], ROOT)
        run_checked([str(cli), "intent", "lock", str(FIXTURE), "-o", str(lock_b), "--mode=max"], ROOT)
        lock_deterministic = lock_a.read_text(encoding="utf-8") == lock_b.read_text(encoding="utf-8")

        print("=== Intent Build Benchmark ===")
        print("unit: seconds")
        print("")
        print(f"{'variant':<24} {'median':>10} {'mean':>10} {'min':>10} {'max':>10} {'overhead':>10}")
        print("-" * 82)
        for row in results:
            print(
                f"{row['label']:<24} {row['median']:>10.6f} {row['mean']:>10.6f} "
                f"{row['min']:>10.6f} {row['max']:>10.6f} {row['overhead_vs_off_pct']:>9.2f}%"
            )
        print("")
        print(f"lock_deterministic: {str(lock_deterministic).lower()}")

        payload = {
            "runs": args.runs,
            "fixture": str(FIXTURE.relative_to(ROOT)),
            "lock_deterministic": lock_deterministic,
            "results": results,
        }
        if args.json_out:
            out_path = Path(args.json_out)
            if not out_path.is_absolute():
                out_path = ROOT / out_path
            out_path.parent.mkdir(parents=True, exist_ok=True)
            out_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
            print(f"json_report: {out_path}")
        return 0
    finally:
        shutil.rmtree(temp_root, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
