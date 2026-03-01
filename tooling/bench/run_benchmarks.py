#!/usr/bin/env python3
import argparse
import datetime as dt
import json
import math
import shutil
import statistics
import subprocess
import tempfile
import time
from pathlib import Path


def percentile(values: list[float], q: float) -> float:
    if not values:
        return 0.0
    sorted_values = sorted(values)
    idx = max(0, min(len(sorted_values) - 1, math.ceil(q * len(sorted_values)) - 1))
    return sorted_values[idx]


def measure_command(cmd: list[str], iterations: int, cwd: Path | None = None) -> dict[str, object]:
    samples_ms: list[float] = []
    for _ in range(iterations):
        start_ns = time.perf_counter_ns()
        proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, check=False)
        end_ns = time.perf_counter_ns()
        if proc.returncode != 0:
            raise RuntimeError(f"command failed ({proc.returncode}): {' '.join(cmd)}\n{proc.stderr}")
        samples_ms.append((end_ns - start_ns) / 1_000_000.0)
    return {
        "iterations": iterations,
        "samples_ms": samples_ms,
        "min_ms": min(samples_ms),
        "p50_ms": statistics.median(samples_ms),
        "p95_ms": percentile(samples_ms, 0.95),
        "max_ms": max(samples_ms),
    }


def maybe_git_commit_short() -> str:
    proc = subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        return "unknown"
    return proc.stdout.strip() or "unknown"


def write_text(path: Path, data: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(data, encoding="utf-8")


def run_language_comparison(work_dir: Path, thagc_bin: Path, run_iterations: int) -> dict[str, object]:
    compare: dict[str, object] = {}

    thagore_src = work_dir / "compare.tg"
    thagore_bin = work_dir / "compare.bin"
    thagore_src.write_text(
        "func main() -> i32:\n"
        "  let i = 0\n"
        "  let acc = 0\n"
        "  while (i < 2000000):\n"
        "    acc = acc + i\n"
        "    i = i + 1\n"
        "  if (acc == -1):\n"
        "    print(acc)\n"
        "  return 0\n",
        encoding="utf-8",
    )
    subprocess.run([str(thagc_bin), "build", str(thagore_src), "-o", str(thagore_bin)], check=True, capture_output=True, text=True)
    compare["thagore_native"] = {
        "available": True,
        "metrics": measure_command([str(thagore_bin)], run_iterations),
    }

    py_file = work_dir / "compare.py"
    py_file.write_text(
        "acc = 0\n"
        "for i in range(2000000):\n"
        "    acc += i\n"
        "if acc == -1:\n"
        "    print(acc)\n",
        encoding="utf-8",
    )
    compare["python"] = {
        "available": True,
        "metrics": measure_command(["python3", str(py_file)], run_iterations),
    }

    go = shutil.which("go")
    if go is None:
        compare["go"] = {"available": False, "reason": "go not found"}
    else:
        go_src = work_dir / "compare.go"
        go_bin = work_dir / "compare_go"
        go_src.write_text(
            "package main\n"
            "import \"fmt\"\n"
            "func main() {\n"
            "  acc := 0\n"
            "  for i := 0; i < 2000000; i++ {\n"
            "    acc += i\n"
            "  }\n"
            "  if acc == -1 {\n"
            "    fmt.Println(acc)\n"
            "  }\n"
            "}\n",
            encoding="utf-8",
        )
        subprocess.run([go, "build", "-o", str(go_bin), str(go_src)], check=True, capture_output=True, text=True)
        compare["go"] = {"available": True, "metrics": measure_command([str(go_bin)], run_iterations)}

    rustc = shutil.which("rustc")
    if rustc is None:
        compare["rust"] = {"available": False, "reason": "rustc not found"}
    else:
        rs_src = work_dir / "compare.rs"
        rs_bin = work_dir / "compare_rust"
        rs_src.write_text(
            "fn main() {\n"
            "    let mut acc: i64 = 0;\n"
            "    for i in 0..2_000_000_i64 {\n"
            "        acc += i;\n"
            "    }\n"
            "    if acc == -1 {\n"
            "        println!(\"{}\", acc);\n"
            "    }\n"
            "}\n",
            encoding="utf-8",
        )
        subprocess.run([rustc, "-O", str(rs_src), "-o", str(rs_bin)], check=True, capture_output=True, text=True)
        compare["rust"] = {"available": True, "metrics": measure_command([str(rs_bin)], run_iterations)}

    return compare


def build_markdown_report(metrics: dict[str, object]) -> str:
    lines: list[str] = []
    lines.append("# Performance Report")
    lines.append("")
    lines.append(f"- Commit: `{metrics['commit']}`")
    lines.append(f"- Timestamp: `{metrics['timestamp_utc']}`")
    lines.append("")
    lines.append("## Compiler Metrics")
    lines.append("")
    lines.append("| Metric | p50 (ms) | p95 (ms) | max (ms) |")
    lines.append("|---|---:|---:|---:|")
    startup = metrics["thagc_startup_ms"]
    build = metrics["thagc_build_ms"]
    lines.append(f"| thagc --version startup | {startup['p50_ms']:.3f} | {startup['p95_ms']:.3f} | {startup['max_ms']:.3f} |")
    lines.append(f"| thagc build tiny program | {build['p50_ms']:.3f} | {build['p95_ms']:.3f} | {build['max_ms']:.3f} |")
    lines.append("")
    lines.append("## Runtime Comparison")
    lines.append("")
    lines.append("| Runtime | available | p50 (ms) | p95 (ms) |")
    lines.append("|---|---|---:|---:|")
    for name, payload in metrics["comparisons"].items():
        if not payload["available"]:
            reason = payload.get("reason", "unavailable")
            lines.append(f"| {name} | no ({reason}) | - | - |")
            continue
        m = payload["metrics"]
        lines.append(f"| {name} | yes | {m['p50_ms']:.3f} | {m['p95_ms']:.3f} |")
    lines.append("")
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--thagc", required=True, help="Path to thagc binary")
    parser.add_argument("--out-json", required=True, help="Output JSON metrics path")
    parser.add_argument("--out-markdown", required=False, help="Optional markdown report path")
    parser.add_argument("--startup-iterations", type=int, default=20)
    parser.add_argument("--build-iterations", type=int, default=10)
    parser.add_argument("--run-iterations", type=int, default=20)
    args = parser.parse_args()

    thagc_bin = Path(args.thagc)
    if not thagc_bin.exists():
        raise SystemExit(f"thagc not found: {thagc_bin}")

    with tempfile.TemporaryDirectory(prefix="thag-bench-") as td:
        root = Path(td)

        startup_metrics = measure_command([str(thagc_bin), "--version"], args.startup_iterations)

        src = root / "bench.tg"
        out = root / "bench.bin"
        src.write_text(
            "func main() -> i32:\n"
            "  let i = 0\n"
            "  let acc = 0\n"
            "  while (i < 100000):\n"
            "    acc = acc + i\n"
            "    i = i + 1\n"
            "  if (acc == -1):\n"
            "    print(acc)\n"
            "  return 0\n",
            encoding="utf-8",
        )

        build_samples_ms: list[float] = []
        for _ in range(args.build_iterations):
            start_ns = time.perf_counter_ns()
            proc = subprocess.run([str(thagc_bin), "build", str(src), "-o", str(out)], capture_output=True, text=True, check=False)
            end_ns = time.perf_counter_ns()
            if proc.returncode != 0:
                raise RuntimeError(f"thagc build failed ({proc.returncode})\n{proc.stderr}")
            build_samples_ms.append((end_ns - start_ns) / 1_000_000.0)
        build_metrics = {
            "iterations": args.build_iterations,
            "samples_ms": build_samples_ms,
            "min_ms": min(build_samples_ms),
            "p50_ms": statistics.median(build_samples_ms),
            "p95_ms": percentile(build_samples_ms, 0.95),
            "max_ms": max(build_samples_ms),
        }

        comparisons = run_language_comparison(root, thagc_bin, args.run_iterations)

        payload: dict[str, object] = {
            "commit": maybe_git_commit_short(),
            "timestamp_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
            "thagc_startup_ms": startup_metrics,
            "thagc_build_ms": build_metrics,
            "comparisons": comparisons,
        }

        out_json = Path(args.out_json)
        out_json.parent.mkdir(parents=True, exist_ok=True)
        out_json.write_text(json.dumps(payload, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
        print(f"wrote benchmark metrics: {out_json}")

        if args.out_markdown:
            report = build_markdown_report(payload)
            write_text(Path(args.out_markdown), report)
            print(f"wrote benchmark report: {args.out_markdown}")


if __name__ == "__main__":
    main()
