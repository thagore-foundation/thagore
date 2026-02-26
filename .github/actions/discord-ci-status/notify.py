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


def status_icon(state: str) -> str:
    return {
        "SUCCESS": "✅",
        "RUNNING": "🟡",
        "FAIL": "❌",
        "success": "✅",
        "running": "🟡",
        "wait": "⏳",
        "skip": "⏭️",
        "fail": "❌",
    }.get(state, "ℹ️")


def status_color(state: str) -> int:
    return {
        "SUCCESS": 0x2ECC71,
        "RUNNING": 0xF1C40F,
        "FAIL": 0xE74C3C,
    }.get(state, 0x95A5A6)


def run_state_label(state: str) -> str:
    return {
        "SUCCESS": "THÀNH CÔNG",
        "RUNNING": "ĐANG CHẠY",
        "FAIL": "THẤT BẠI",
    }.get(state, state)


def workflow_icon(name: str) -> str:
    lowered = (name or "").lower()
    if "release" in lowered:
        return "🚀"
    if "seed" in lowered:
        return "🌱"
    if "docs" in lowered:
        return "📚"
    if "policy" in lowered:
        return "🛡️"
    if "selfhost" in lowered:
        return "🏗️"
    if "ci" in lowered:
        return "⚙️"
    return "🔔"


def truncate(text: str, max_len: int) -> str:
    if len(text) <= max_len:
        return text
    return text[: max_len - 1] + "…"


def chunk_lines(lines: List[str], max_len: int = 950) -> List[str]:
    chunks: List[str] = []
    cur: List[str] = []
    cur_len = 0
    for line in lines:
        piece = truncate(line, 300)
        add_len = len(piece) + (1 if cur else 0)
        if cur and (cur_len + add_len) > max_len:
            chunks.append("\n".join(cur))
            cur = [piece]
            cur_len = len(piece)
        else:
            cur.append(piece)
            cur_len += add_len
    if cur:
        chunks.append("\n".join(cur))
    return chunks


def parse_int(value: str, default_value: int, minimum: int, maximum: int) -> int:
    try:
        parsed = int(value)
    except (TypeError, ValueError):
        return default_value
    return max(minimum, min(maximum, parsed))


def current_step_name(job: Dict[str, Any]) -> str:
    for step in job.get("steps", []) or []:
        if step.get("status") == "in_progress":
            return step.get("name", "-")
    return "-"


def is_notify_job(job_name: str) -> bool:
    lowered = job_name.lower()
    return "discord-notify" in lowered or "discord-finalize" in lowered


def build_payload(
    repo: str,
    run: Dict[str, Any],
    commit: Dict[str, Any],
    jobs: List[Dict[str, Any]],
    milestone: str,
) -> Dict[str, Any]:
    sha = run.get("head_sha", "")
    commit_first_line = to_one_line(commit.get("commit", {}).get("message", "")) or "(không có nội dung)"
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
    branch = run.get("head_branch", "") or "(không rõ)"
    wf_icon = workflow_icon(workflow_name)

    job_lines: List[str] = []
    counts = {"wait": 0, "running": 0, "skip": 0, "success": 0, "fail": 0}

    for job in sorted(jobs, key=lambda j: j.get("name", "")):
        name = job.get("name", "unknown-job")
        state = map_job_state(job.get("status", ""), job.get("conclusion", ""))
        if state in counts:
            counts[state] += 1
        step_name = current_step_name(job)
        if milestone and state == "running" and step_name.startswith("Discord update -"):
            step_name = milestone
        icon = status_icon(state)
        line = f"{icon} `{name}` • `{state}` • `{step_name}`"
        if state in ("running", "fail") and job.get("html_url"):
            line += f" • [link]({job['html_url']})"
        job_lines.append(line)

    fields: List[Dict[str, Any]] = [
        {
            "name": "🧾 Commit",
            "value": truncate(f"`{to_short_sha(sha)}` {commit_first_line}", 1024),
            "inline": False,
        },
        {"name": "👤 Người commit", "value": truncate(f"`{author}`", 1024), "inline": True},
        {
            "name": "🌿 Nhánh / Sự kiện",
            "value": truncate(f"`{branch}` / `{event}`", 1024),
            "inline": True,
        },
        {
            "name": "📊 Summary",
            "value": (
                f"{status_icon('wait')} `{counts['wait']}`  "
                f"{status_icon('running')} `{counts['running']}`  "
                f"{status_icon('skip')} `{counts['skip']}`  "
                f"{status_icon('success')} `{counts['success']}`  "
                f"{status_icon('fail')} `{counts['fail']}`"
            ),
            "inline": False,
        },
    ]

    chunks = chunk_lines(job_lines) or ["(không có job)"]
    for idx, chunk in enumerate(chunks, start=1):
        suffix = "" if len(chunks) == 1 else f" ({idx}/{len(chunks)})"
        fields.append({"name": f"🛠️ Job{suffix}", "value": chunk, "inline": False})

    embed: Dict[str, Any] = {
        "title": f"{wf_icon} {status_icon(status)} [{workflow_name}] {run_state_label(status)}",
        "url": run_url,
        "color": status_color(status),
        "fields": fields[:25],
        "footer": {"text": f"📦 {repo} • cập nhật tự động"},
    }
    actor = run.get("actor", {}) or {}
    if actor.get("login"):
        author_obj: Dict[str, Any] = {"name": f"👤 Người chạy: {actor['login']}"}
        if actor.get("avatar_url"):
            author_obj["icon_url"] = actor["avatar_url"]
        embed["author"] = author_obj
    if actor.get("avatar_url"):
        embed["thumbnail"] = {"url": actor["avatar_url"]}
    timestamp = run.get("updated_at") or run.get("run_started_at")
    if timestamp:
        embed["timestamp"] = timestamp

    return {"content": "", "embeds": [embed], "allowed_mentions": {"parse": []}}


def fetch_run_and_jobs(repo: str, run_id: str, token: str) -> Dict[str, Any]:
    run = gh_api(f"/repos/{repo}/actions/runs/{run_id}", token=token)
    jobs_data = gh_api(
        f"/repos/{repo}/actions/runs/{run_id}/jobs", token=token, query={"per_page": "100"}
    )
    jobs = []
    for job in jobs_data.get("jobs", []):
        if is_notify_job(job.get("name", "")):
            continue
        jobs.append(job)
    return {"run": run, "jobs": jobs}


def main() -> int:
    mode = getenv_required("INPUT_MODE").lower()
    webhook_url = os.getenv("INPUT_WEBHOOK_URL", "").strip()
    token = os.getenv("INPUT_GITHUB_TOKEN", "").strip()
    message_id = os.getenv("INPUT_MESSAGE_ID", "").strip()
    milestone = os.getenv("INPUT_MILESTONE", "").strip()
    poll_seconds = parse_int(os.getenv("INPUT_POLL_SECONDS", "20"), 20, 5, 120)
    max_minutes = parse_int(os.getenv("INPUT_MAX_MINUTES", "180"), 180, 10, 1440)

    if not webhook_url:
        print("DISCORD_WEBHOOK_URL đang trống. Bỏ qua thông báo.")
        return 0
    if not token:
        print("GitHub token đang trống. Bỏ qua thông báo.")
        return 0

    repo = getenv_required("GITHUB_REPOSITORY")
    run_id = getenv_required("GITHUB_RUN_ID")
    sha = getenv_required("GITHUB_SHA")
    commit = gh_api(f"/repos/{repo}/commits/{sha}", token=token)

    if mode == "init":
        snapshot = fetch_run_and_jobs(repo, run_id, token)
        payload = build_payload(repo, snapshot["run"], commit, snapshot["jobs"], milestone)
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
            print("message_id đang trống cho update/finalize. Bỏ qua.")
            return 0
        snapshot = fetch_run_and_jobs(repo, run_id, token)
        payload = build_payload(repo, snapshot["run"], commit, snapshot["jobs"], milestone)
        webhook_request("PATCH", webhook_url, payload, message_id=message_id)
        return 0

    if mode == "watch":
        if not message_id:
            print("message_id đang trống cho watch. Bỏ qua.")
            return 0
        deadline = time.time() + (max_minutes * 60)
        while True:
            snapshot = fetch_run_and_jobs(repo, run_id, token)
            payload = build_payload(repo, snapshot["run"], commit, snapshot["jobs"], "")
            webhook_request("PATCH", webhook_url, payload, message_id=message_id)
            state = run_state(snapshot["jobs"])
            if state in ("SUCCESS", "FAIL"):
                break
            if time.time() >= deadline:
                print("Đã đạt timeout của watch, dừng vòng lặp.")
                break
            time.sleep(poll_seconds)
        return 0

    raise RuntimeError(f"Unsupported mode: {mode}")


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"discord notify failed: {exc}", file=sys.stderr)
        raise SystemExit(0)
