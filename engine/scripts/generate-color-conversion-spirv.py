#!/usr/bin/env python3
"""Regenerate the embedded HDR colour-conversion SPIR-V header."""

from __future__ import annotations

import hashlib
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent
SHADER_DIR = ROOT / "mako-backend/src/shaders"
OUTPUT = SHADER_DIR / "color_conversion_spirv.hpp"
SHADERS = (
    ("hdr10_pq_to_scrgb.comp", "hdr10PqToScRgbSpirv"),
    ("scrgb_to_hdr10_pq.comp", "scRgbToHdr10PqSpirv"),
    ("scrgb_to_hdr10_pq_packed.comp", "scRgbToHdr10PqPackedSpirv"),
)


def format_bytes(data: bytes) -> str:
    rows = []
    for offset in range(0, len(data), 12):
        values = ", ".join(f"0x{value:02x}" for value in data[offset:offset + 12])
        rows.append(f"      {values},")
    return "\n".join(rows)


def main() -> None:
    hashes: list[tuple[str, str]] = []
    arrays: list[tuple[str, bytes]] = []
    with tempfile.TemporaryDirectory(prefix="mako-spirv-") as temp_dir:
        for filename, symbol in SHADERS:
            source = SHADER_DIR / filename
            binary = Path(temp_dir) / f"{filename}.spv"
            subprocess.run(
                ["glslangValidator", "-V", "--quiet", "-o", str(binary), str(source)],
                check=True,
            )
            source_bytes = source.read_bytes()
            hashes.append((filename, hashlib.sha256(source_bytes).hexdigest()))
            arrays.append((symbol, binary.read_bytes()))

    lines = [
        "/* SPDX-License-Identifier: GPL-3.0-or-later */",
        "",
        "// Generated from the adjacent GLSL sources with glslangValidator -V.",
    ]
    for filename, digest in hashes:
        lines.extend((f"// {filename} sha256:", f"// {digest}"))
    lines.extend((
        "#pragma once",
        "",
        "#include <cstdint>",
        "#include <vector>",
        "",
        "namespace mako::backend::embedded {",
    ))
    for symbol, data in arrays:
        lines.extend((
            f"    inline const std::vector<uint8_t> {symbol} = {{",
            format_bytes(data),
            "    };",
        ))
    lines.extend(("}", ""))
    OUTPUT.write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()
