import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DRIVER = ROOT / "runtime" / "src" / "runtime.cpp"
DEFAULT_REGISTRY = ROOT / "docs" / "idea" / "intent_rule_registry.txt"


def parse_registry(path: Path) -> tuple[bool, int, dict[str, int], set[str]]:
    enabled = True
    total_budget = 0
    family_budget: dict[str, int] = {}
    rules: set[str] = set()
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("enabled="):
            v = line.split("=", 1)[1].strip().lower()
            enabled = v not in {"0", "false", "off", "no"}
            continue
        if line.startswith("budget.total="):
            total_budget = int(line.split("=", 1)[1].strip())
            continue
        if line.startswith("budget.family."):
            key, val = line.split("=", 1)
            fam = key[len("budget.family.") :].strip()
            family_budget[fam] = int(val.strip())
            continue
        if line.startswith("rule="):
            rid = line.split("=", 1)[1].strip()
            if rid:
                rules.add(rid)
            continue
    return enabled, total_budget, family_budget, rules


def parse_driver_rules(path: Path) -> set[str]:
    text = path.read_text(encoding="utf-8")
    out = set(re.findall(r'return\s+"(rule\.[^"]+)"', text))
    out.discard("rule.none")
    return out


def family_of(rule_id: str) -> str:
    parts = rule_id.split(".")
    if len(parts) >= 3:
        return parts[1]
    return "misc"


def main() -> int:
    parser = argparse.ArgumentParser(description="Intent rule budget gate")
    parser.add_argument("--driver", default=str(DEFAULT_DRIVER), help="Path to intent rule driver source")
    parser.add_argument("--registry", default=str(DEFAULT_REGISTRY), help="Path to registry file")
    args = parser.parse_args()

    driver_path = Path(args.driver)
    registry_path = Path(args.registry)
    if not driver_path.exists():
        raise SystemExit(f"FAIL: missing driver: {driver_path}")
    if not registry_path.exists():
        raise SystemExit(f"FAIL: missing registry: {registry_path}")

    enabled, total_budget, family_budget, reg_rules = parse_registry(registry_path)
    driver_rules = parse_driver_rules(driver_path)

    missing = sorted(driver_rules - reg_rules)
    extra = sorted(reg_rules - driver_rules)

    family_counts: dict[str, int] = {}
    for rid in reg_rules:
        fam = family_of(rid)
        family_counts[fam] = family_counts.get(fam, 0) + 1

    errors: list[str] = []
    if enabled and total_budget > 0 and len(reg_rules) > total_budget:
        errors.append(
            f"registry total rules={len(reg_rules)} exceeds budget.total={total_budget}"
        )
    for fam, cap in family_budget.items():
        used = family_counts.get(fam, 0)
        if used > cap:
            errors.append(f"family {fam} uses {used} rules > cap {cap}")
    if missing:
        errors.append("driver rules missing in registry: " + ", ".join(missing))
    if extra:
        errors.append("registry rules not found in driver: " + ", ".join(extra))

    print("=== Intent Budget Gate ===")
    print(f"enabled: {enabled}")
    print(f"driver rules: {len(driver_rules)}")
    print(f"registry rules: {len(reg_rules)}")
    print(f"budget.total: {total_budget}")
    for fam in sorted(family_counts.keys()):
        cap = family_budget.get(fam, 0)
        print(f"family.{fam}: {family_counts[fam]}/{cap}")

    if errors:
        print("")
        for err in errors:
            print("FAIL:", err)
        raise SystemExit(1)

    print("PASS: intent budget gate")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
