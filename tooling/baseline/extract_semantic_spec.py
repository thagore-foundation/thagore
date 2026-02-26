#!/usr/bin/env python3
import argparse
import json
import re
import subprocess
from pathlib import Path


def git_show(branch: str, path: str) -> str:
    return subprocess.check_output(["git", "show", f"{branch}:{path}"], text=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--branch", required=True)
    parser.add_argument("--out-dir", required=True)
    args = parser.parse_args()

    typecheck_src = git_show(args.branch, "src/semantics/typecheck/program.tg")
    lowering_src = git_show(args.branch, "src/lowering/transform/program.tg")

    typecheck_hooks = sorted(set(re.findall(r"func\s+([a-zA-Z0-9_]+)\(", typecheck_src)))
    lowering_hooks = sorted(set(re.findall(r"func\s+([a-zA-Z0-9_]+)\(", lowering_src)))
    errors = sorted(set(re.findall(r'print\("ERROR: ([^"]+)"\)', typecheck_src)))

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "type_rules_snapshot.yaml").write_text(
        "source_branch: " + args.branch + "\n"
        "typecheck_hooks:\n" + "".join(f"  - {h}\n" for h in typecheck_hooks) +
        "error_markers:\n" + "".join(f"  - \"{e}\"\n" for e in errors),
        encoding="utf-8",
    )
    (out_dir / "lowering_snapshot.yaml").write_text(
        "source_branch: " + args.branch + "\n"
        "lowering_hooks:\n" + "".join(f"  - {h}\n" for h in lowering_hooks),
        encoding="utf-8",
    )
    (out_dir / "semantic_meta.json").write_text(
        json.dumps(
            {
                "source_branch": args.branch,
                "typecheck_hooks_count": len(typecheck_hooks),
                "lowering_hooks_count": len(lowering_hooks),
                "error_markers_count": len(errors),
            },
            indent=2,
        )
        + "\n"
    )


if __name__ == "__main__":
    main()

