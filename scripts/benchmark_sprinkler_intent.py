import argparse
import re
import statistics
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NATIVE_SRC = ROOT / "examples" / "sprinkler_cover_native.tg"
INTENT_SRC = ROOT / "examples" / "sprinkler_cover_intent.tg"


def detect_compiler() -> Path:
    allow_stage0 = os.environ.get("ALLOW_STAGE0_BOOTSTRAP", "").strip().lower() in {"1", "true", "yes"}
    candidates = [
        ROOT / "stage2.exe",
        ROOT / "stage2",
    ]
    if allow_stage0:
        candidates.extend([ROOT / "legacy" / "stage0.exe", ROOT / "legacy" / "stage0"])
    for c in candidates:
        if c.exists():
            return c
    if allow_stage0:
        raise SystemExit("FAIL: compiler not found (expected stage2(.exe) or legacy/stage0(.exe))")
    raise SystemExit(
        "FAIL: compiler not found (expected stage2(.exe)). "
        "Set ALLOW_STAGE0_BOOTSTRAP=1 to allow legacy/stage0 fallback."
    )


def run_checked(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
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


def build(compiler: Path, src: Path, out_exe: Path) -> None:
    run_checked([str(compiler), "build", str(src), "-o", str(out_exe)])
    if not out_exe.exists():
        raise SystemExit(f"FAIL: missing output {out_exe}")


def parse_checksum(stdout: str) -> int:
    m = re.search(r"checksum:\s*\n(-?\d+)", stdout)
    if m is None:
        raise SystemExit(f"FAIL: checksum not found in output: {stdout!r}")
    return int(m.group(1))


def measure(exe: Path, expected_checksum: int, runs: int) -> dict[str, float]:
    run_checked([str(exe)])
    samples: list[float] = []
    for _ in range(runs):
        t0 = time.perf_counter()
        proc = run_checked([str(exe)])
        dt = time.perf_counter() - t0
        got = parse_checksum(proc.stdout)
        if got != expected_checksum:
            raise SystemExit(
                f"FAIL: checksum mismatch for {exe.name}, expected={expected_checksum}, got={got}"
            )
        samples.append(dt)
    return {
        "median": statistics.median(samples),
        "mean": statistics.mean(samples),
        "min": min(samples),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Benchmark sprinkler native vs intent")
    parser.add_argument("--compiler", default="", help="Path to compiler (default: stage2)")
    parser.add_argument("--runs", type=int, default=7, help="Runs per variant")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    compiler = Path(args.compiler) if args.compiler else detect_compiler()

    native_exe = ROOT / "sprinkler_cover_native.exe"
    intent_exe = ROOT / "sprinkler_cover_intent.exe"

    build(compiler, NATIVE_SRC, native_exe)
    build(compiler, INTENT_SRC, intent_exe)

    expected = parse_checksum(run_checked([str(native_exe)]).stdout)

    native = measure(native_exe, expected, args.runs)
    intent = measure(intent_exe, expected, args.runs)

    print("=== Sprinkler Benchmark (Thagore native vs intent) ===")
    print("unit: seconds")
    print("")
    print(f"{'variant':<12} {'median':>10} {'mean':>10} {'min':>10}")
    print("-" * 46)
    print(f"{'native':<12} {native['median']:>10.6f} {native['mean']:>10.6f} {native['min']:>10.6f}")
    print(f"{'intent':<12} {intent['median']:>10.6f} {intent['mean']:>10.6f} {intent['min']:>10.6f}")
    print("")
    print(f"speedup intent/native (median): {native['median'] / intent['median']:.2f}x")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
