#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
from pathlib import Path


def run(cmd: list[str], env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd,
        check=False,
        text=True,
        capture_output=True,
        env=env,
    )


def write_log(path: Path, title: str, result: subprocess.CompletedProcess[str]) -> None:
    path.write_text(
        "\n".join(
            [
                title,
                f"command: {' '.join(result.args if isinstance(result.args, list) else [str(result.args)])}",
                f"exit: {result.returncode}",
                "--- stdout ---",
                result.stdout,
                "--- stderr ---",
                result.stderr,
            ]
        ),
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--thagc", required=True)
    parser.add_argument("--probe", required=True)
    parser.add_argument("--expected", required=True)
    parser.add_argument("--result", required=True)
    parser.add_argument("--out-dir", required=True)
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    thagc = Path(args.thagc)
    probe = Path(args.probe)
    expected = Path(args.expected)
    result_file = Path(args.result)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    first = out_dir / ("bootstrap-probe.exe" if os.name == "nt" else "bootstrap-probe")
    second = out_dir / ("bootstrap-probe-second.exe" if os.name == "nt" else "bootstrap-probe-second")

    version = run([str(thagc), "--version"])
    write_log(out_dir / "version.log", "thagc --version", version)
    if version.returncode != 0:
        raise SystemExit("installed thagc did not report version successfully")

    check = run([str(thagc), "check", str(probe)])
    write_log(out_dir / "check.log", "thagc check", check)
    if check.returncode != 0:
        raise SystemExit("bootstrap probe check failed")

    build_first = run([str(thagc), "build", str(probe), "-o", str(first)])
    write_log(out_dir / "build-first.log", "first build", build_first)
    if build_first.returncode != 0:
        raise SystemExit("first bootstrap probe build failed")

    if result_file.exists():
        result_file.unlink()
    env = os.environ.copy()
    env["THAGORE_BOOTSTRAP_PROBE"] = "ok"
    first_run = run([str(first)], env=env)
    write_log(out_dir / "run-first.log", "first run", first_run)
    if first_run.returncode != 0:
        raise SystemExit("first bootstrap probe run failed")
    shutil.copyfile(result_file, out_dir / "probe-result-first.txt")

    build_second = run([str(thagc), "build", str(probe), "-o", str(second)])
    write_log(out_dir / "build-second.log", "second build", build_second)
    if build_second.returncode != 0:
        raise SystemExit("second bootstrap probe build failed")

    if result_file.exists():
        result_file.unlink()
    second_run = run([str(second)], env=env)
    write_log(out_dir / "run-second.log", "second run", second_run)
    if second_run.returncode != 0:
        raise SystemExit("second bootstrap probe run failed")
    shutil.copyfile(result_file, out_dir / "probe-result-second.txt")

    expected_text = expected.read_text(encoding="utf-8").rstrip("\r\n")
    first_text = (out_dir / "probe-result-first.txt").read_text(encoding="utf-8").rstrip("\r\n")
    second_text = (out_dir / "probe-result-second.txt").read_text(encoding="utf-8").rstrip("\r\n")
    if expected_text != first_text:
        raise SystemExit("first bootstrap probe output differs from expected")
    if expected_text != second_text:
        raise SystemExit("second bootstrap probe output differs from expected")
    if first_text != second_text:
        raise SystemExit("bootstrap probe output differs between rebuilds")

    (out_dir / "summary.txt").write_text(
        "\n".join(
            [
                f"thagc: {thagc}",
                f"probe: {probe.relative_to(repo_root)}",
                "result: success",
            ]
        ),
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
