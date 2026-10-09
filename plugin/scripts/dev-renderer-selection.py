#!/usr/bin/env python3
"""Resolve and verify the active native libraries for direct development deploys."""

import argparse
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from py_modules.mako_plugin import constants as paths  # noqa: E402
from py_modules.mako_plugin.layer_manifests import (  # noqa: E402
    manifest_library, manifest_owner,
)


def selected_libraries(home: Path, bits: int) -> tuple[str, tuple[Path, ...]]:
    """Keep the FG-selected owner; require agreement across native launch paths."""
    filename = paths.JSON_FILENAME if bits == 64 else paths.JSON32_FILENAME
    private = home / paths.VULKAN_LAYER_DIR / filename
    registered = home / paths.USER_VULKAN_LAYER_DIR / filename
    decky_dir = home / (paths.LOCAL_LIB if bits == 64 else paths.LOCAL_LIB32)
    standalone_dir = home / ".local" / ("lib" if bits == 64 else "lib32")
    owner = manifest_owner(private, decky_dir / paths.LIB_FILENAME,
                           standalone_dir / paths.LIB_FILENAME)
    if owner is None:
        raise ValueError(f"Unrecognized active {bits}-bit Renderer: {private}")
    library_dir = standalone_dir if owner == paths.ACTIVE_RENDERER_OWNER_STANDALONE else decky_dir
    renderer = library_dir / paths.LIB_FILENAME
    for manifest in (private, registered):
        if manifest_library(manifest, paths.MAKO_LAYER_NAME) != renderer.resolve():
            raise ValueError(f"Native Renderer manifests disagree: {manifest}")
    if not renderer.is_file():
        raise ValueError(f"Selected Renderer is not installed: {renderer}")
    return owner, (
        renderer,
        library_dir / paths.SPATIAL_SCALING_LIB_FILENAME,
        library_dir / "vkbasalt" / paths.VKBASALT_LIB_FILENAME,
    )


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def renderer_manifest(home: Path, bits: int, build_directory: Path) -> tuple[dict[str, object], tuple[Path, Path]]:
    """Retain generated Vulkan metadata, changing only the selected library path."""
    _, libraries = selected_libraries(home, bits)
    filename = paths.JSON_FILENAME if bits == 64 else paths.JSON32_FILENAME
    source = build_directory / "private-manifest" / filename
    manifest = json.loads(source.read_text(encoding="utf-8"))
    layer = manifest["layer"]
    if layer.get("name") != paths.MAKO_LAYER_NAME or layer.get("library_arch") != str(bits):
        raise ValueError(f"Invalid development Renderer manifest identity or architecture: {source}")
    layer["library_path"] = str(libraries[0].resolve())
    return manifest, (home / paths.VULKAN_LAYER_DIR / filename,
                      home / paths.USER_VULKAN_LAYER_DIR / filename)


def prepare_manifest(home: Path, bits: int, build_directory: Path, staged: Path) -> None:
    manifest, destinations = renderer_manifest(home, bits, build_directory)
    staged.write_text(json.dumps(manifest, indent=4) + "\n", encoding="utf-8")
    print(*destinations, sep="\n")


def verify(home: Path, bits: int, built: list[Path]) -> None:
    owner, libraries = selected_libraries(home, bits)
    expected, manifests = renderer_manifest(home, bits, built[0].parent)
    for manifest in manifests:
        if json.loads(manifest.read_text(encoding="utf-8")) != expected:
            raise ValueError(f"Development Renderer manifest differs from the build: {manifest}")
    spatial_name = paths.SPATIAL_SCALING_JSON_FILENAME if bits == 64 else paths.SPATIAL_SCALING_JSON32_FILENAME
    vkbasalt_name = paths.VKBASALT_MANIFEST_FILENAME_64 if bits == 64 else paths.VKBASALT_MANIFEST_FILENAME_32
    for manifest, library, identity in (
        (home / paths.SPATIAL_SCALING_LAYER_DIR / spatial_name,
         libraries[1], paths.SPATIAL_SCALING_LAYER_NAME),
        (home / paths.VKBASALT_LAYER_DIR / vkbasalt_name,
         libraries[2], paths.VKBASALT_LAYER_NAME_64),
    ):
        if manifest_library(manifest, identity) != library.resolve():
            raise ValueError(f"Development manifest selects another library: {manifest}")
    for source, installed in zip(built, libraries, strict=True):
        checksum = sha256(source)
        if sha256(installed) != checksum:
            raise ValueError(f"Active library differs from the development build: {installed}")
        print(f"MAKO Renderer: verified active {bits}-bit {owner} library {installed}; sha256={checksum}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--home", type=Path, default=Path.home())
    parser.add_argument("--bits", type=int, choices=(64, 32), required=True)
    operations = parser.add_mutually_exclusive_group()
    operations.add_argument("--verify", type=Path, nargs=3, metavar=("FG", "SPATIAL", "VKBASALT"))
    operations.add_argument("--prepare-manifest", type=Path, nargs=2, metavar=("BUILD_DIRECTORY", "STAGED_MANIFEST"))
    args = parser.parse_args()
    try:
        if args.verify:
            verify(args.home, args.bits, args.verify)
        elif args.prepare_manifest:
            prepare_manifest(args.home, args.bits, *args.prepare_manifest)
        else:
            owner, libraries = selected_libraries(args.home, args.bits)
            print(*libraries, sep="\n")
            print(f"MAKO Renderer: deploying to the active {args.bits}-bit {owner} installation.", file=sys.stderr)
    except (OSError, ValueError, TypeError, KeyError, AttributeError) as error:
        parser.exit(1, f"MAKO Renderer: development selection failed: {error}\n")


if __name__ == "__main__":
    main()
