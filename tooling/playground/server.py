#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import subprocess
import tempfile
import time
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
PLAYGROUND_ROOT = REPO_ROOT / "playground"


def _resolve_thagc_bin(explicit: str | None) -> str:
    if explicit:
        return explicit
    env_bin = os.environ.get("THAGC_BIN", "").strip()
    if env_bin:
        return env_bin
    return "thagc"


def compile_and_run(source: str, thagc_bin: str, timeout_sec: int = 20) -> dict[str, Any]:
    start = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="thag-playground-") as td:
        root = Path(td)
        src = root / "main.tg"
        src.write_text(source, encoding="utf-8")
        out = root / ("main.exe" if os.name == "nt" else "main.bin")

        build = subprocess.run(
            [thagc_bin, "build", str(src), "-o", str(out)],
            capture_output=True,
            text=True,
            check=False,
            timeout=timeout_sec,
        )
        if build.returncode != 0:
            return {
                "ok": False,
                "stage": "build",
                "exit_code": build.returncode,
                "stdout": build.stdout,
                "stderr": build.stderr,
                "elapsed_ms": int((time.monotonic() - start) * 1000),
            }

        run = subprocess.run(
            [str(out)],
            cwd=root,
            capture_output=True,
            text=True,
            check=False,
            timeout=timeout_sec,
        )
        return {
            "ok": True,
            "stage": "run",
            "exit_code": run.returncode,
            "stdout": run.stdout,
            "stderr": run.stderr,
            "elapsed_ms": int((time.monotonic() - start) * 1000),
        }


class PlaygroundHandler(SimpleHTTPRequestHandler):
    thagc_bin = "thagc"

    def __init__(self, *args: Any, **kwargs: Any) -> None:
        super().__init__(*args, directory=str(PLAYGROUND_ROOT), **kwargs)

    def _send_json(self, payload: dict[str, Any], status: int = HTTPStatus.OK) -> None:
        data = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("content-type", "application/json; charset=utf-8")
        self.send_header("content-length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self) -> None:
        if self.path == "/health":
            self._send_json({"ok": True, "service": "thagore-playground"})
            return
        super().do_GET()

    def do_POST(self) -> None:
        if self.path != "/api/run":
            self._send_json({"ok": False, "error": "unknown endpoint"}, HTTPStatus.NOT_FOUND)
            return
        try:
            length = int(self.headers.get("content-length", "0"))
        except ValueError:
            self._send_json({"ok": False, "error": "invalid content-length"}, HTTPStatus.BAD_REQUEST)
            return
        raw = self.rfile.read(max(0, length))
        try:
            payload = json.loads(raw.decode("utf-8"))
        except json.JSONDecodeError:
            self._send_json({"ok": False, "error": "invalid json body"}, HTTPStatus.BAD_REQUEST)
            return
        source = str(payload.get("source", ""))
        if not source.strip():
            self._send_json({"ok": False, "error": "source is empty"}, HTTPStatus.BAD_REQUEST)
            return
        try:
            result = compile_and_run(source, self.thagc_bin)
        except subprocess.TimeoutExpired:
            self._send_json({"ok": False, "error": "execution timeout"}, HTTPStatus.REQUEST_TIMEOUT)
            return
        self._send_json(result, HTTPStatus.OK if result.get("ok") else HTTPStatus.BAD_REQUEST)


def main() -> int:
    parser = argparse.ArgumentParser(description="Thagore web playground server")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--thagc-bin", default="")
    args = parser.parse_args()

    PlaygroundHandler.thagc_bin = _resolve_thagc_bin(args.thagc_bin)
    server = ThreadingHTTPServer((args.host, args.port), PlaygroundHandler)
    print(f"playground: http://{args.host}:{args.port} using {PlaygroundHandler.thagc_bin}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
