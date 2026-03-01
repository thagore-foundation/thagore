#!/usr/bin/env python3
"""Generate embedded_stdlib.cpp from stdlib .tg sources."""

from __future__ import annotations

import argparse
from pathlib import Path


def _hex_lines(data: bytes, width: int = 12) -> list[str]:
    if not data:
        return ["0x00"]
    values = [f"0x{byte:02x}" for byte in data]
    lines: list[str] = []
    for i in range(0, len(values), width):
        lines.append(", ".join(values[i : i + width]))
    return lines


def _normalize_relpath(path: Path) -> str:
    return path.as_posix().strip("/")


def generate_cpp(items: list[tuple[str, bytes]]) -> str:
    out: list[str] = []
    out.append('#include "thagc/driver/embedded_stdlib.hpp"')
    out.append("")
    out.append("namespace thagc::driver {")
    out.append("namespace {")
    for idx, (_, payload) in enumerate(items):
        body = ",\n    ".join(_hex_lines(payload))
        out.append(f"const unsigned char kStdlibData{idx}[] = {{")
        out.append(f"    {body}")
        out.append("};")
    out.append("}  // namespace")
    out.append("")
    out.append("const EmbeddedStdlibFile kEmbeddedStdlibFiles[] = {")
    for idx, (rel, payload) in enumerate(items):
        out.append(f'    {{"{rel}", kStdlibData{idx}, {len(payload)}u}},')
    out.append("};")
    out.append(f"const unsigned int kEmbeddedStdlibFileCount = {len(items)}u;")
    out.append("")
    out.append("}  // namespace thagc::driver")
    out.append("")
    return "\n".join(out)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, dest="root")
    parser.add_argument("--output", required=True, dest="output")
    parser.add_argument("--module", action="append", default=[], dest="modules")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    output = Path(args.output)
    modules = sorted(set(args.modules))
    if not modules:
        raise SystemExit("no --module values supplied")

    items: list[tuple[str, bytes]] = []
    for rel in modules:
        rel_path = Path(rel)
        source = root / rel_path
        if not source.exists():
            raise SystemExit(f"missing stdlib module: {source}")
        items.append((_normalize_relpath(rel_path), source.read_bytes()))

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(generate_cpp(items), encoding="utf-8")


if __name__ == "__main__":
    main()

