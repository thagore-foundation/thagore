#!/usr/bin/env python3
import argparse
import datetime as dt
import json
import math
import statistics
import subprocess
import tempfile
import textwrap
import time
from pathlib import Path


def percentile(values: list[float], q: float) -> float:
    if not values:
        return 0.0
    sorted_values = sorted(values)
    idx = max(0, min(len(sorted_values) - 1, math.ceil(q * len(sorted_values)) - 1))
    return sorted_values[idx]


def measure_command(cmd: list[str], iterations: int, cwd: Path | None = None) -> dict[str, float]:
    samples_ms: list[float] = []
    for _ in range(iterations):
        start_ns = time.perf_counter_ns()
        proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, check=False)
        end_ns = time.perf_counter_ns()
        if proc.returncode != 0:
            raise RuntimeError(f"command failed ({proc.returncode}): {' '.join(cmd)}\n{proc.stderr}")
        samples_ms.append((end_ns - start_ns) / 1_000_000.0)
    return {
        "min_ms": min(samples_ms),
        "p50_ms": statistics.median(samples_ms),
        "p95_ms": percentile(samples_ms, 0.95),
        "max_ms": max(samples_ms),
    }


def write_markdown(path: Path, payload: dict[str, object]) -> None:
    thag = payload["thagore_native"]
    flask = payload["python_flask"]
    lines: list[str] = []
    lines.append("# v1.7 Model Serving Benchmark")
    lines.append("")
    lines.append(f"- Timestamp: `{payload['timestamp_utc']}`")
    lines.append(f"- Iterations per command sample: `{payload['iterations']}`")
    lines.append(f"- Workload loop count: `{payload['workload_loops']}`")
    lines.append(f"- Speedup vs Flask (p50): `{payload['speedup_vs_flask_x']:.2f}x`")
    lines.append("")
    lines.append("| Runtime | p50 (ms) | p95 (ms) | max (ms) |")
    lines.append("|---|---:|---:|---:|")
    lines.append(f"| thagore_native | {thag['p50_ms']:.3f} | {thag['p95_ms']:.3f} | {thag['max_ms']:.3f} |")
    lines.append(f"| python_flask | {flask['p50_ms']:.3f} | {flask['p95_ms']:.3f} | {flask['max_ms']:.3f} |")
    lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--thagc", required=True, help="Path to thagc binary")
    parser.add_argument("--out-json", required=True, help="Output metrics JSON")
    parser.add_argument("--out-markdown", required=True, help="Output markdown report")
    parser.add_argument("--iterations", type=int, default=12, help="Number of benchmark command samples")
    parser.add_argument("--workload-loops", type=int, default=4000, help="Inference loop count per run")
    args = parser.parse_args()

    try:
        import flask  # noqa: F401
    except Exception as exc:
        raise SystemExit("Flask is required for v1.7 benchmark. Install with: pip install flask") from exc

    thagc = Path(args.thagc)
    if not thagc.exists():
        raise SystemExit(f"thagc not found: {thagc}")

    with tempfile.TemporaryDirectory(prefix="thag-v17-bench-") as td:
        root = Path(td)
        thag_src = root / "model.tg"
        thag_bin = root / "model.bin"
        flask_src = root / "flask_model.py"
        loops = max(200, int(args.workload_loops))

        thag_src.write_text(
            textwrap.dedent(
                f"""\
                import lib.tensor as tensor

                func infer(seed: i64) -> i64:
                  let x = tensor.new_i64(4)
                  let w = tensor.new_i64(4)
                  tensor.set_i64(x, 0, seed)
                  tensor.set_i64(x, 1, seed + 1)
                  tensor.set_i64(x, 2, seed + 2)
                  tensor.set_i64(x, 3, seed + 3)
                  tensor.set_i64(w, 0, 2)
                  tensor.set_i64(w, 1, 3)
                  tensor.set_i64(w, 2, 5)
                  tensor.set_i64(w, 3, 7)
                  let score = tensor.dot_i64(x, w)
                  tensor.free(x)
                  tensor.free(w)
                  return score

                func main() -> i32:
                  let i = 0
                  let acc: i64 = 0
                  while (i < {loops}):
                    acc = acc + infer(i)
                    i = i + 1
                  if (acc == -1):
                    print(acc)
                  return 0
                """
            ),
            encoding="utf-8",
        )
        subprocess.run([str(thagc), "build", str(thag_src), "-o", str(thag_bin)], capture_output=True, text=True, check=True)

        flask_src.write_text(
            textwrap.dedent(
                f"""\
                from flask import Flask

                app = Flask(__name__)

                @app.get("/infer")
                def infer():
                    total = 0
                    for seed in range({loops}):
                        x = [seed, seed + 1, seed + 2, seed + 3]
                        w = [2, 3, 5, 7]
                        total += x[0] * w[0] + x[1] * w[1] + x[2] * w[2] + x[3] * w[3]
                    return str(total)

                if __name__ == "__main__":
                    with app.test_client() as client:
                        response = client.get("/infer")
                        if response.status_code != 200:
                            raise SystemExit(1)
                """
            ),
            encoding="utf-8",
        )

        thag_metrics = measure_command([str(thag_bin)], args.iterations)
        flask_metrics = measure_command(["python3", str(flask_src)], args.iterations)
        speedup = flask_metrics["p50_ms"] / max(thag_metrics["p50_ms"], 0.000001)

        payload: dict[str, object] = {
            "timestamp_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
            "iterations": args.iterations,
            "workload_loops": loops,
            "thagore_native": thag_metrics,
            "python_flask": flask_metrics,
            "speedup_vs_flask_x": speedup,
        }

        out_json = Path(args.out_json)
        out_json.parent.mkdir(parents=True, exist_ok=True)
        out_json.write_text(json.dumps(payload, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
        write_markdown(Path(args.out_markdown), payload)
        print(f"wrote: {out_json}")
        print(f"speedup_vs_flask_x={speedup:.2f}")


if __name__ == "__main__":
    main()
