#!/usr/bin/env python3
"""Generate embedded_runtime.cpp from a runtime archive."""

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


def generate_cpp(archive: bytes) -> str:
    lines = _hex_lines(archive)
    body = ",\n    ".join(lines)
    return (
        '#include "thagc/infra/embedded_runtime.hpp"\n'
        "\n"
        "namespace thagore {\n"
        "const unsigned char kRuntimeLib[] = {\n"
        f"    {body}\n"
        "};\n"
        f"const unsigned int kRuntimeLibLen = {len(archive)}u;\n"
        "}  // namespace thagore\n"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, dest="input_path")
    parser.add_argument("--output", required=True, dest="output_path")
    args = parser.parse_args()

    input_path = Path(args.input_path)
    output_path = Path(args.output_path)

    if not input_path.exists():
        raise SystemExit(f"missing input archive: {input_path}")

    archive = input_path.read_bytes()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(generate_cpp(archive), encoding="utf-8")


if __name__ == "__main__":
    main()
