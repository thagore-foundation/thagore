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
    "binary_search_sorted",
    "lower_bound_sorted",
    "upper_bound_sorted",
    "count_less_sorted",
    "count_less_equal_sorted",
    "count_greater_sorted",
    "count_greater_equal_sorted",
    "count_equal_sorted",
    "count_not_equal_sorted",
    "count_range_sorted",
    "count_outside_range_sorted",
    "two_sum_sorted_exists",
    "string_contains",
    "dot_product",
    "polynomial_eval",
    "fibonacci_dp",
    "tribonacci_dp",
    "factorial_iterative",
    "power_fast",
    "gcd_euclid",
    "is_prime_fast",
    "count_divisors_sqrt",
    "interval_cover_greedy",
    "bit_peel_iterative",
    "sum_formula",
    "sum_squares_formula",
    "sum_cubes_formula",
    "sum_even_squares_formula",
    "sum_odd_squares_formula",
    "sum_even_cubes_formula",
    "sum_odd_cubes_formula",
    "sum_even_formula",
    "sum_odd_formula",
    "sort_ascending",
    "search_element",
    "sqrt_bounded_loop",
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


def run_cmd(
    cmd: list[str],
    expect: int = 0,
    cwd: Path = ROOT,
    extra_env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    if extra_env:
        env.update(extra_env)
    proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, env=env)
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


def policy_presets_test(cli: Path, workdir: Path) -> None:
    src = workdir / "policy_src.tg"
    src.write_text(
        "intent loop i in 0..n:\n"
        "    goal: reduce_sum\n"
        "    constraints:\n"
        "        deterministic == true\n"
        "\n"
        "func main() -> i32:\n"
        "    print(\"POLICY_OK\")\n"
        "    return 0\n",
        encoding="utf-8",
    )

    lock_good = workdir / "policy.lock"
    run_cmd([str(cli), "intent", "lock", str(src), "-o", str(lock_good), "--mode=max"])
    lock_bad = workdir / "policy.bad.lock"
    payload = load_json(lock_good)
    payload["entries"][0]["selected_rule"] = "rule.reduce_sum.scalar.v1"
    lock_bad.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")

    exe_ext = ".exe" if os.name == "nt" else ""
    run_cmd(
        [
            str(cli),
            "build",
            str(src),
            "--intent-policy=safe",
            "--intent-lock",
            str(lock_good),
            "-o",
            str(workdir / f"policy_safe_ok{exe_ext}"),
        ]
    )
    run_cmd(
        [
            str(cli),
            "build",
            str(src),
            "--intent-policy=safe",
            "--intent-lock",
            str(lock_bad),
            "-o",
            str(workdir / f"policy_safe_bad{exe_ext}"),
        ],
        expect=1,
    )

    run_cmd(
        [
            str(cli),
            "build",
            str(src),
            "--intent-policy=debug",
            "--intent-lock",
            str(lock_bad),
            "-o",
            str(workdir / f"policy_debug_ok{exe_ext}"),
        ]
    )
    run_cmd(
        [
            str(cli),
            "build",
            str(src),
            "--intent-policy=safe",
            "--no-strict-lock",
            "--intent=max",
            "--intent-lock",
            str(lock_bad),
            "-o",
            str(workdir / f"policy_override_ok{exe_ext}"),
        ]
    )

    env_proc = run_cmd(
        [
            str(cli),
            "build",
            str(src),
            "--intent-lock",
            str(lock_good),
            "-o",
            str(workdir / f"policy_env_ok{exe_ext}"),
        ],
        extra_env={"THAG_INTENT_POLICY": "fast"},
    )
    if "[intent] mode=min" not in env_proc.stdout:
        raise SystemExit("FAIL: THAG_INTENT_POLICY=fast did not resolve to intent mode=min")


def tribonacci_rewrite_smoke(cli: Path, workdir: Path) -> None:
    src = workdir / "trib_rewrite.tg"
    src.write_text(
        "intent func trib(n: i32) -> i32:\n"
        "    goal: tribonacci_dp\n"
        "    constraints:\n"
        "        deterministic == true\n"
        "\n"
        "func trib(n: i32) -> i32:\n"
        "    if (n == 0):\n"
        "        return 0\n"
        "    if (n == 1):\n"
        "        return 1\n"
        "    if (n == 2):\n"
        "        return 1\n"
        "    return trib(n - 1) + trib(n - 2) + trib(n - 3)\n"
        "\n"
        "func main() -> i32:\n"
        "    print(\"TRI_REWRITE_OK\")\n"
        "    return 0\n",
        encoding="utf-8",
    )
    exe_ext = ".exe" if os.name == "nt" else ""
    out_bin = workdir / f"trib_rewrite{exe_ext}"
    proc = run_cmd([str(cli), "build", str(src), "--intent=max", "-o", str(out_bin)])
    if "intent rewrite applied for trib using rule.dp.trib.iterative.v1" not in proc.stdout:
        raise SystemExit("FAIL: tribonacci rewrite note was not emitted")


def strategy_pinning_test(cli: Path, workdir: Path) -> None:
    src = workdir / "strategy_pin_ok.tg"
    src.write_text(
        "intent func fib(n: i32) -> i32:\n"
        "    goal: fibonacci_dp\n"
        "    strategy: dp.fib.v1\n"
        "    constraints:\n"
        "        deterministic == true\n"
        "\n"
        "func fib(n: i32) -> i32:\n"
        "    if (n < 2):\n"
        "        return n\n"
        "    return fib(n - 1) + fib(n - 2)\n"
        "\n"
        "func main() -> i32:\n"
        "    print(\"STRATEGY_PIN_OK\")\n"
        "    return 0\n",
        encoding="utf-8",
    )
    proc = run_cmd([str(cli), "intent", "explain", str(src), "--json", "--mode=max"])
    payload = json.loads(proc.stdout)
    entries = payload.get("entries", [])
    if len(entries) != 1:
        raise SystemExit("FAIL: expected one intent entry for strategy pin test")
    entry = entries[0]
    if entry.get("selected_rule") != "rule.fibonacci_dp.iterative.v1":
        raise SystemExit("FAIL: strategy pin dp.fib.v1 did not force rule.fibonacci_dp.iterative.v1")
    if not bool(entry.get("verified", False)):
        raise SystemExit("FAIL: strategy pin selected rule must be verified")

    bad_unknown = workdir / "strategy_pin_unknown.tg"
    bad_unknown.write_text(
        "intent block:\n"
        "    goal: fibonacci_dp\n"
        "    strategy: totally.unknown.strategy\n"
        "    constraints:\n"
        "        deterministic == true\n"
        "\n"
        "func main() -> i32:\n"
        "    print(\"BAD\")\n"
        "    return 0\n",
        encoding="utf-8",
    )
    run_cmd([str(cli), "intent", "lock", str(bad_unknown), "--mode=max"], expect=1)

    bad_mismatch = workdir / "strategy_pin_mismatch.tg"
    bad_mismatch.write_text(
        "intent block:\n"
        "    goal: fibonacci_dp\n"
        "    strategy: search.binary.v1\n"
        "    constraints:\n"
        "        deterministic == true\n"
        "\n"
        "func main() -> i32:\n"
        "    print(\"BAD\")\n"
        "    return 0\n",
        encoding="utf-8",
    )
    run_cmd([str(cli), "intent", "lock", str(bad_mismatch), "--mode=max"], expect=1)


def intent_disable_test(cli: Path, workdir: Path) -> None:
    goal_off_src = workdir / "intent_goal_off.tg"
    goal_off_src.write_text(
        "intent block:\n"
        "    goal: off\n"
        "\n"
        "func main() -> i32:\n"
        "    print(\"GOAL_OFF_OK\")\n"
        "    return 0\n",
        encoding="utf-8",
    )
    explain_goal_off = run_cmd([str(cli), "intent", "explain", str(goal_off_src), "--json", "--mode=max"])
    payload_goal_off = json.loads(explain_goal_off.stdout)
    entry_goal_off = payload_goal_off.get("entries", [{}])[0]
    if entry_goal_off.get("selected_rule") != "rule.intent.off":
        raise SystemExit("FAIL: goal: off must select rule.intent.off")
    if not bool(entry_goal_off.get("verified", False)):
        raise SystemExit("FAIL: goal: off must be verified")

    lock_goal_off = workdir / "goal_off.lock"
    run_cmd([str(cli), "intent", "lock", str(goal_off_src), "-o", str(lock_goal_off), "--mode=max"])
    lock_payload = load_json(lock_goal_off)
    if lock_payload["entries"][0]["selected_rule"] != "rule.intent.off":
        raise SystemExit("FAIL: goal: off lock entry must use rule.intent.off")

    constraint_off_src = workdir / "intent_constraint_off.tg"
    constraint_off_src.write_text(
        "intent block:\n"
        "    goal: fibonacci_dp\n"
        "    constraints:\n"
        "        deterministic == true\n"
        "        intent == false\n"
        "\n"
        "func main() -> i32:\n"
        "    print(\"CONSTRAINT_OFF_OK\")\n"
        "    return 0\n",
        encoding="utf-8",
    )
    explain_constraint_off = run_cmd([str(cli), "intent", "explain", str(constraint_off_src), "--json", "--mode=max"])
    payload_constraint_off = json.loads(explain_constraint_off.stdout)
    entry_constraint_off = payload_constraint_off.get("entries", [{}])[0]
    if entry_constraint_off.get("selected_rule") != "rule.intent.off":
        raise SystemExit("FAIL: constraints intent==false must select rule.intent.off")
    if entry_constraint_off.get("verify_reason") != "intent-disabled":
        raise SystemExit("FAIL: disabled intent should return verify_reason=intent-disabled")


def runtime_registry_gate_test(cli: Path, workdir: Path) -> None:
    src = workdir / "registry_gate_src.tg"
    src.write_text(
        "intent block:\n"
        "    goal: reduce_sum\n"
        "    constraints:\n"
        "        deterministic == true\n"
        "\n"
        "func main() -> i32:\n"
        "    print(\"REGISTRY_GATE_OK\")\n"
        "    return 0\n",
        encoding="utf-8",
    )

    reg_ok = workdir / "registry.ok.txt"
    reg_ok.write_text(
        "enabled=1\n"
        "budget.total=4\n"
        "budget.family.reduce_sum=4\n"
        "rule=rule.reduce_sum.simd.v2\n",
        encoding="utf-8",
    )
    run_cmd(
        [str(cli), "intent", "lock", str(src), "-o", str(workdir / "registry_ok.lock"), "--mode=max"],
        extra_env={"THAG_INTENT_REGISTRY": str(reg_ok)},
    )

    reg_block = workdir / "registry.block.txt"
    reg_block.write_text(
        "enabled=1\n"
        "budget.total=4\n"
        "rule=rule.reduce_sum.scalar.v1\n",
        encoding="utf-8",
    )
    run_cmd(
        [str(cli), "intent", "lock", str(src), "-o", str(workdir / "registry_block.lock"), "--mode=max"],
        expect=1,
        extra_env={"THAG_INTENT_REGISTRY": str(reg_block)},
    )

    reg_budget_zero = workdir / "registry.budget0.txt"
    reg_budget_zero.write_text(
        "enabled=1\n"
        "budget.total=0\n"
        "rule=rule.reduce_sum.simd.v2\n",
        encoding="utf-8",
    )
    run_cmd(
        [str(cli), "intent", "lock", str(src), "-o", str(workdir / "registry_budget0.lock"), "--mode=max"],
        expect=1,
        extra_env={"THAG_INTENT_REGISTRY": str(reg_budget_zero)},
    )

    reg_disabled = workdir / "registry.disabled.txt"
    reg_disabled.write_text(
        "enabled=0\n"
        "budget.total=0\n"
        "rule=rule.unsupported\n",
        encoding="utf-8",
    )
    run_cmd(
        [str(cli), "intent", "lock", str(src), "-o", str(workdir / "registry_disabled.lock"), "--mode=max"],
        extra_env={"THAG_INTENT_REGISTRY": str(reg_disabled)},
    )


def auto_plan_name_heuristic_test(cli: Path, workdir: Path) -> None:
    cases = [
        ("lower_bound_index", "lower_bound_sorted", "rule.search.lower_bound.sorted.v1"),
        ("count_range_query", "count_range_sorted", "rule.search.count_range.sorted.v1"),
        ("two_sum_exists", "two_sum_sorted_exists", "rule.search.two_sum.sorted.v1"),
        ("gcd_value", "gcd_euclid", "rule.math.gcd.euclid.v1"),
        ("fast_pow", "power_fast", "rule.math.pow.binary_exp.v1"),
        ("prime_check", "is_prime_fast", "rule.number.prime.sqrt.v1"),
        ("sprinkler_cover", "interval_cover_greedy", "rule.greedy.interval_cover.v1"),
        ("sum_even_values", "sum_even_formula", "rule.math.sum_even.formula.v1"),
    ]
    for idx, (fn_name, expected_goal, expected_rule) in enumerate(cases, start=1):
        src = workdir / f"auto_name_case_{idx}.tg"
        src.write_text(
            f"intent func {fn_name}(n: i32) -> i32:\n"
            "    goal: auto_plan\n"
            "    constraints:\n"
            "        deterministic == true\n"
            "\n"
            f"func {fn_name}(n: i32) -> i32:\n"
            "    return n\n"
            "\n"
            "func main() -> i32:\n"
            "    print(\"AUTO_NAME_OK\")\n"
            "    return 0\n",
            encoding="utf-8",
        )
        proc = run_cmd([str(cli), "intent", "explain", str(src), "--json", "--mode=max"])
        payload = json.loads(proc.stdout)
        entries = payload.get("entries", [])
        if len(entries) != 1:
            raise SystemExit(f"FAIL: expected one entry for auto-plan name case {fn_name}")
        entry = entries[0]
        if entry.get("goal") != expected_goal:
            raise SystemExit(
                f"FAIL: auto_plan name heuristic goal mismatch for {fn_name}: "
                f"{entry.get('goal')} != {expected_goal}"
            )
        if entry.get("selected_rule") != expected_rule:
            raise SystemExit(
                f"FAIL: auto_plan selected rule mismatch for {fn_name}: "
                f"{entry.get('selected_rule')} != {expected_rule}"
            )
        if not bool(entry.get("verified", False)):
            raise SystemExit(f"FAIL: auto-plan name case {fn_name} must be verified")


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
        policy_presets_test(cli, temp_root)
        tribonacci_rewrite_smoke(cli, temp_root)
        strategy_pinning_test(cli, temp_root)
        intent_disable_test(cli, temp_root)
        runtime_registry_gate_test(cli, temp_root)
        auto_plan_name_heuristic_test(cli, temp_root)
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
