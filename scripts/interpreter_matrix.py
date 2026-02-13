import argparse
import os
import subprocess
import sys
import tempfile
import shutil
from dataclasses import dataclass


SEPARATOR = "----------------------------------------"


@dataclass
class Case:
    name: str
    source: str
    expected_stdout: str
    expected_exit: int


CASES = [
    Case(
        name="arith_if_while",
        source=(
            "import env\n"
            "func main() -> i32:\n"
            "    let i = 0\n"
            "    let s = 0\n"
            "    while (i < 5):\n"
            "        if (i == 3):\n"
            "            s = s + 100\n"
            "        else:\n"
            "            s = s + i\n"
            "        i = i + 1\n"
            "    print(s)\n"
            "    return s\n"
        ),
        expected_stdout="107\n",
        expected_exit=107,
    ),
    Case(
        name="loop_keyword",
        source=(
            "import env\n"
            "func main() -> i32:\n"
            "    let i = 0\n"
            "    loop:\n"
            "        i = i + 1\n"
            "        if (i >= 4):\n"
            "            print(i)\n"
            "            return i\n"
        ),
        expected_stdout="4\n",
        expected_exit=4,
    ),
    Case(
        name="bool_logic",
        source=(
            "import env\n"
            "func main() -> i32:\n"
            "    let a = 1\n"
            "    let b = 0\n"
            "    if ((a == 1) and not (b == 1)):\n"
            "        print(77)\n"
            "        return 77\n"
            "    print(0)\n"
            "    return 0\n"
        ),
        expected_stdout="77\n",
        expected_exit=77,
    ),
]

INTERPRETER_ONLY_CASES = [
    Case(
        name="typed_let_interpreter",
        source=(
            "import env\n"
            "func main() -> i32:\n"
            "    let i = 0\n"
            "    let s: i32 = 0\n"
            "    while i < 3:\n"
            "        s = s + i\n"
            "        i = i + 1\n"
            "    print(s)\n"
            "    return s\n"
        ),
        expected_stdout="3\n",
        expected_exit=3,
    ),
]


STRICT_FAIL_CASE = Case(
    name="strict_unsupported_call",
    source=(
        "import env\n"
        "func main() -> i32:\n"
        "    let x = 1\n"
        "    let y = foo(1)\n"
        "    print(y)\n"
        "    return y\n"
    ),
    expected_stdout="",
    expected_exit=-1,
)


def run_cmd(cmd, cwd):
    return subprocess.run(cmd, cwd=cwd, text=True, capture_output=True)


def extract_interpreter_program_output(stdout: str) -> str:
    lines = stdout.splitlines()
    marker_indexes = [i for i, line in enumerate(lines) if line.strip() == SEPARATOR]
    if len(marker_indexes) >= 2:
        start = marker_indexes[0] + 1
        end = marker_indexes[1]
        body = "\n".join(lines[start:end])
        return (body + "\n") if body else ""
    return stdout


def require(ok: bool, message: str):
    if not ok:
        raise RuntimeError(message)


def run_case(case: Case, compiler: str, workdir: str, run_cwd: str):
    tg_path = os.path.join(workdir, f"{case.name}.tg")
    exe_path = os.path.join(workdir, f"{case.name}.exe")
    with open(tg_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(case.source)

    run_proc = run_cmd([compiler, "run", tg_path, "--strict-interpret"], run_cwd)
    run_prog_out = extract_interpreter_program_output(run_proc.stdout)
    require(
        run_proc.returncode == case.expected_exit,
        f"[{case.name}] run exit mismatch: got {run_proc.returncode}, expected {case.expected_exit}\nSTDERR:\n{run_proc.stderr}",
    )
    require(
        run_prog_out == case.expected_stdout,
        f"[{case.name}] run stdout mismatch:\n--- got ---\n{run_prog_out}--- expected ---\n{case.expected_stdout}",
    )

    build_proc = run_cmd([compiler, "build", tg_path, "-o", exe_path], run_cwd)
    require(
        build_proc.returncode == 0 and os.path.exists(exe_path),
        f"[{case.name}] build failed: exit={build_proc.returncode}\nSTDOUT:\n{build_proc.stdout}\nSTDERR:\n{build_proc.stderr}",
    )
    exe_proc = run_cmd([exe_path], run_cwd)
    require(
        exe_proc.returncode == case.expected_exit,
        f"[{case.name}] exe exit mismatch: got {exe_proc.returncode}, expected {case.expected_exit}\nSTDERR:\n{exe_proc.stderr}",
    )
    require(
        exe_proc.stdout == case.expected_stdout,
        f"[{case.name}] exe stdout mismatch:\n--- got ---\n{exe_proc.stdout}--- expected ---\n{case.expected_stdout}",
    )


def run_strict_failure_case(compiler: str, workdir: str, run_cwd: str):
    case = STRICT_FAIL_CASE
    tg_path = os.path.join(workdir, f"{case.name}.tg")
    with open(tg_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(case.source)

    proc = run_cmd([compiler, "run", tg_path, "--strict-interpret"], run_cwd)
    valid_failure_codes = {-1, 255, 4294967295}
    require(
        proc.returncode in valid_failure_codes,
        f"[{case.name}] strict mode should fail with -1/255, got {proc.returncode}",
    )
    require(
        "Interpreter strict mode: unsupported" in (proc.stdout + proc.stderr),
        f"[{case.name}] expected strict unsupported diagnostic.\nSTDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}",
    )


def run_interpreter_only_case(case: Case, compiler: str, workdir: str, run_cwd: str):
    tg_path = os.path.join(workdir, f"{case.name}.tg")
    with open(tg_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(case.source)

    run_proc = run_cmd([compiler, "run", tg_path, "--strict-interpret"], run_cwd)
    run_prog_out = extract_interpreter_program_output(run_proc.stdout)
    require(
        run_proc.returncode == case.expected_exit,
        f"[{case.name}] run exit mismatch: got {run_proc.returncode}, expected {case.expected_exit}\nSTDERR:\n{run_proc.stderr}",
    )
    require(
        run_prog_out == case.expected_stdout,
        f"[{case.name}] run stdout mismatch:\n--- got ---\n{run_prog_out}--- expected ---\n{case.expected_stdout}",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Interpreter vs build matrix smoke tests.")
    parser.add_argument("--compiler", default="stage_probe_final.exe", help="Path to thagore compiler executable.")
    args = parser.parse_args()

    compiler = os.path.abspath(args.compiler)
    if not os.path.exists(compiler):
        print(f"[FAIL] Compiler not found: {compiler}")
        return 2

    run_cwd = os.getcwd()
    with tempfile.TemporaryDirectory(prefix="thg_interp_matrix_") as td:
        runtime_candidates = [
            os.path.join(os.path.dirname(compiler), "thag_runtime.lib"),
            os.path.join(os.getcwd(), "thag_runtime.lib"),
            os.path.join(os.getcwd(), "legacy", "build", "Debug", "thag_runtime.lib"),
            os.path.join(os.getcwd(), "legacy", "thag_runtime.lib"),
        ]
        runtime_src = next((p for p in runtime_candidates if os.path.exists(p)), None)
        if runtime_src is None:
            print("[FAIL] thag_runtime.lib not found near compiler or workspace root.")
            return 2
        shutil.copyfile(runtime_src, os.path.join(td, "thag_runtime.lib"))
        try:
            for case in CASES:
                run_case(case, compiler, td, run_cwd)
                print(f"[PASS] {case.name}")
            for case in INTERPRETER_ONLY_CASES:
                run_interpreter_only_case(case, compiler, td, run_cwd)
                print(f"[PASS] {case.name}")
            run_strict_failure_case(compiler, td, run_cwd)
            print("[PASS] strict failure diagnostics")
        except RuntimeError as exc:
            print(f"[FAIL] {exc}")
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
