import statistics
import subprocess
import sys
import time
import argparse
import os
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
EXPECTED = "9227465"
RUNS = 7


def run_checked(cmd: list[str], label: str) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.stderr.write(f"[FAIL] {label}\n")
        if proc.stdout:
            sys.stderr.write(proc.stdout + "\n")
        if proc.stderr:
            sys.stderr.write(proc.stderr + "\n")
        raise SystemExit(1)
    return proc


def measure(cmd: list[str], label: str, expected: str, runs: int = RUNS) -> dict[str, float]:
    run_checked(cmd, f"{label} warmup")
    samples: list[float] = []
    for _ in range(runs):
        start = time.perf_counter()
        proc = run_checked(cmd, label)
        elapsed = time.perf_counter() - start
        out = proc.stdout.strip().splitlines()
        if not out or out[-1].strip() != expected:
            sys.stderr.write(f"[FAIL] {label} output mismatch. expected={expected}, got={proc.stdout!r}\n")
            raise SystemExit(1)
        samples.append(elapsed)
    return {
        "mean": statistics.mean(samples),
        "median": statistics.median(samples),
        "min": min(samples),
    }


def emit_ir(compiler: str, ir_file: str) -> None:
    cmd = ["cmd", "/c", compiler, "examples/fib.tg", "--emit-llvm", "-o", ir_file]
    run_checked(cmd, f"emit IR via {compiler}")


def build_native_from_ir(ir_file: str, opt_level: int, output_exe: str) -> None:
    clang = "llvm\\clang+llvm-21.1.8-x86_64-pc-windows-msvc\\bin\\clang.exe"
    cmd = [
        "cmd",
        "/c",
        clang,
        ir_file,
        "thag_runtime.lib",
        f"-O{opt_level}",
        "-o",
        output_exe,
        "-Wno-override-module",
    ]
    run_checked(cmd, f"build {output_exe} from {ir_file}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Benchmark fib(35): Thagore native vs Python")
    parser.add_argument(
        "--compiler",
        default="stage2.exe",
        help="Compiler executable used to emit LLVM IR (default: stage2.exe)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    variants = [
        ("thagore_O0", 0, "fib_O0.exe"),
        ("thagore_O2", 2, "fib_O2.exe"),
        ("thagore_O3", 3, "fib_O3.exe"),
    ]

    ir_file = "fib_bench.ll"
    emit_ir(args.compiler, ir_file)
    for _, opt, exe in variants:
        build_native_from_ir(ir_file, opt, exe)
        if not (ROOT / exe).exists():
            sys.stderr.write(f"[FAIL] expected output missing: {exe}\n")
            raise SystemExit(1)

    results: dict[str, dict[str, float]] = {}
    results["python"] = measure(["python", "examples/fib.py"], "python", EXPECTED)
    for name, _, exe in variants:
        results[name] = measure(["cmd", "/c", exe], name, EXPECTED)

    print("=== Fibonacci Benchmark (fib(35)) ===")
    print("unit: seconds")
    print("")
    print(f"{'variant':<14} {'median':>10} {'mean':>10} {'min':>10}")
    print("-" * 46)
    order = ["python", "thagore_O0", "thagore_O2", "thagore_O3"]
    for key in order:
        v = results[key]
        print(f"{key:<14} {v['median']:>10.6f} {v['mean']:>10.6f} {v['min']:>10.6f}")

    py_med = results["python"]["median"]
    print("")
    print("speedup vs python (median):")
    for key in ["thagore_O0", "thagore_O2", "thagore_O3"]:
        sp = py_med / results[key]["median"]
        print(f"- {key}: {sp:.2f}x")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
