import argparse
import os
import re
import statistics
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NAIVE_SRC = ROOT / "examples" / "intent_real_advanced_pack_naive.tg"
INTENT_SRC = ROOT / "examples" / "intent_real_advanced_pack_intent.tg"


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


def build(compiler: Path, src: Path, out_exe: Path, auto_opt: bool) -> None:
    env = os.environ.copy()
    env["THAG_AUTO_OPT"] = "1" if auto_opt else "0"
    run_checked([str(compiler), "build", str(src), "-o", str(out_exe)], env=env)


def parse_checksum(text: str) -> int:
    m = re.search(r"checksum:\s*\n(-?\d+)", text)
    if m is None:
        raise SystemExit(f"FAIL: checksum not found in output: {text!r}")
    return int(m.group(1))


def measure(exe: Path, expected: int, runs: int) -> dict[str, float]:
    run_checked([str(exe)])
    samples: list[float] = []
    for _ in range(runs):
        t0 = time.perf_counter()
        proc = run_checked([str(exe)])
        dt = time.perf_counter() - t0
        got = parse_checksum(proc.stdout)
        if got != expected:
            raise SystemExit(f"FAIL: checksum mismatch for {exe.name}, expected={expected}, got={got}")
        samples.append(dt)
    return {
        "median": statistics.median(samples),
        "mean": statistics.mean(samples),
        "min": min(samples),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Benchmark advanced intent rewrite pack")
    parser.add_argument("--compiler", default="", help="Compiler path")
    parser.add_argument("--runs", type=int, default=3, help="Runs per variant")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    compiler = Path(args.compiler) if args.compiler else detect_compiler()

    naive_exe = ROOT / "intent_real_advanced_pack_naive.exe"
    intent_exe = ROOT / "intent_real_advanced_pack_intent.exe"

    build(compiler, NAIVE_SRC, naive_exe, auto_opt=False)
    build(compiler, INTENT_SRC, intent_exe, auto_opt=True)

    expected = parse_checksum(run_checked([str(naive_exe)]).stdout)

    naive = measure(naive_exe, expected, args.runs)
    intent = measure(intent_exe, expected, args.runs)

    print("=== Advanced Intent Pack Benchmark ===")
    print("unit: seconds")
    print("")
    print(f"{'variant':<12} {'median':>10} {'mean':>10} {'min':>10}")
    print("-" * 46)
    print(f"{'naive':<12} {naive['median']:>10.6f} {naive['mean']:>10.6f} {naive['min']:>10.6f}")
    print(f"{'intent':<12} {intent['median']:>10.6f} {intent['mean']:>10.6f} {intent['min']:>10.6f}")
    print("")
    print(f"speedup intent/naive (median): {naive['median'] / intent['median']:.2f}x")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
