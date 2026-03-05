#!/usr/bin/env python3
import argparse
import json
import re
import subprocess
from pathlib import Path


def resolve_ref(branch: str) -> str:
    candidates = [
        branch,
        f"origin/{branch}",
        f"refs/remotes/origin/{branch}",
        f"refs/heads/{branch}",
        "HEAD",
    ]
    for ref in candidates:
        if subprocess.run(["git", "rev-parse", "--verify", "--quiet", ref], check=False).returncode == 0:
            return ref
    return "HEAD"


def git_show(branch: str, path: str) -> str:
    return subprocess.check_output(["git", "show", f"{branch}:{path}"], text=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--branch", required=True)
    parser.add_argument("--out-dir", required=True)
    args = parser.parse_args()

    source_ref = resolve_ref(args.branch)
    syntax_doc = git_show(source_ref, "docs/starlight/src/content/docs/syntax/index.mdx")
    parser_src = git_show(source_ref, "src/syntax/native/parser.tg")
    lexer_src = git_show(source_ref, "src/syntax/native/lexer.tg")

    tokens = sorted(set(re.findall(r"[A-Z_]{3,}", lexer_src)))
    keywords = sorted(set(re.findall(r'\b(func|let|if|else|while|struct|impl|return|import|extern)\b', syntax_doc)))
    parser_hooks = sorted(set(re.findall(r"func\s+([a-zA-Z0-9_]+)\(", parser_src)))

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "tokens.yaml").write_text(
        "source_branch: " + args.branch + "\n" +
        "source_ref: " + source_ref + "\n" +
        "tokens:\n" + "".join(f"  - {t}\n" for t in tokens),
        encoding="utf-8",
    )
    (out_dir / "syntax_rules.yaml").write_text(
        "source_branch: " + args.branch + "\n" +
        "source_ref: " + source_ref + "\n" +
        "keywords:\n" + "".join(f"  - {k}\n" for k in keywords) +
        "parser_entrypoints:\n" + "".join(f"  - {h}\n" for h in parser_hooks[:120]),
        encoding="utf-8",
    )
    (out_dir / "precedence.yaml").write_text(
        "source_branch: " + args.branch + "\n"
        "source_ref: " + source_ref + "\n"
        "operators:\n"
        "  - level: 1\n    ops: [\"*\", \"/\"]\n"
        "  - level: 2\n    ops: [\"+\", \"-\"]\n"
        "  - level: 3\n    ops: [\"<\", \">\", \"<=\", \">=\", \"==\", \"!=\"]\n",
        encoding="utf-8",
    )

    meta = {
        "source_branch": args.branch,
        "source_ref": source_ref,
        "tokens_count": len(tokens),
        "keywords_count": len(keywords),
        "parser_hooks_count": len(parser_hooks),
    }
    (out_dir / "grammar_meta.json").write_text(json.dumps(meta, indent=2) + "\n")


if __name__ == "__main__":
    main()
