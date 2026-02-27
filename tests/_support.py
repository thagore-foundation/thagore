import os
from pathlib import Path


def resolve_thagc_bin() -> Path | None:
    env = os.environ.get("THAGC_BIN", "").strip()
    candidates = []
    if env:
        candidates.append(Path(env))
    candidates.extend(
        [
            Path("build-llvm21-run/compiler/thagc"),
            Path("build-gcc-llvm21-clean/compiler/thagc"),
            Path("build/compiler/thagc"),
        ]
    )
    for cand in candidates:
        if cand.exists() and cand.is_file():
            return cand.resolve()
    return None
