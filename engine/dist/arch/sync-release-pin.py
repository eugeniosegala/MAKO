#!/usr/bin/env python3
"""Synchronize the Arch recipe with MAKO Decky's verified Renderer pin."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


def _replace_exactly_once(
    contents: str,
    pattern: str,
    replacement: str,
    field: str,
    pkgbuild_path: Path,
) -> str:
    updated, replacements = re.subn(
        pattern, replacement, contents, flags=re.MULTILINE
    )
    if replacements != 1:
        raise ValueError(
            f"Expected exactly one {field} assignment in {pkgbuild_path}"
        )
    return updated


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit(
            "Usage: sync-release-pin.py "
            "<plugin/package.json> <engine/dist/arch/PKGBUILD>"
        )

    pin_path = Path(sys.argv[1])
    pkgbuild_path = Path(sys.argv[2])
    manifest = json.loads(pin_path.read_text(encoding="utf-8"))
    binaries = manifest.get("remote_binary")
    if not isinstance(binaries, list) or len(binaries) != 1:
        raise ValueError(f"{pin_path} must contain exactly one remote_binary entry")

    binary = binaries[0]
    version = binary.get("version")
    checksum = binary.get("sha256hash")
    if not isinstance(version, str) or not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError(f"Invalid Renderer version in {pin_path}: {version!r}")
    if not isinstance(checksum, str) or not re.fullmatch(
        r"[0-9a-fA-F]{64}", checksum
    ):
        raise ValueError(f"Invalid Renderer SHA-256 in {pin_path}")

    expected_name = f"MAKO-Renderer-v{version}-linux.tar.xz"
    expected_url = (
        "https://github.com/eugeniosegala/MAKO/releases/download/"
        f"render-v{version}/{expected_name}"
    )
    if binary.get("name") != expected_name or binary.get("url") != expected_url:
        raise ValueError(
            f"{pin_path} does not describe the canonical {expected_name} release asset"
        )

    original = pkgbuild_path.read_text(encoding="utf-8")
    current_version_match = re.search(r"^pkgver=(.+)$", original, re.MULTILINE)
    if current_version_match is None:
        raise ValueError(f"Could not find pkgver in {pkgbuild_path}")

    updated = _replace_exactly_once(
        original,
        r"^pkgver=.*$",
        f"pkgver={version}",
        "pkgver",
        pkgbuild_path,
    )
    if current_version_match.group(1) != version:
        updated = _replace_exactly_once(
            updated,
            r"^pkgrel=.*$",
            "pkgrel=1",
            "pkgrel",
            pkgbuild_path,
        )
    updated = _replace_exactly_once(
        updated,
        r"^sha256sums=\('[^']*'\)$",
        f"sha256sums=('{checksum.lower()}')",
        "sha256sums",
        pkgbuild_path,
    )

    if updated == original:
        print(f"Arch package already pins MAKO Renderer {version}.")
    else:
        pkgbuild_path.write_text(updated, encoding="utf-8")
        print(f"Pinned Arch package to MAKO Renderer {version}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
