#!/usr/bin/env python3
"""Validate, stage, and generate build metadata for MAKO's vkBasalt pin."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import tarfile
import tempfile
from typing import Any
from urllib.request import urlopen


ENGINE_ROOT = Path(__file__).resolve().parent.parent

REPOSITORY = "eugeniosegala/vkBasalt"
TAG_PATTERN = re.compile(r"mako-v\d+\.\d+\.\d+\.\d+-\d+")
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}")
SOURCE_PATHS = {
    "lib64": "lib/libvkbasalt.so",
    "lib32": "lib32/libvkbasalt.so",
    "manifest64": "share/vulkan/implicit_layer.d/vkBasalt.x86_64.json",
    "manifest32": "share/vulkan/implicit_layer.d/vkBasalt.x86.json",
    "license": "share/doc/vkbasalt/LICENSE",
    "reshade_license": "share/doc/vkbasalt/RESHade-LICENSE.md",
    "source": "share/doc/vkbasalt/SOURCE",
    "checksums": "SHA256SUMS",
}
LAYER_NAME = "VK_LAYER_VKBASALT_post_processing"
ENABLE_ENVIRONMENT = {"ENABLE_VKBASALT": "1"}
DISABLE_ENVIRONMENT = {"DISABLE_VKBASALT": "1"}
LIVE_RELOAD_MARKERS = (
    b"VKBASALT_CONFIG_RELOAD",
    b"makoVibrance",
    b"makoCurves",
    b"makoDeband",
    b"makoTechnicolor",
    b"makoSepia",
    b"makoMonochrome",
    b"makoVignette",
    b"makoHDRLook",
    b"makoColourfulness",
    b"makoTechnicolor2",
    b"makoDPX",
    b"makoBleachBypass",
    b"makoNoir",
    b"makoFilmGrain",
    b"makoCartoon",
    b"makoNostalgia",
    b"makoChromaticAberration",
    b"makoClarity",
    b"makoLevelsPlus",
)


def _read_pin(path: Path) -> dict[str, Any]:
    try:
        pin = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"could not read vkBasalt pin {path}: {error}") from error
    return _validate_pin(pin)


def _validate_pin(pin: Any) -> dict[str, Any]:
    if not isinstance(pin, dict) or pin.get("schema_version") != 1:
        raise ValueError("vkBasalt pin must use schema_version 1")
    tag = pin.get("tag")
    asset = pin.get("asset")
    expected_asset = f"vkBasalt-{tag}-linux-x86.tar.xz"
    expected_url = (
        f"https://github.com/{REPOSITORY}/releases/download/{tag}/{asset}"
    )
    if pin.get("repository") != REPOSITORY:
        raise ValueError(f"vkBasalt repository must be {REPOSITORY}")
    if not isinstance(tag, str) or TAG_PATTERN.fullmatch(tag) is None:
        raise ValueError("vkBasalt tag must use mako-vX.Y.Z.UPSTREAM-REVISION")
    if asset != expected_asset:
        raise ValueError(f"vkBasalt asset must be {expected_asset}")
    if pin.get("url") != expected_url:
        raise ValueError(f"vkBasalt URL must be {expected_url}")
    for field in ("source_commit", "upstream_commit"):
        value = pin.get(field)
        if not isinstance(value, str) or COMMIT_PATTERN.fullmatch(value) is None:
            raise ValueError(f"vkBasalt {field} must be a lowercase Git commit")
    checksum = pin.get("sha256")
    if not isinstance(checksum, str) or SHA256_PATTERN.fullmatch(checksum) is None:
        raise ValueError("vkBasalt sha256 must be a lowercase SHA-256 value")
    revision = pin.get("vulkan_headers_revision")
    expected_revision = (ENGINE_ROOT / "vulkan-headers-revision.txt").read_text().strip()
    if revision != expected_revision:
        raise ValueError("vkBasalt Vulkan header pin must match MAKO Renderer: " + expected_revision)
    header_commit = pin.get("vulkan_headers_commit")
    if not isinstance(header_commit, str) or COMMIT_PATTERN.fullmatch(header_commit) is None:
        raise ValueError("vkBasalt Vulkan headers must record their resolved commit")
    api_version = pin.get("vulkan_api_version")
    for name in ("VkLayer_MAKO_render.json.in", "VkLayer_MAKO_spatial_scaling.json.in"):
        renderer_api = json.loads((ENGINE_ROOT / "mako-render" / name).read_text())["layer"]["api_version"]
        if api_version != renderer_api:
            raise ValueError("vkBasalt API declaration must match the reviewed MAKO Renderer API: " + renderer_api)
    return pin


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _obtain_archive(pin: dict[str, Any], cache_dir: Path) -> Path:
    cache_dir.mkdir(parents=True, exist_ok=True)
    archive = cache_dir / pin["asset"]
    if archive.is_file() and _sha256(archive) == pin["sha256"]:
        return archive
    archive.unlink(missing_ok=True)
    partial = archive.with_suffix(archive.suffix + ".part")
    partial.unlink(missing_ok=True)
    try:
        with urlopen(pin["url"], timeout=120) as response, partial.open("wb") as output:
            shutil.copyfileobj(response, output)
        actual = _sha256(partial)
        if actual != pin["sha256"]:
            raise ValueError(
                f"vkBasalt archive checksum mismatch: expected {pin['sha256']}, got {actual}"
            )
        os.replace(partial, archive)
    finally:
        partial.unlink(missing_ok=True)
    return archive


def _archive_members(archive_path: Path) -> dict[str, bytes]:
    members: dict[str, bytes] = {}
    with tarfile.open(archive_path, "r:xz") as archive:
        for member in archive.getmembers():
            if not member.isfile():
                continue
            normalized = str(PurePosixPath(member.name.removeprefix("./")))
            if normalized not in SOURCE_PATHS.values():
                continue
            source = archive.extractfile(member)
            if source is None:
                raise ValueError(f"could not read vkBasalt archive member {normalized}")
            members[normalized] = source.read()
    missing = sorted(set(SOURCE_PATHS.values()) - set(members))
    if missing:
        raise ValueError("vkBasalt archive is missing: " + ", ".join(missing))
    return members


def _validate_live_reload_markers(key: str, binary: bytes) -> None:
    missing_live_markers = [
        marker.decode("ascii")
        for marker in LIVE_RELOAD_MARKERS
        if marker not in binary
    ]
    if missing_live_markers:
        raise ValueError(
            f"vkBasalt {key} library is missing MAKO live reload markers: "
            + ", ".join(missing_live_markers)
        )


def _validate_archive(pin: dict[str, Any], archive_path: Path) -> dict[str, bytes]:
    actual = _sha256(archive_path)
    if actual != pin["sha256"]:
        raise ValueError(
            f"vkBasalt archive checksum mismatch: expected {pin['sha256']}, got {actual}"
        )
    members = _archive_members(archive_path)
    checksum_lines = members[SOURCE_PATHS["checksums"]].decode("utf-8").splitlines()
    checksums: dict[str, str] = {}
    for line in checksum_lines:
        checksum, separator, name = line.partition("  ")
        if separator and SHA256_PATTERN.fullmatch(checksum):
            checksums[str(PurePosixPath(name.removeprefix("./")))] = checksum
    for key, member_path in SOURCE_PATHS.items():
        if key == "checksums":
            continue
        expected = checksums.get(member_path)
        actual_member = hashlib.sha256(members[member_path]).hexdigest()
        if expected != actual_member:
            raise ValueError(f"vkBasalt internal checksum failed for {member_path}")
    for key, elf_class in (("lib64", 2), ("lib32", 1)):
        binary = members[SOURCE_PATHS[key]]
        if len(binary) < 5 or binary[:4] != b"\x7fELF" or binary[4] != elf_class:
            raise ValueError(f"vkBasalt {key} library has the wrong ELF class")
        if (
            b"vkBasalt_GetInstanceProcAddr" not in binary
            or b"vkBasalt_GetDeviceProcAddr" not in binary
        ):
            raise ValueError(f"vkBasalt {key} library is missing its layer entrypoints")
        _validate_live_reload_markers(key, binary)
    for key, architecture in (("manifest64", "64"), ("manifest32", "32")):
        manifest = json.loads(members[SOURCE_PATHS[key]])
        layer = manifest.get("layer", {})
        if (
            layer.get("name") != LAYER_NAME
            or layer.get("type") != "GLOBAL"
            or layer.get("api_version") != pin["vulkan_api_version"]
            or layer.get("library_arch") != architecture
            or layer.get("enable_environment") != ENABLE_ENVIRONMENT
            or layer.get("disable_environment") != DISABLE_ENVIRONMENT
        ):
            raise ValueError(f"vkBasalt {key} manifest contract is invalid")
    source_lines = dict(
        line.split("=", 1)
        for line in members[SOURCE_PATHS["source"]].decode("utf-8").splitlines()
        if "=" in line
    )
    if (
        source_lines.get("repository") != f"https://github.com/{REPOSITORY}"
        or source_lines.get("tag") != pin["tag"]
        or source_lines.get("commit") != pin["source_commit"]
        or source_lines.get("upstream_commit") != pin["upstream_commit"]
    ):
        raise ValueError("vkBasalt archive source provenance does not match its pin")
    for field in ("vulkan_headers_revision", "vulkan_headers_commit", "vulkan_api_version"):
        if source_lines.get(field) != pin[field]:
            raise ValueError("vkBasalt archive Vulkan provenance does not match its pin: " + field)
    return members


def _pin_from_archive(archive: Path) -> dict[str, Any]:
    members = _archive_members(archive)
    source = dict(line.split("=", 1) for line in members[SOURCE_PATHS["source"]].decode("utf-8").splitlines() if "=" in line)
    tag = source.get("tag")
    asset = f"vkBasalt-{tag}-linux-x86.tar.xz"
    pin = _validate_pin({
        "schema_version": 1,
        "repository": REPOSITORY,
        "tag": tag,
        "source_commit": source.get("commit"),
        "upstream_commit": source.get("upstream_commit"),
        "asset": asset,
        "url": f"https://github.com/{REPOSITORY}/releases/download/{tag}/{asset}",
        "sha256": _sha256(archive),
        "vulkan_headers_revision": source.get("vulkan_headers_revision"),
        "vulkan_headers_commit": source.get("vulkan_headers_commit"),
        "vulkan_api_version": source.get("vulkan_api_version"),
    })
    _validate_archive(pin, archive)
    return pin


def _manifest(source: bytes, architecture: str, library_path: str) -> bytes:
    manifest = json.loads(source)
    layer = manifest["layer"]
    layer["library_arch"] = architecture
    layer["library_path"] = library_path
    return (json.dumps(manifest, indent=2) + "\n").encode("utf-8")


def _write(path: Path, content: bytes, mode: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as temporary:
        temporary.write(content)
        temporary_path = Path(temporary.name)
    os.chmod(temporary_path, mode)
    os.replace(temporary_path, path)


def _stage_native(
    pin: dict[str, Any], members: dict[str, bytes], target: Path, include_32: bool
) -> None:
    files: list[tuple[Path, bytes, int]] = [
        (target / "lib/vkbasalt/libvkbasalt.so", members[SOURCE_PATHS["lib64"]], 0o755),
        (
            target / "share/mako-render/vulkan/vkbasalt.d/vkBasalt.json",
            _manifest(
                members[SOURCE_PATHS["manifest64"]],
                "64",
                "../../../../lib/vkbasalt/libvkbasalt.so",
            ),
            0o644,
        ),
        (
            target / "share/doc/mako-render/vkbasalt/LICENSE",
            members[SOURCE_PATHS["license"]],
            0o644,
        ),
        (
            target / "share/doc/mako-render/vkbasalt/RESHade-LICENSE.md",
            members[SOURCE_PATHS["reshade_license"]],
            0o644,
        ),
        (
            target / "share/doc/mako-render/vkbasalt/SOURCE",
            members[SOURCE_PATHS["source"]],
            0o644,
        ),
        (
            target / "share/doc/mako-render/vkbasalt/MAKO-PIN.json",
            (json.dumps(pin, indent=2) + "\n").encode("utf-8"),
            0o644,
        ),
    ]
    if include_32:
        files.extend([
            (
                target / "lib32/vkbasalt/libvkbasalt.so",
                members[SOURCE_PATHS["lib32"]],
                0o755,
            ),
            (
                target / "share/mako-render/vulkan/vkbasalt.d/vkBasalt.x86.json",
                _manifest(
                    members[SOURCE_PATHS["manifest32"]],
                    "32",
                    "../../../../lib32/vkbasalt/libvkbasalt.so",
                ),
                0o644,
            ),
        ])
    for path, content, mode in files:
        _write(path, content, mode)


def _flatpak_manifest(pin: dict[str, Any], architecture: str) -> str:
    library = "lib64" if architecture == "64" else "lib/i386-linux-gnu"
    manifest = {
        "file_format_version": "1.0.0",
        "layer": {
            "name": LAYER_NAME,
            "type": "GLOBAL",
            "library_path": f"/usr/lib/extensions/vulkan/makorender/{library}/vkbasalt/libvkbasalt.so",
            "library_arch": architecture,
            "api_version": pin["vulkan_api_version"],
            "implementation_version": "1",
            "description": "vkBasalt post processing layer (MAKO build)",
            "functions": {
                "vkGetInstanceProcAddr": "vkBasalt_GetInstanceProcAddr",
                "vkGetDeviceProcAddr": "vkBasalt_GetDeviceProcAddr",
            },
            "enable_environment": ENABLE_ENVIRONMENT,
            "disable_environment": DISABLE_ENVIRONMENT,
        },
    }
    return json.dumps(manifest, indent=2) + "\n"


def _flatpak_module(pin: dict[str, Any]) -> str:
    module = {
        "name": "vkbasalt-mako-bundle",
        "buildsystem": "simple",
        "build-commands": [
            "install -Dm755 lib/libvkbasalt.so $FLATPAK_DEST/lib64/vkbasalt/libvkbasalt.so",
            "install -Dm755 lib32/libvkbasalt.so $FLATPAK_DEST/lib/i386-linux-gnu/vkbasalt/libvkbasalt.so",
            "install -Dm644 vkBasalt.flatpak.json $FLATPAK_DEST/share/vulkan/implicit_layer.d/vkBasalt.json",
            "install -Dm644 vkBasalt.flatpak.x86.json $FLATPAK_DEST/share/vulkan/implicit_layer.d/vkBasalt.x86.json",
            "install -Dm644 share/doc/vkbasalt/LICENSE $FLATPAK_DEST/share/doc/mako-render/vkbasalt/LICENSE",
            "install -Dm644 share/doc/vkbasalt/RESHade-LICENSE.md $FLATPAK_DEST/share/doc/mako-render/vkbasalt/RESHade-LICENSE.md",
            "install -Dm644 share/doc/vkbasalt/SOURCE $FLATPAK_DEST/share/doc/mako-render/vkbasalt/SOURCE",
            "install -Dm644 vkbasalt-release.json $FLATPAK_DEST/share/doc/mako-render/vkbasalt/MAKO-PIN.json",
        ],
        "sources": [
            {"type": "archive", "url": pin["url"], "sha256": pin["sha256"]},
            {"type": "file", "path": "vkBasalt.flatpak.json"},
            {"type": "file", "path": "vkBasalt.flatpak.x86.json"},
            {"type": "file", "path": "../../../vkbasalt-release.json"},
        ],
    }
    return json.dumps(module, indent=2) + "\n"


def _local_candidate(archive: Path) -> tuple[dict[str, Any], dict[str, bytes]]:
    """Verify a developer archive without changing or impersonating a release pin."""
    pin = _pin_from_archive(archive)
    members = _validate_archive(pin, archive)
    # This candidate workflow accompanies the HDR handoff. Check both native
    # declarations through the compiled payload before packaging either arch.
    for key in ("lib64", "lib32"):
        if b"makoSetSwapchainColorSpaceV1" not in members[SOURCE_PATHS[key]]:
            raise ValueError(f"local vkBasalt {key} lacks the HDR colour-space handoff")
    pin["build_kind"] = "local-candidate"
    pin["url"] = None
    return pin, members


def _prepare_local_flatpak(archive: Path, target: Path) -> None:
    pin, _ = _local_candidate(archive)
    target.mkdir(parents=True, exist_ok=True)
    source = ENGINE_ROOT / "dist/flatpak/mako-render"
    module = json.loads(_flatpak_module(pin))
    module["sources"][0] = {
        "type": "archive", "path": str(archive.resolve()), "sha256": pin["sha256"],
    }
    module["sources"][-1]["path"] = "vkbasalt-release.json"
    _write(target / "vkbasalt-module.json", (json.dumps(module, indent=2) + "\n").encode(), 0o644)
    _write(target / "vkbasalt-release.json", (json.dumps(pin, indent=2) + "\n").encode(), 0o644)
    for name, architecture in (("vkBasalt.flatpak.json", "64"), ("vkBasalt.flatpak.x86.json", "32")):
        _write(target / name, _flatpak_manifest(pin, architecture).encode(), 0o644)
    shutil.copyfile(source / "vulkan-headers.json", target / "vulkan-headers.json")
    # Keep the generated runtime manifests authoritative. Relocate only their
    # checkout source path in this ignored build directory, never tracked files.
    for manifest in source.glob("org.freedesktop.Platform.VulkanLayer.makorender_*.yml"):
        text = manifest.read_text()
        if text.count("path: ../../../..") != 2:
            raise ValueError("unexpected Flatpak source layout: " + str(manifest))
        text = text.replace("path: ../../../..", "path: " + json.dumps(str(ENGINE_ROOT.parent)))
        _write(target / manifest.name, text.encode(), 0o644)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--pin",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "vkbasalt-release.json",
    )
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--update-from-archive", type=Path, help="Validate a downloaded public archive, then replace the dependency pin")
    parser.add_argument("--stage-native", type=Path)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--local-archive", type=Path, help="Verified developer payload for staging only; never updates release pins")
    parser.add_argument("--prepare-local-flatpak", type=Path, help="Create ignored Flatpak manifests using --local-archive")
    parser.add_argument("--cache-dir", type=Path)
    parser.add_argument("--64-bit-only", action="store_true")
    parser.add_argument("--generate-flatpak-module", type=Path)
    parser.add_argument("--check-flatpak-module", type=Path)
    args = parser.parse_args()

    if args.local_archive and (args.update_from_archive or args.generate_flatpak_module or args.check_flatpak_module):
        parser.error("local candidates cannot update or replace release metadata")
    if args.prepare_local_flatpak:
        if not args.local_archive:
            parser.error("--prepare-local-flatpak requires --local-archive")
        _prepare_local_flatpak(args.local_archive, args.prepare_local_flatpak)
        print("Prepared local vkBasalt candidate Flatpak manifests.")
        return 0

    if args.update_from_archive:
        pin = _pin_from_archive(args.update_from_archive)
        _write(args.pin, (json.dumps(pin, indent=2) + "\n").encode("utf-8"), 0o644)
    else:
        pin = _read_pin(args.pin)
    if args.generate_flatpak_module:
        _write(
            args.generate_flatpak_module,
            _flatpak_module(pin).encode("utf-8"),
            0o644,
        )
        for name, architecture in (("vkBasalt.flatpak.json", "64"), ("vkBasalt.flatpak.x86.json", "32")):
            _write(args.generate_flatpak_module.parent / name,
                   _flatpak_manifest(pin, architecture).encode("utf-8"), 0o644)
    if args.check_flatpak_module:
        expected = _flatpak_module(pin)
        actual = args.check_flatpak_module.read_text(encoding="utf-8")
        if actual != expected:
            raise ValueError(
                f"generated Flatpak vkBasalt module is stale: {args.check_flatpak_module}"
            )
        for name, architecture in (("vkBasalt.flatpak.json", "64"), ("vkBasalt.flatpak.x86.json", "32")):
            manifest = args.check_flatpak_module.parent / name
            if manifest.read_text(encoding="utf-8") != _flatpak_manifest(pin, architecture):
                raise ValueError(f"generated Flatpak vkBasalt manifest is stale: {manifest}")
    if args.stage_native:
        cache_dir = args.cache_dir or args.pin.parent / "build/cache/vkbasalt"
        if args.local_archive:
            pin, members = _local_candidate(args.local_archive)
        else:
            archive = args.archive or _obtain_archive(pin, cache_dir)
            members = _validate_archive(pin, archive)
        _stage_native(
            pin,
            members,
            args.stage_native,
            not getattr(args, "64_bit_only"),
        )
    if not any((
        args.check,
        args.update_from_archive,
        args.stage_native,
        args.generate_flatpak_module,
        args.check_flatpak_module,
    )):
        parser.error("select --check, --stage-native, or a Flatpak module operation")
    kind = "local candidate" if args.local_archive else "pin"
    print(f"vkBasalt {kind} is current: {pin['tag']} ({pin['sha256']})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
