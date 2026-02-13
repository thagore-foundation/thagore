import argparse
import os
import subprocess
import sys


def _kill_process_tree(proc: subprocess.Popen) -> None:
    if proc.poll() is not None:
        return
    if os.name == "nt":
        subprocess.run(
            ["taskkill", "/F", "/T", "/PID", str(proc.pid)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    else:
        proc.kill()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run stage command with timeout to avoid hard hangs."
    )
    parser.add_argument("--timeout", type=int, default=120, help="Timeout in seconds.")
    parser.add_argument("cmd", nargs=argparse.REMAINDER, help="Command to execute.")
    args = parser.parse_args()

    cmd = args.cmd
    if cmd and cmd[0] == "--":
        cmd = cmd[1:]

    if not cmd:
        print("[stage_guard] Missing command.")
        return 2

    print(f"[stage_guard] Running (timeout={args.timeout}s): {' '.join(cmd)}")
    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    except OSError as exc:
        print(f"[stage_guard] Failed to launch process: {exc}")
        return 126

    try:
        out, _ = proc.communicate(timeout=args.timeout)
        if out:
            print(out, end="")
        return proc.returncode
    except subprocess.TimeoutExpired:
        _kill_process_tree(proc)
        print(f"[stage_guard] TIMEOUT after {args.timeout}s. Process tree was terminated.")
        return 124


if __name__ == "__main__":
    raise SystemExit(main())
