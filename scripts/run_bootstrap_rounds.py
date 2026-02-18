import argparse
import json
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent

WORKFLOWS = [
    ("CI", []),
    ("Selfhost Matrix", []),
    ("Release", ["-f", "dry_run=true"]),
]


def _run(args: list[str], cwd: Path = ROOT) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=cwd, capture_output=True, text=True, check=False)


def _run_strict(args: list[str], cwd: Path = ROOT) -> str:
    proc = _run(args, cwd)
    if proc.returncode != 0:
        raise RuntimeError(f"command failed: {' '.join(args)}\n{proc.stderr.strip()}")
    return proc.stdout


def _current_branch() -> str:
    return _run_strict(["git", "rev-parse", "--abbrev-ref", "HEAD"]).strip()


def _latest_run_id(workflow: str, branch: str) -> int | None:
    out = _run_strict(
        [
            "gh",
            "run",
            "list",
            "--workflow",
            workflow,
            "--branch",
            branch,
            "--event",
            "workflow_dispatch",
            "--limit",
            "1",
            "--json",
            "databaseId",
        ]
    )
    rows = json.loads(out)
    if not rows:
        return None
    return int(rows[0]["databaseId"])


def _run_status(run_id: int) -> tuple[str, str]:
    out = _run_strict(
        [
            "gh",
            "run",
            "view",
            str(run_id),
            "--json",
            "status,conclusion",
        ]
    )
    row = json.loads(out)
    return str(row.get("status") or ""), str(row.get("conclusion") or "")


def _trigger(workflow: str, branch: str, extra: list[str]) -> int:
    args = ["gh", "workflow", "run", workflow, "--ref", branch] + extra
    _run_strict(args)
    for _ in range(20):
        run_id = _latest_run_id(workflow, branch)
        if run_id is not None:
            return run_id
        time.sleep(3)
    raise RuntimeError(f"cannot locate dispatched run for workflow: {workflow}")


def _wait_run(run_id: int, timeout_sec: int = 7200) -> str:
    started = time.time()
    while True:
        status, conclusion = _run_status(run_id)
        if status == "completed":
            return conclusion
        if (time.time() - started) > timeout_sec:
            return "timed_out"
        time.sleep(15)


def main() -> int:
    parser = argparse.ArgumentParser(description="Dispatch CI/Selfhost/Release dry-run rounds and certify results.")
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--branch", default="")
    parser.add_argument("--skip-local-cert", action="store_true")
    args = parser.parse_args()

    if args.rounds <= 0:
        raise SystemExit("FAIL: --rounds must be > 0")

    branch = args.branch.strip() or _current_branch()
    report = ROOT / "bootstrap-rounds-report.txt"
    lines: list[str] = [f"branch={branch}", f"rounds={args.rounds}"]

    for i in range(1, args.rounds + 1):
        lines.append(f"ROUND|{i}|start")
        run_ids: dict[str, int] = {}
        for wf, extra in WORKFLOWS:
            run_id = _trigger(wf, branch, extra)
            run_ids[wf] = run_id
            lines.append(f"ROUND|{i}|dispatch|{wf}|run_id={run_id}")

        round_ok = True
        for wf, _ in WORKFLOWS:
            run_id = run_ids[wf]
            conclusion = _wait_run(run_id)
            lines.append(f"ROUND|{i}|result|{wf}|run_id={run_id}|conclusion={conclusion}")
            if conclusion != "success":
                round_ok = False

        if not round_ok:
            lines.append(f"ROUND|{i}|status=fail")
            report.write_text("\n".join(lines) + "\n", encoding="utf-8")
            print(report.read_text(encoding="utf-8"), end="")
            return 2
        lines.append(f"ROUND|{i}|status=pass")

    cert_cmd = [
        "python",
        "scripts/certify_bootstrap_100.py",
        "--window",
        str(args.rounds),
        "--branch",
        branch,
        "--event",
        "workflow_dispatch",
        "--report",
        "bootstrap-certify-rounds.txt",
    ]
    if args.skip_local_cert:
        cert_cmd.append("--skip-local")
    proc = _run(cert_cmd)
    lines.append(f"CERTIFY|rc={proc.returncode}")
    if proc.stdout.strip():
        lines.append("CERTIFY_OUT_BEGIN")
        lines.extend(proc.stdout.strip().splitlines())
        lines.append("CERTIFY_OUT_END")
    if proc.stderr.strip():
        lines.append("CERTIFY_ERR_BEGIN")
        lines.extend(proc.stderr.strip().splitlines())
        lines.append("CERTIFY_ERR_END")

    report.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(report.read_text(encoding="utf-8"), end="")
    return 0 if proc.returncode == 0 else 2


if __name__ == "__main__":
    raise SystemExit(main())
