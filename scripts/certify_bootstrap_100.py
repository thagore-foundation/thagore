import argparse
import json
import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_REPORT = ROOT / "bootstrap-certify-report.txt"


WORKFLOW_REQUIREMENTS = {
    "Core CI": [
        r"^core-ci-report-ubuntu-latest$",
        r"^core-ci-report-macos-latest$",
        r"^core-ci-report-windows-latest$",
    ],
    "Core Selfhost Matrix": [
        r"^core-selfhost-report-ubuntu-latest$",
        r"^core-selfhost-report-macos-latest$",
        r"^core-selfhost-report-windows-latest$",
    ],
    "Core Release": [
        r"^core-release-assets-ubuntu-latest$",
        r"^core-release-assets-macos-latest$",
        r"^core-release-assets-windows-latest$",
    ],
}


def _run(args: list[str], cwd: Path = ROOT) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=cwd, capture_output=True, text=True, check=False)


def _run_strict(args: list[str], cwd: Path = ROOT) -> str:
    proc = _run(args, cwd=cwd)
    if proc.returncode != 0:
        raise RuntimeError(f"command failed: {' '.join(args)}\n{proc.stderr.strip()}")
    return proc.stdout


def _parse_origin_repo() -> tuple[str, str]:
    origin = _run_strict(["git", "config", "--get", "remote.origin.url"]).strip()
    if not origin:
        raise RuntimeError("missing git remote.origin.url")

    if origin.startswith("git@github.com:"):
        tail = origin.split(":", 1)[1]
    else:
        parsed = urlparse(origin)
        if "github.com" not in (parsed.netloc or ""):
            raise RuntimeError(f"unsupported remote host: {origin}")
        tail = (parsed.path or "").lstrip("/")
    if tail.endswith(".git"):
        tail = tail[:-4]
    parts = tail.split("/")
    if len(parts) != 2:
        raise RuntimeError(f"cannot parse owner/repo from remote: {origin}")
    return parts[0], parts[1]


def _gh_api(owner: str, repo: str, endpoint: str) -> dict:
    out = _run_strict(["gh", "api", f"repos/{owner}/{repo}/{endpoint}"])
    return json.loads(out)


def _check_git_clean(lines: list[str]) -> bool:
    proc = _run(["git", "status", "--short"])
    if proc.returncode != 0:
        lines.append("FAIL|local|git_status_failed")
        return False
    if proc.stdout.strip():
        lines.append("FAIL|local|git_dirty")
        for row in proc.stdout.strip().splitlines():
            lines.append(f"DIRTY|{row}")
        return False
    lines.append("PASS|local|git_clean")
    return True


def _check_local_commands(lines: list[str]) -> bool:
    ok = True
    cmds = [
        ("no_cpp_dependency_gate", [sys.executable, "scripts/no_cpp_dependency_gate.py"]),
        (
            "runtime_abi_bootstrap_critical",
            [
                sys.executable,
                "scripts/runtime_abi_inventory.py",
                "--profile",
                "bootstrap-critical",
                "--fail-on-missing",
            ],
        ),
    ]
    for name, cmd in cmds:
        proc = _run(cmd)
        if proc.returncode != 0:
            ok = False
            lines.append(f"FAIL|local|{name}|rc={proc.returncode}")
            err = (proc.stderr or proc.stdout).strip()
            if err:
                lines.append(f"DETAIL|{name}|{err[:300]}")
        else:
            lines.append(f"PASS|local|{name}")

    if sys.platform.startswith("win"):
        proc = _run(["cmd", "/c", "scripts\\bootstrap.bat"])
        if proc.returncode != 0:
            ok = False
            lines.append(f"FAIL|local|bootstrap_bat|rc={proc.returncode}")
            err = (proc.stderr or proc.stdout).strip()
            if err:
                lines.append(f"DETAIL|bootstrap_bat|{err[:300]}")
        else:
            lines.append("PASS|local|bootstrap_bat")
    else:
        lines.append("WARN|local|bootstrap_bat_skipped_non_windows")
    return ok


def _find_workflow_id(owner: str, repo: str, workflow_name: str) -> int | None:
    data = _gh_api(owner, repo, "actions/workflows?per_page=100")
    for wf in data.get("workflows", []):
        if wf.get("name") == workflow_name:
            return int(wf["id"])
    return None


def _artifact_names(owner: str, repo: str, run_id: int) -> list[str]:
    data = _gh_api(owner, repo, f"actions/runs/{run_id}/artifacts?per_page=100")
    return [str(a.get("name") or "") for a in data.get("artifacts", [])]


def _check_remote(window: int, branch: str, event: str, lines: list[str]) -> bool:
    ok = True
    owner, repo = _parse_origin_repo()
    lines.append(f"INFO|remote|repo={owner}/{repo}")

    for workflow_name, required_patterns in WORKFLOW_REQUIREMENTS.items():
        wf_id = _find_workflow_id(owner, repo, workflow_name)
        if wf_id is None:
            ok = False
            lines.append(f"FAIL|remote|{workflow_name}|workflow_not_found")
            continue

        runs = _gh_api(
            owner,
            repo,
            f"actions/workflows/{wf_id}/runs?per_page=30&branch={branch}&event={event}&status=completed",
        ).get("workflow_runs", [])
        if len(runs) < window:
            ok = False
            lines.append(f"FAIL|remote|{workflow_name}|insufficient_runs={len(runs)}<{window}")
            continue

        target_runs = runs[:window]
        for run in target_runs:
            run_id = int(run["id"])
            conclusion = str(run.get("conclusion") or "")
            lines.append(f"RUN|{workflow_name}|id={run_id}|conclusion={conclusion}")
            if conclusion != "success":
                ok = False
                lines.append(f"FAIL|remote|{workflow_name}|run={run_id}|conclusion={conclusion}")
                continue

            names = _artifact_names(owner, repo, run_id)
            missing: list[str] = []
            for pat in required_patterns:
                cre = re.compile(pat)
                if not any(cre.search(name) for name in names):
                    missing.append(pat)
            if missing:
                ok = False
                lines.append(
                    f"FAIL|remote|{workflow_name}|run={run_id}|missing_artifacts={','.join(missing)}"
                )
            else:
                lines.append(f"PASS|remote|{workflow_name}|run={run_id}|artifacts_ok")

    return ok


def main() -> int:
    parser = argparse.ArgumentParser(description="Certify Thagore bootstrap 100% gates (local + 3-run remote).")
    parser.add_argument("--window", type=int, default=3, help="Consecutive green run window per workflow")
    parser.add_argument("--branch", default="main", help="Branch for remote workflow run checks")
    parser.add_argument("--event", default="push", help="Workflow event for remote checks (push/workflow_dispatch)")
    parser.add_argument("--skip-local", action="store_true", help="Skip local gate checks")
    parser.add_argument("--skip-remote", action="store_true", help="Skip remote GitHub run checks")
    parser.add_argument("--report", default=str(DEFAULT_REPORT), help="Report output path")
    args = parser.parse_args()

    if args.window <= 0:
        raise SystemExit("FAIL: --window must be > 0")

    lines: list[str] = []
    lines.append("=== Bootstrap 100% Certification ===")
    lines.append(f"window={args.window}")

    overall_ok = True
    if not args.skip_local:
        local_ok = _check_git_clean(lines) and _check_local_commands(lines)
        overall_ok = overall_ok and local_ok
    else:
        lines.append("WARN|local|skipped")

    if not args.skip_remote:
        try:
            remote_ok = _check_remote(args.window, args.branch, args.event, lines)
            overall_ok = overall_ok and remote_ok
        except Exception as exc:
            overall_ok = False
            lines.append(f"FAIL|remote|exception={exc}")
    else:
        lines.append("WARN|remote|skipped")

    lines.append(f"status={'pass' if overall_ok else 'fail'}")
    content = "\n".join(lines) + "\n"

    report_path = Path(args.report)
    if not report_path.is_absolute():
        report_path = ROOT / report_path
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(content, encoding="utf-8")
    print(content, end="")
    return 0 if overall_ok else 2


if __name__ == "__main__":
    raise SystemExit(main())
