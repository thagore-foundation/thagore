#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

REQUIRED_CHECKS = [
    "Bootstrap declaration gate linux-x64",
    "Bootstrap declaration gate windows-x64",
    "Bootstrap selfhost stage linux-x64",
    "Bootstrap selfhost stage windows-x64",
    "Selfhost compiler driver linux-x64",
    "Selfhost compiler driver windows-x64",
    "Bootstrap probe linux-x64",
    "Bootstrap probe windows-x64",
    "Linux selfhost probe + bench",
    "Windows selfhost probe",
    "Frontend selfhost ownership audit",
]

REQUIRED_DISPATCH_WORKFLOWS = [
    "Bootstrap Declaration Gate",
    "Bootstrap Selfhost Stage",
    "Selfhost Compiler Driver",
]


def github_json(url: str, token: str) -> dict:
    req = urllib.request.Request(
        url,
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {token}",
            "X-GitHub-Api-Version": "2022-11-28",
            "User-Agent": "thagore-bootstrap-publication-audit",
        },
    )
    with urllib.request.urlopen(req) as resp:
        return json.load(resp)


def normalize(name: str) -> str:
    return name.strip().lower()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True)
    parser.add_argument("--sha", required=True)
    parser.add_argument("--branch", required=True)
    parser.add_argument("--token", default=os.environ.get("GITHUB_TOKEN", ""))
    parser.add_argument("--report-out", default="")
    args = parser.parse_args()

    if not args.token:
        raise SystemExit("missing GitHub token")

    owner, repo = args.repo.split("/", 1)
    check_runs = github_json(
        f"https://api.github.com/repos/{owner}/{repo}/commits/{args.sha}/check-runs",
        args.token,
    ).get("check_runs", [])
    checks_by_name = {normalize(run["name"]): run for run in check_runs}

    problems: list[str] = []
    report_lines = [f"repo={args.repo}", f"branch={args.branch}", f"sha={args.sha}"]

    for check in REQUIRED_CHECKS:
        run = checks_by_name.get(normalize(check))
        if run is None:
            problems.append(f"missing required check: {check}")
            continue
        report_lines.append(f"check:{check}|status={run['status']}|conclusion={run.get('conclusion')}")
        if run["status"] != "completed" or run.get("conclusion") != "success":
            problems.append(f"required check not green: {check} ({run['status']}/{run.get('conclusion')})")

    runs = github_json(
        f"https://api.github.com/repos/{owner}/{repo}/actions/runs?branch={urllib.parse.quote(args.branch)}&per_page=100",
        args.token,
    ).get("workflow_runs", [])
    for workflow in REQUIRED_DISPATCH_WORKFLOWS:
        dispatch_ok = False
        for run in runs:
            if run.get("name") != workflow:
                continue
            if run.get("event") != "workflow_dispatch":
                continue
            if run.get("status") != "completed" or run.get("conclusion") != "success":
                continue
            dispatch_ok = True
            report_lines.append(
                f"dispatch:{workflow}|sha={run.get('head_sha')}|run={run.get('html_url')}|conclusion=success"
            )
            break
        if not dispatch_ok:
            problems.append(f"missing successful workflow_dispatch run for {workflow} on {args.branch}")

    if args.report_out:
        Path(args.report_out).write_text(
            "\n".join(report_lines + (["problems="] + problems if problems else ["problems=none"])) + "\n",
            encoding="utf-8",
        )

    if problems:
        raise SystemExit("bootstrap publication audit failed\n- " + "\n- ".join(problems))

    print("bootstrap publication audit ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
