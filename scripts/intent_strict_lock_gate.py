import argparse
import ast
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_INTENT_SUITE = ROOT / "scripts" / "intent_suite.py"
DEFAULT_WORKFLOWS = [
    ROOT / ".github" / "workflows" / "core-ci.yml",
    ROOT / ".github" / "workflows" / "core-selfhost.yml",
]
GATE_SCRIPT_REF = "scripts/intent_strict_lock_gate.py"


def _call_name(node: ast.expr) -> str:
    if isinstance(node, ast.Name):
        return node.id
    if isinstance(node, ast.Attribute):
        return node.attr
    return ""


def _collect_string_literals(node: ast.expr) -> list[str]:
    out: list[str] = []
    if isinstance(node, (ast.List, ast.Tuple)):
        for elt in node.elts:
            if isinstance(elt, ast.Constant) and isinstance(elt.value, str):
                out.append(elt.value)
    return out


def check_intent_suite(path: Path) -> list[str]:
    if not path.exists():
        return [f"missing intent suite script: {path}"]

    text = path.read_text(encoding="utf-8")
    tree = ast.parse(text, filename=str(path))
    errors: list[str] = []

    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        if _call_name(node.func) != "run_cmd":
            continue
        if not node.args:
            continue

        argv = _collect_string_literals(node.args[0])
        if "--intent=max" not in argv:
            continue

        has_intent_lock = "--intent-lock" in argv
        has_lock_policy = ("--strict-lock" in argv) or ("--no-strict-lock" in argv)
        if not has_intent_lock or not has_lock_policy:
            errors.append(
                "intent suite run_cmd missing lock policy for --intent=max "
                f"(line {node.lineno}, has_intent_lock={has_intent_lock}, has_lock_policy={has_lock_policy})"
            )

    return errors


def check_workflows(paths: list[Path]) -> list[str]:
    errors: list[str] = []
    for wf in paths:
        if not wf.exists():
            errors.append(f"missing workflow: {wf}")
            continue
        text = wf.read_text(encoding="utf-8")
        if GATE_SCRIPT_REF not in text:
            errors.append(f"workflow does not invoke {GATE_SCRIPT_REF}: {wf}")

        for idx, raw in enumerate(text.splitlines(), start=1):
            line = raw.strip()
            if (not line) or line.startswith("#"):
                continue
            if "--intent=max" not in line:
                continue
            has_intent_lock = "--intent-lock" in line
            has_lock_policy = ("--strict-lock" in line) or ("--no-strict-lock" in line)
            if (not has_intent_lock) or (not has_lock_policy):
                errors.append(
                    f"workflow command missing lock policy for --intent=max ({wf}:{idx})"
                )
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Gate: intent=max must use explicit lock policy")
    parser.add_argument(
        "--intent-suite",
        default=str(DEFAULT_INTENT_SUITE),
        help="Path to scripts/intent_suite.py",
    )
    parser.add_argument(
        "--workflow",
        action="append",
        default=None,
        help="Workflow file to check (repeatable). Defaults: core-ci.yml + core-selfhost.yml",
    )
    args = parser.parse_args()

    suite_path = Path(args.intent_suite)
    workflow_paths = [Path(p) for p in args.workflow] if args.workflow else DEFAULT_WORKFLOWS

    errors = []
    errors.extend(check_intent_suite(suite_path))
    errors.extend(check_workflows(workflow_paths))

    print("=== Intent Strict-Lock Gate ===")
    print(f"intent_suite: {suite_path}")
    print(f"workflows: {len(workflow_paths)}")
    for wf in workflow_paths:
        print(f"  - {wf}")

    if errors:
        print("")
        for err in errors:
            print("FAIL:", err)
        raise SystemExit(1)

    print("PASS: intent strict-lock gate")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
