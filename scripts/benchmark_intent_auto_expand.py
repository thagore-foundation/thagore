import argparse
import os
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

NO_INTENT_SRC = ROOT / "examples" / "intent_real_auto_expand_no_intent.tg"
INTENT_SRC = ROOT / "examples" / "intent_real_auto_expand_intent.tg"


def detect_compiler() -> Path:
    candidates = [
        ROOT / "stage2.exe",
        ROOT / "stage2",
        ROOT / "legacy" / "stage0.exe",
        ROOT / "legacy" / "stage0",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise SystemExit("FAIL: compiler not found. Expected stage2(.exe) or legacy/stage0(.exe)")


def run_checked(cmd: list[str], env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, env=env)
    if proc.returncode != 0:
        lines = [
            "FAIL: command failed",
            "CMD: " + " ".join(cmd),
            "--- stdout ---",
            proc.stdout,
            "--- stderr ---",
            proc.stderr,
        ]
        raise SystemExit("\n".join(lines))
    return proc


def build_variant(compiler: Path, src: Path, out_exe: Path, auto_opt: bool | None) -> None:
    env = os.environ.copy()
    if auto_opt is None:
        env.pop("THAG_AUTO_OPT", None)
    else:
        env["THAG_AUTO_OPT"] = "1" if auto_opt else "0"
    run_checked([str(compiler), "build", str(src), "-o", str(out_exe)], env=env)
    if not out_exe.exists():
        raise SystemExit(f"FAIL: output missing after build: {out_exe}")


def run_program(path: Path) -> tuple[str, int]:
    proc = run_checked([str(path)])
    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    if len(lines) < 2:
        raise SystemExit(f"FAIL: unexpected output from {path.name}: {proc.stdout!r}")
    checksum = int(lines[1])
    return proc.stdout, checksum


def measure(path: Path, expected_checksum: int, runs: int) -> dict[str, float]:
    run_program(path)
    samples: list[float] = []
    for _ in range(runs):
        start = time.perf_counter()
        _, got = run_program(path)
        elapsed = time.perf_counter() - start
        if got != expected_checksum:
            raise SystemExit(
                f"FAIL: checksum mismatch for {path.name}, expected={expected_checksum}, got={got}"
            )
        samples.append(elapsed)
    return {
        "median": statistics.median(samples),
        "mean": statistics.mean(samples),
        "min": min(samples),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Benchmark expanded automatic intent rewrites")
    parser.add_argument("--compiler", default="", help="Compiler path (default: auto detect stage2/stage0)")
    parser.add_argument("--runs", type=int, default=5, help="Measurement runs per variant")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    compiler = Path(args.compiler) if args.compiler else detect_compiler()
    if not compiler.exists():
        raise SystemExit(f"FAIL: compiler not found: {compiler}")

    out_no_intent_on = ROOT / "auto_expand_no_intent_on.exe"
    out_no_intent_off = ROOT / "auto_expand_no_intent_off.exe"
    out_intent = ROOT / "auto_expand_intent.exe"

    build_variant(compiler, NO_INTENT_SRC, out_no_intent_on, True)
    build_variant(compiler, NO_INTENT_SRC, out_no_intent_off, False)
    build_variant(compiler, INTENT_SRC, out_intent, None)

    _, expected = run_program(out_no_intent_off)

    results: dict[str, dict[str, float]] = {}
    results["no_intent_off"] = measure(out_no_intent_off, expected, args.runs)
    results["no_intent_on"] = measure(out_no_intent_on, expected, args.runs)
    results["intent_directive"] = measure(out_intent, expected, args.runs)

    print("=== Intent Auto Expand Benchmark ===")
    print("unit: seconds")
    print("")
    print(f"{'variant':<18} {'median':>10} {'mean':>10} {'min':>10}")
    print("-" * 52)
    for name in ["no_intent_off", "no_intent_on", "intent_directive"]:
        row = results[name]
        print(f"{name:<18} {row['median']:>10.6f} {row['mean']:>10.6f} {row['min']:>10.6f}")

    base = results["no_intent_off"]["median"]
    print("")
    print("speedup vs no_intent_off (median):")
    for name in ["no_intent_on", "intent_directive"]:
        sp = base / results[name]["median"]
        print(f"- {name}: {sp:.2f}x")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

