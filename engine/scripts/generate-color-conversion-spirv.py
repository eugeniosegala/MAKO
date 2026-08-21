#!/usr/bin/env python3
"""Regenerate the embedded HDR colour-conversion SPIR-V header."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent
SHADER_DIR = ROOT / "mako-backend/src/shaders"
OUTPUT = SHADER_DIR / "color_conversion_spirv.hpp"
HASH_MANIFEST = SHADER_DIR / "color_conversion_spirv.hashes"
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


def source_hashes() -> list[tuple[str, str]]:
    return [
        (
            filename,
            hashlib.sha256((SHADER_DIR / filename).read_bytes()).hexdigest(),
        )
        for filename, _symbol in SHADERS
    ]


def embedded_payloads(header: str) -> dict[str, bytes]:
    payloads = {}
    for _filename, symbol in SHADERS:
        match = re.search(
            rf"\b{re.escape(symbol)}\s*=\s*\{{(.*?)\n\s*\}};",
            header,
            flags=re.DOTALL,
        )
        if match is None:
            raise SystemExit(f"Embedded SPIR-V array is missing: {symbol}")
        values = re.findall(r"0x([0-9a-fA-F]{2})", match.group(1))
        if not values:
            raise SystemExit(f"Embedded SPIR-V array is empty: {symbol}")
        payloads[symbol] = bytes(int(value, 16) for value in values)
    return payloads


def recorded_payload_hashes() -> dict[str, tuple[str, str, str]]:
    records = {}
    for line_number, raw_line in enumerate(
        HASH_MANIFEST.read_text(encoding="utf-8").splitlines(),
        start=1,
    ):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split()
        if len(fields) != 4:
            raise SystemExit(
                f"Invalid embedded SPIR-V hash record on line {line_number}"
            )
        filename, symbol, source_digest, payload_digest = fields
        if filename in records:
            raise SystemExit(f"Duplicate embedded SPIR-V hash record: {filename}")
        records[filename] = (symbol, source_digest, payload_digest)
    return records


def check_source_hashes() -> None:
    header = OUTPUT.read_text(encoding="utf-8")
    recorded = dict(
        re.findall(
            r"^// (\S+) sha256:\n// ([0-9a-f]{64})$",
            header,
            flags=re.MULTILINE,
        )
    )
    expected = dict(source_hashes())
    payloads = embedded_payloads(header)
    expected_records = {
        filename: (
            symbol,
            expected[filename],
            hashlib.sha256(payloads[symbol]).hexdigest(),
        )
        for filename, symbol in SHADERS
    }
    manifest_records = recorded_payload_hashes()
    if recorded != expected or manifest_records != expected_records:
        mismatches = [
            filename
            for filename in sorted(
                set(recorded)
                | set(expected)
                | set(manifest_records)
                | set(expected_records)
            )
            if (
                recorded.get(filename) != expected.get(filename)
                or manifest_records.get(filename) != expected_records.get(filename)
            )
        ]
        raise SystemExit(
            "Embedded colour-conversion SPIR-V is stale for: "
            + ", ".join(mismatches)
            + ". Run engine/scripts/generate-color-conversion-spirv.py "
            "with glslangValidator available."
        )
    print("Embedded colour-conversion SPIR-V source and payload hashes are current.")


def generate() -> None:
    hashes = source_hashes()
    arrays: list[tuple[str, bytes]] = []
    with tempfile.TemporaryDirectory(prefix="mako-spirv-") as temp_dir:
        for filename, symbol in SHADERS:
            source = SHADER_DIR / filename
            binary = Path(temp_dir) / f"{filename}.spv"
            subprocess.run(
                ["glslangValidator", "-V", "--quiet", "-o", str(binary), str(source)],
                check=True,
            )
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
    sources = dict(hashes)
    payloads = dict(arrays)
    manifest_lines = [
        "# Generated by generate-color-conversion-spirv.py; do not edit.",
        "# source symbol source-sha256 payload-sha256",
        *(
            f"{filename} {symbol} {sources[filename]} "
            f"{hashlib.sha256(payloads[symbol]).hexdigest()}"
            for filename, symbol in SHADERS
        ),
        "",
    ]
    HASH_MANIFEST.write_text("\n".join(manifest_lines), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Regenerate or verify embedded HDR colour-conversion SPIR-V."
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help=(
            "verify recorded GLSL source and embedded payload hashes without "
            "invoking glslangValidator"
        ),
    )
    arguments = parser.parse_args()
    if arguments.check:
        check_source_hashes()
        return
    generate()


if __name__ == "__main__":
    main()
