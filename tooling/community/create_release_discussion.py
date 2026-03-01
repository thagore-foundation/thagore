#!/usr/bin/env python3
import argparse
import json
import os
import sys
import urllib.request


def api_request(url: str, token: str, payload: dict) -> dict:
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=data,
        method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
            "Content-Type": "application/json",
            "User-Agent": "thagore-community-ops",
        },
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        body = resp.read().decode("utf-8")
    if not body:
        return {}
    return json.loads(body)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True, help="owner/repo")
    parser.add_argument("--category-id", required=True, help="GitHub discussion category id")
    parser.add_argument("--title", required=True)
    parser.add_argument("--body", required=True)
    args = parser.parse_args()

    token = os.environ.get("GH_TOKEN", "").strip()
    if not token:
        print("GH_TOKEN is required", file=sys.stderr)
        return 1

    url = f"https://api.github.com/repos/{args.repo}/discussions"
    payload = {
        "title": args.title,
        "body": args.body,
        "category_id": args.category_id,
    }
    try:
        result = api_request(url, token, payload)
    except Exception as exc:
        print(f"failed to create discussion: {exc}", file=sys.stderr)
        return 1

    number = result.get("number", 0)
    html_url = result.get("html_url", "")
    print(f"discussion created: #{number} {html_url}".strip())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
