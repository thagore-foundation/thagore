import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
FIXTURE = ROOT / "tests" / "intent" / "fixtures" / "intent_demo_golden.tg"
GOLDEN_EXPLAIN = ROOT / "tests" / "intent" / "golden" / "explain.max.normalized.json"
GOLDEN_LOCK = ROOT / "tests" / "intent" / "golden" / "lock.max.normalized.json"
SUPPORTED_GOALS = [
    "reduce_sum",
    "map_filter_reduce",
    "deduplicate_sorted",
    "binary_search",
    "string_contains",
    "dot_product",
    "polynomial_eval",
]


def detect_cli() -> Path:
    candidates = [
        ROOT / "runtime" / "build" / "Release" / "thagore_runtime_cli.exe",
        ROOT / "runtime" / "build" / "Debug" / "thagore_runtime_cli.exe",
        ROOT / "runtime" / "build" / "thagore_runtime_cli",
        ROOT / "thagore_runtime_cli.exe",
        ROOT / "thagore_runtime_cli",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise SystemExit("FAIL: unable to find thagore_runtime_cli binary. Pass --cli explicitly.")


def run_cmd(cmd: list[str], expect: int = 0, cwd: Path = ROOT) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if proc.returncode != expect:
        sys.stderr.write(f"FAIL: command exit={proc.returncode}, expected={expect}\n")
        sys.stderr.write("CMD: " + " ".join(cmd) + "\n")
        if proc.stdout:
            sys.stderr.write("--- stdout ---\n" + proc.stdout + "\n")
        if proc.stderr:
            sys.stderr.write("--- stderr ---\n" + proc.stderr + "\n")
        raise SystemExit(1)
    return proc


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def json_canonical(data: dict) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def normalize_explain(doc: dict) -> dict:
    out = json.loads(json.dumps(doc))
    out["entry"] = "__ENTRY__"
    entries = out.get("entries", [])
    for idx, entry in enumerate(entries, start=1):
        entry["intent_id"] = f"__INTENT_ID_{idx}__"
    return out


def normalize_lock(doc: dict) -> dict:
    out = json.loads(json.dumps(doc))
    out["target"] = "__TARGET__"
    out["source_digest"] = "__SOURCE_DIGEST__"
    entries = out.get("entries", [])
    for idx, entry in enumerate(entries, start=1):
        entry["intent_id"] = f"__INTENT_ID_{idx}__"
        entry["constraints_digest"] = "__CONSTRAINTS_DIGEST__"
        entry["verification_digest"] = "__VERIFICATION_DIGEST__"
    return out


def assert_json_equal(actual: dict, expected_path: Path, label: str) -> None:
    expected = load_json(expected_path)
    if actual != expected:
        sys.stderr.write(f"FAIL: {label} mismatch\n")
        sys.stderr.write("--- expected ---\n")
        sys.stderr.write(json_canonical(expected))
        sys.stderr.write("--- actual ---\n")
        sys.stderr.write(json_canonical(actual))
        raise SystemExit(1)


def golden_tests(cli: Path, workdir: Path) -> None:
    proc = run_cmd([str(cli), "intent", "explain", str(FIXTURE), "--json", "--mode=max"])
    explain = json.loads(proc.stdout)
    assert_json_equal(normalize_explain(explain), GOLDEN_EXPLAIN, "intent explain --json")

    lock_path = workdir / "intent_demo.lock"
    run_cmd([str(cli), "intent", "lock", str(FIXTURE), "-o", str(lock_path), "--mode=max"])
    lock_doc = load_json(lock_path)
    assert_json_equal(normalize_lock(lock_doc), GOLDEN_LOCK, "intent lock")


def differential_test(cli: Path, workdir: Path) -> None:
    canonical_src = workdir / "canonical.tg"
    intent_src = workdir / "with_intent.tg"
    lock_path = workdir / "with_intent.lock"

    canonical_src.write_text(
        "func main() -> i32:\n"
        "    print(\"INTENT_DIFF_OK\")\n"
        "    return 0\n",
        encoding="utf-8",
    )
    intent_src.write_text(
        "intent block:\n"
        "    goal: reduce_sum\n"
        "    constraints:\n"
        "        deterministic == true\n"
        "\n"
        "func main() -> i32:\n"
        "    print(\"INTENT_DIFF_OK\")\n"
        "    return 0\n",
        encoding="utf-8",
    )

    exe_ext = ".exe" if os.name == "nt" else ""
    canonical_bin = workdir / f"canonical{exe_ext}"
    intent_bin = workdir / f"with_intent{exe_ext}"

    run_cmd([str(cli), "build", str(canonical_src), "--intent=off", "-o", str(canonical_bin)])
    run_cmd([str(cli), "intent", "lock", str(intent_src), "-o", str(lock_path), "--mode=max"])
    run_cmd(
        [
            str(cli),
            "build",
            str(intent_src),
            "--intent=max",
            "--strict-lock",
            "--intent-lock",
            str(lock_path),
            "-o",
            str(intent_bin),
        ]
    )

    out_a = run_cmd([str(canonical_bin)]).stdout
    out_b = run_cmd([str(intent_bin)]).stdout
    if out_a != out_b:
        sys.stderr.write("FAIL: differential runtime output mismatch\n")
        sys.stderr.write("--- canonical ---\n" + out_a + "\n")
        sys.stderr.write("--- intent ---\n" + out_b + "\n")
        raise SystemExit(1)


def property_tests(cli: Path, workdir: Path, rounds: int) -> None:
    for goal in SUPPORTED_GOALS:
        src = workdir / f"prop_{goal}.tg"
        src.write_text(
            "intent block:\n"
            f"    goal: {goal}\n"
            "    constraints:\n"
            "        deterministic == true\n"
            "\n"
            "func main() -> i32:\n"
            "    print(\"OK\")\n"
            "    return 0\n",
            encoding="utf-8",
        )
        chosen: str | None = None
        for _ in range(rounds):
            proc = run_cmd([str(cli), "intent", "explain", str(src), "--json", "--mode=max"])
            payload = json.loads(proc.stdout)
            entries = payload.get("entries", [])
            if len(entries) != 1:
                raise SystemExit(f"FAIL: expected one entry for goal {goal}")
            entry = entries[0]
            current = str(entry.get("selected_rule", ""))
            verified = bool(entry.get("verified", False))
            if not current.startswith("rule."):
                raise SystemExit(f"FAIL: invalid selected_rule for goal {goal}: {current}")
            if not verified:
                raise SystemExit(f"FAIL: expected verified=true for goal {goal}")
            if chosen is None:
                chosen = current
            elif chosen != current:
                raise SystemExit(
                    f"FAIL: non-deterministic selected_rule for goal {goal}: {chosen} vs {current}"
                )

        lock_a = workdir / f"prop_{goal}.a.lock"
        lock_b = workdir / f"prop_{goal}.b.lock"
        run_cmd([str(cli), "intent", "lock", str(src), "-o", str(lock_a), "--mode=max"])
        run_cmd([str(cli), "intent", "lock", str(src), "-o", str(lock_b), "--mode=max"])
        if lock_a.read_text(encoding="utf-8") != lock_b.read_text(encoding="utf-8"):
            raise SystemExit(f"FAIL: lockfile changed across repeated runs for goal {goal}")


def strict_lock_gate_test(cli: Path, workdir: Path) -> None:
    src = workdir / "strict_lock_src.tg"
    src.write_text(
        "intent loop i in 0..n:\n"
        "    goal: reduce_sum\n"
        "    constraints:\n"
        "        deterministic == true\n"
        "\n"
        "func main() -> i32:\n"
        "    print(\"STRICT_OK\")\n"
        "    return 0\n",
        encoding="utf-8",
    )

    lock_good = workdir / "strict.lock"
    run_cmd([str(cli), "intent", "lock", str(src), "-o", str(lock_good), "--mode=max"])
    lock_bad = workdir / "strict.bad.lock"
    payload = load_json(lock_good)
    payload["entries"][0]["selected_rule"] = "rule.reduce_sum.scalar.v1"
    lock_bad.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")

    exe_ext = ".exe" if os.name == "nt" else ""
    out_bin = workdir / f"strict{exe_ext}"
    run_cmd(
        [
            str(cli),
            "build",
            str(src),
            "--intent=max",
            "--strict-lock",
            "--intent-lock",
            str(lock_good),
            "-o",
            str(out_bin),
        ]
    )
    run_cmd(
        [
            str(cli),
            "build",
            str(src),
            "--intent=max",
            "--strict-lock",
            "--intent-lock",
            str(lock_bad),
            "-o",
            str(workdir / f"strict_bad{exe_ext}"),
        ],
        expect=1,
    )


def doctor_smoke(cli: Path) -> None:
    run_cmd([str(cli), "intent", "doctor", str(FIXTURE)])


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Intent end-to-end validation suite")
    parser.add_argument("--cli", default="", help="Path to thagore_runtime_cli binary")
    parser.add_argument("--rounds", type=int, default=5, help="Determinism rounds per goal")
    parser.add_argument("--keep-workdir", action="store_true", help="Keep temporary workdir")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    cli = Path(args.cli) if args.cli else detect_cli()
    if not cli.exists():
        raise SystemExit(f"FAIL: cli not found: {cli}")

    temp_root = Path(tempfile.mkdtemp(prefix="thagore_intent_suite_", dir=str(ROOT)))
    try:
        doctor_smoke(cli)
        golden_tests(cli, temp_root)
        differential_test(cli, temp_root)
        property_tests(cli, temp_root, args.rounds)
        strict_lock_gate_test(cli, temp_root)
        print("PASS: intent suite")
        print(f"workdir: {temp_root}")
        return 0
    finally:
        if args.keep_workdir:
            print(f"kept workdir: {temp_root}")
        else:
            shutil.rmtree(temp_root, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
