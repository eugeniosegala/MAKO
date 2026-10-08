#!/usr/bin/env python3
"""Generate Renderer manifests and Decky's runtime list from one build catalogue."""

import argparse
import json
from pathlib import Path
import re
import sys
from typing import TypedDict


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
MATRIX_DIRECTORY = Path("engine/dist/flatpak/mako-render")
CATALOGUE = MATRIX_DIRECTORY / "runtime-versions.json"
TEMPLATE = MATRIX_DIRECTORY / "org.freedesktop.Platform.VulkanLayer.makorender.yml.in"
MANIFEST_PREFIX = "org.freedesktop.Platform.VulkanLayer.makorender_"
GENERATED_HEADER = f"# Generated from {CATALOGUE} and {TEMPLATE}; do not edit.\n"


class FlatpakRuntime(TypedDict):
    version: str
    llvm: int


def load_runtimes(root: Path) -> list[FlatpakRuntime]:
    entries = json.loads((root / CATALOGUE).read_text(encoding="utf-8"))
    if not isinstance(entries, list) or not entries:
        raise ValueError("The Flatpak catalogue must be a nonempty list")
    result: list[FlatpakRuntime] = []
    previous = ""
    for entry in entries:
        if not isinstance(entry, dict) or set(entry) != {"version", "llvm"}:
            raise ValueError("Each Flatpak runtime needs version and llvm")
        version = entry["version"]
        if not isinstance(version, str) or not re.fullmatch(r"[0-9]{2}\.08", version):
            raise ValueError(f"Invalid Freedesktop runtime version: {version!r}")
        if version <= previous:
            raise ValueError("Flatpak runtime versions must be unique and ordered oldest first")
        llvm = entry["llvm"]
        if type(llvm) is not int or llvm <= 0:
            raise ValueError(f"{version} needs a positive LLVM major version")
        result.append(FlatpakRuntime(version=version, llvm=llvm))
        previous = version
    return result


def generated_files(root: Path) -> dict[Path, str]:
    entries = load_runtimes(root)
    template = (root / TEMPLATE).read_text(encoding="utf-8")
    if set(re.findall(r"@[A-Z_]+@", template)) != {
        "@RUNTIME_VERSION@", "@LLVM_VERSION@"
    }:
        raise ValueError("The Flatpak manifest template needs runtime and LLVM placeholders only")
    outputs = {
        MATRIX_DIRECTORY / "runtime-versions.txt": "".join(
            f"{entry['version']}\n" for entry in entries
        ),
        Path("plugin/shared_flatpak_runtimes.py"): "\n".join([
            f'"""Generated from {CATALOGUE}; do not edit."""',
            "# Regenerate: just generate-flatpak-runtimes",
            "",
            "SUPPORTED_FLATPAK_RUNTIME_VERSIONS = (",
            *(f'    "{entry["version"]}",' for entry in entries),
            ")",
            "",
        ]),
    }
    for entry in entries:
        manifest = template.replace("@RUNTIME_VERSION@", entry["version"]).replace(
            "@LLVM_VERSION@", str(entry["llvm"])
        )
        outputs[MATRIX_DIRECTORY / f"{MANIFEST_PREFIX}{entry['version']}.yml"] = (
            GENERATED_HEADER
            + "# Regenerate: just generate-flatpak-runtimes\n"
            + manifest
        )
    return outputs


def synchronize(root: Path, *, check: bool) -> list[Path]:
    outputs = generated_files(root)
    obsolete = [
        path.relative_to(root)
        for path in sorted((root / MATRIX_DIRECTORY).glob(f"{MANIFEST_PREFIX}*.yml"))
        if path.relative_to(root) not in outputs
    ]
    # Remove only files produced by this generator when a runtime is retired.
    # Refuse foreign files before writing anything.
    if not check:
        for relative in obsolete:
            if not (root / relative).read_text(encoding="utf-8").startswith(GENERATED_HEADER):
                raise ValueError(f"Refusing to remove an unmanaged manifest: {relative}")
    stale = list(obsolete)
    for relative, expected in outputs.items():
        path = root / relative
        if not path.is_file() or path.read_text(encoding="utf-8") != expected:
            stale.append(relative)
            if not check:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(expected, encoding="utf-8")
    if not check:
        for relative in obsolete:
            (root / relative).unlink()
    return stale


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Check without writing")
    args = parser.parse_args()
    try:
        stale = synchronize(REPOSITORY_ROOT, check=args.check)
        if args.check and stale:
            print("Flatpak runtime outputs are stale: " + ", ".join(map(str, stale)),
                  file=sys.stderr)
            print("Run: just generate-flatpak-runtimes", file=sys.stderr)
            return 1
    except (OSError, ValueError) as error:
        print(f"Flatpak runtimes: {error}", file=sys.stderr)
        return 1
    print("Flatpak runtime outputs are current.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
