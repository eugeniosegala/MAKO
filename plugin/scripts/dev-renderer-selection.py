#!/usr/bin/env python3
"""Resolve and verify the active native libraries for direct development deploys."""

import argparse
import json
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from py_modules.mako_plugin import constants as paths  # noqa: E402
from py_modules.mako_plugin.layer_manifests import (  # noqa: E402
    manifest_library, manifest_owner,
)
from py_modules.mako_plugin.development_renderer import (  # noqa: E402
    file_sha256 as sha256, record_development_files,
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


def selected_tools(home: Path) -> tuple[str, tuple[Path, ...]]:
    """Host tools follow the 64-bit owner used by Decky's runtime services."""
    owner, _libraries = selected_libraries(home, 64)
    directory = (home / paths.STANDALONE_INSTALL_PREFIX / "bin"
                 if owner == paths.ACTIVE_RENDERER_OWNER_STANDALONE
                 else home / paths.CLI_DIR)
    return owner, tuple(directory / name for name in (
        paths.CLI_FILENAME, paths.SHADER_TOOL_FILENAME, paths.VRR_LEASE_FILENAME,
    ))


def verify_tools(home: Path, built: list[Path]) -> None:
    owner, installed_tools = selected_tools(home)
    for index, (source, installed) in enumerate(zip(built, installed_tools, strict=True)):
        if index == 1 and not source.is_file():
            if installed.exists():
                raise ValueError(f"Stale shader tool remains selected: {installed}")
            continue
        if not installed.is_file() or not os.access(installed, os.X_OK):
            raise ValueError(f"Selected native tool is not executable: {installed}")
        checksum = sha256(source)
        if sha256(installed) != checksum:
            raise ValueError(f"Selected native tool differs from the development build: {installed}")
        label = "CLI" if index == 0 else installed.name
        print(f"MAKO Renderer: verified installed 64-bit {label} {installed}; owner={owner}; sha256={checksum}")


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
    operations.add_argument("--tool-paths", action="store_true")
    operations.add_argument("--verify-tools", type=Path, nargs=3, metavar=("CLI", "SHADER", "VRR"))
    operations.add_argument("--record-deployment", type=Path, metavar="PLUGIN_DIRECTORY")
    parser.add_argument("--copied-files", type=Path, nargs="+", metavar="PATH")
    args = parser.parse_args()
    if bool(args.record_deployment) != bool(args.copied_files) or (
            args.copied_files and len(args.copied_files) % 2):
        parser.error("--record-deployment requires source/destination pairs in --copied-files")
    try:
        if args.record_deployment:
            record_development_files(
                args.record_deployment,
                args.home / paths.MAKO_ROOT / paths.ACTIVE_RENDERER_STATE_FILENAME,
                list(zip(args.copied_files[::2], args.copied_files[1::2], strict=True)))
            print("MAKO Renderer: recorded verified development files for installation checks.")
        elif args.tool_paths or args.verify_tools:
            if args.bits != 64:
                raise ValueError("Native host tools follow the selected 64-bit Renderer")
            if args.verify_tools:
                verify_tools(args.home, args.verify_tools)
            else:
                _owner, tools = selected_tools(args.home)
                print(*tools, sep="\n")
        elif args.verify:
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
