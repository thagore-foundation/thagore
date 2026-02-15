#!/usr/bin/env python3
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Any, Dict, List, Optional


def getenv_required(name: str) -> str:
    value = os.getenv(name, "").strip()
    if not value:
        raise RuntimeError(f"Missing required env: {name}")
    return value


def gh_api(
    path: str, token: str, query: Optional[Dict[str, str]] = None
) -> Dict[str, Any]:
    base_url = os.getenv("GITHUB_API_URL", "https://api.github.com").rstrip("/")
    url = f"{base_url}{path}"
    if query:
        url += "?" + urllib.parse.urlencode(query)
    req = urllib.request.Request(
        url,
        headers={
            "Authorization": f"Bearer {token}",
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
            "User-Agent": "thagore-discord-ci-status",
        },
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        return json.loads(resp.read().decode("utf-8"))


def webhook_request(
    method: str,
    webhook_url: str,
    payload: Dict[str, Any],
    message_id: Optional[str] = None,
) -> Dict[str, Any]:
    url = webhook_url.rstrip("/")
    if message_id:
        url = f"{url}/messages/{message_id}"
    url = f"{url}?wait=true"

    body = json.dumps(payload).encode("utf-8")
    retries = 3
    for attempt in range(retries):
        req = urllib.request.Request(
            url,
            data=body,
            method=method,
            headers={
                "Content-Type": "application/json",
                "User-Agent": "thagore-discord-ci-status",
            },
        )
        try:
            with urllib.request.urlopen(req, timeout=30) as resp:
                raw = resp.read().decode("utf-8")
                return json.loads(raw) if raw else {}
        except urllib.error.HTTPError as err:
            if err.code == 429 and attempt < retries - 1:
                retry_after = err.headers.get("Retry-After")
                try:
                    wait_s = float(retry_after) if retry_after else 1.5
                except ValueError:
                    wait_s = 1.5
                time.sleep(max(1.0, wait_s))
                continue
            raise
    return {}


def to_short_sha(sha: str) -> str:
    return sha[:7] if sha else "unknown"


def to_one_line(text: str) -> str:
    if not text:
        return ""
    return text.splitlines()[0].strip()


def map_job_state(status: str, conclusion: str) -> str:
    if status in ("queued", "pending", "waiting"):
        return "wait"
    if status == "in_progress":
        return "running"
    if conclusion == "success":
        return "success"
    if conclusion in ("failure", "timed_out", "cancelled", "action_required"):
        return "fail"
    if conclusion in ("skipped", "neutral", "stale"):
        return "skip"
    return "wait"


def run_state(jobs: List[Dict[str, Any]]) -> str:
    has_running = False
    has_fail = False
    has_wait = False
    for job in jobs:
        state = map_job_state(job.get("status", ""), job.get("conclusion", ""))
        if state == "fail":
            has_fail = True
        elif state == "running":
            has_running = True
        elif state == "wait":
            has_wait = True
    if has_fail:
        return "FAIL"
    if has_running or has_wait:
        return "RUNNING"
    return "SUCCESS"


def current_step_name(job: Dict[str, Any]) -> str:
    for step in job.get("steps", []) or []:
        if step.get("status") == "in_progress":
            return step.get("name", "-")
    return "-"


def is_notify_job(job_name: str) -> bool:
    lowered = job_name.lower()
    return "discord-notify" in lowered or "discord-finalize" in lowered


def build_content(
    repo: str,
    run: Dict[str, Any],
    commit: Dict[str, Any],
    jobs: List[Dict[str, Any]],
    milestone: str,
) -> str:
    sha = run.get("head_sha", "")
    commit_first_line = to_one_line(commit.get("commit", {}).get("message", ""))
    author = (
        commit.get("author", {}) or {}
    ).get("login") or (commit.get("commit", {}).get("author", {}) or {}).get("name") or run.get(
        "actor", {}
    ).get(
        "login", "unknown"
    )
    status = run_state(jobs)
    run_url = run.get("html_url", "")
    workflow_name = run.get("name", "Workflow")
    event = run.get("event", "")
    branch = run.get("head_branch", "")

    lines = [
        f"**[{workflow_name}] {status}**",
        f"Repo: `{repo}`",
        f"Branch/Event: `{branch}` / `{event}`",
        f"Commit: `{to_short_sha(sha)}` - {commit_first_line}",
        f"By: `{author}`",
        f"Run: {run_url}",
        "",
        "**Jobs**",
    ]

    for job in sorted(jobs, key=lambda j: j.get("name", "")):
        name = job.get("name", "unknown-job")
        state = map_job_state(job.get("status", ""), job.get("conclusion", ""))
        step_name = current_step_name(job)
        if milestone and state == "running" and step_name.startswith("Discord update -"):
            step_name = milestone
        line = f"- `{name}`: `{state}` - step: `{step_name}`"
        if state in ("running", "fail") and job.get("html_url"):
            line += f" - {job['html_url']}"
        lines.append(line)
    return "\n".join(lines)


def main() -> int:
    mode = getenv_required("INPUT_MODE").lower()
    webhook_url = os.getenv("INPUT_WEBHOOK_URL", "").strip()
    token = os.getenv("INPUT_GITHUB_TOKEN", "").strip()
    message_id = os.getenv("INPUT_MESSAGE_ID", "").strip()
    milestone = os.getenv("INPUT_MILESTONE", "").strip()

    if not webhook_url:
        print("DISCORD_WEBHOOK_URL is empty. Skip notification.")
        return 0
    if not token:
        print("github token is empty. Skip notification.")
        return 0

    repo = getenv_required("GITHUB_REPOSITORY")
    run_id = getenv_required("GITHUB_RUN_ID")
    sha = getenv_required("GITHUB_SHA")
    run = gh_api(f"/repos/{repo}/actions/runs/{run_id}", token=token)
    jobs_data = gh_api(
        f"/repos/{repo}/actions/runs/{run_id}/jobs", token=token, query={"per_page": "100"}
    )
    commit = gh_api(f"/repos/{repo}/commits/{sha}", token=token)

    jobs = []
    for job in jobs_data.get("jobs", []):
        if is_notify_job(job.get("name", "")):
            continue
        jobs.append(job)

    content = build_content(repo, run, commit, jobs, milestone)
    payload = {"content": content}

    if mode == "init":
        response = webhook_request("POST", webhook_url, payload)
        mid = str(response.get("id", "")).strip()
        if mid:
            github_output = os.getenv("GITHUB_OUTPUT")
            if github_output:
                with open(github_output, "a", encoding="utf-8") as fh:
                    fh.write(f"message_id={mid}\n")
        return 0

    if mode in ("update", "finalize"):
        if not message_id:
            print("message_id is empty for update/finalize. Skip.")
            return 0
        webhook_request("PATCH", webhook_url, payload, message_id=message_id)
        return 0

    raise RuntimeError(f"Unsupported mode: {mode}")


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"discord notify failed: {exc}", file=sys.stderr)
        raise
