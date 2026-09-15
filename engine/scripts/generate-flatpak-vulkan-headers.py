#!/usr/bin/env python3
"""Generate Flatpak's shared build-only module from the Renderer header pin."""

import argparse
import json
from pathlib import Path
import re
import sys


ENGINE_ROOT = Path(__file__).resolve().parents[1]


def module_text(revision: str) -> str:
    if not re.fullmatch(r"(?:vulkan-sdk-|v)\d+\.\d+\.\d+", revision):
        raise ValueError("Expected a Vulkan-Headers release tag or SDK branch")
    source = {
        "type": "git",
        "url": "https://github.com/KhronosGroup/Vulkan-Headers.git",
        "tag" if revision.startswith("v") and not revision.startswith("vulkan-sdk-") else "branch": revision,
    }
    module = {
        "name": "mako-vulkan-headers",
        "buildsystem": "simple",
        "build-commands": [
            'install -d "${FLATPAK_DEST}/include"',
            'cp -a include/vulkan include/vk_video "${FLATPAK_DEST}/include/"',
            f'printf "MAKO Renderer: Vulkan-Headers revision={revision} commit=%s\\n" "$(git rev-parse HEAD)"',
        ],
        "cleanup": ["/include/vulkan", "/include/vk_video"],
        "sources": [source],
    }
    return json.dumps(module, indent=2) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Check freshness without writing files")
    parser.add_argument("--revision-file", type=Path, default=ENGINE_ROOT / "vulkan-headers-revision.txt")
    parser.add_argument("--output", type=Path, default=ENGINE_ROOT / "dist/flatpak/mako-render/vulkan-headers.json")
    args = parser.parse_args()
    try:
        expected = module_text(args.revision_file.read_text(encoding="utf-8").strip())
        if args.check:
            if not args.output.is_file() or args.output.read_text(encoding="utf-8") != expected:
                print("Flatpak Vulkan-Headers module is stale; run engine/scripts/generate-flatpak-vulkan-headers.py", file=sys.stderr)
                return 1
            print("Flatpak Vulkan-Headers module matches the shared Renderer pin.")
        else:
            args.output.write_text(expected, encoding="utf-8")
            print(f"Generated {args.output}")
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
