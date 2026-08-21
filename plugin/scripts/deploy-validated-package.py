#!/usr/bin/env python3
"""Deploy one already-validated local MAKO Decky ZIP without rebuilding it."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import stat
import sys
import tempfile
import zipfile
from pathlib import Path, PurePosixPath
from typing import Any


PACKAGE_ROOT = "Mako"
MANAGED_DIRECTORIES = ("bin", "dist", "py_modules")
SUPPORTED_PLUGIN_NAMES = {
    "MAKO Decky",
    "MAKO",
    "Mako",
    "MAKO - Frame Generation",
}
MAX_ARCHIVE_FILES = 20_000
MAX_UNCOMPRESSED_SIZE = 4 * 1024 * 1024 * 1024


class DeploymentError(RuntimeError):
    """Raised when a package cannot be safely deployed."""


def _arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Synchronize an already-validated local MAKO Decky ZIP into an "
            "existing dedicated test installation without rebuilding it."
        )
    )
    parser.add_argument("archive", type=Path, help="MAKO Decky ZIP to deploy")
    parser.add_argument(
        "--plugin-dir",
        type=Path,
        default=Path(
            os.environ.get(
                "DECKY_PLUGIN_DIR",
                str(Path.home() / "homebrew" / "plugins" / PACKAGE_ROOT),
            )
        ),
        help="existing MAKO Decky installation",
    )
    return parser.parse_args()


def _read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise DeploymentError(f"Could not read JSON file {path}: {error}") from error
    if not isinstance(value, dict):
        raise DeploymentError(f"Expected a JSON object in {path}")
    return value


def _plugin_name(plugin_root: Path) -> str:
    name = _read_json(plugin_root / "plugin.json").get("name")
    if not isinstance(name, str) or name not in SUPPORTED_PLUGIN_NAMES:
        raise DeploymentError(f"Refusing to modify a different Decky plugin: {plugin_root}")
    return name


def _safe_extract(archive: Path, destination: Path) -> Path:
    try:
        with zipfile.ZipFile(archive) as package:
            members = package.infolist()
            if not members or len(members) > MAX_ARCHIVE_FILES:
                raise DeploymentError("Decky package has an invalid file count")
            total_size = 0
            seen_paths: set[PurePosixPath] = set()
            for member in members:
                member_path = PurePosixPath(member.filename)
                if (
                    member_path.is_absolute()
                    or ".." in member_path.parts
                    or not member_path.parts
                    or member_path.parts[0] != PACKAGE_ROOT
                ):
                    raise DeploymentError(
                        f"Decky package contains an unsafe path: {member.filename}"
                    )
                if member_path in seen_paths:
                    raise DeploymentError(
                        f"Decky package contains a duplicate path: {member.filename}"
                    )
                seen_paths.add(member_path)
                file_type = stat.S_IFMT(member.external_attr >> 16)
                if file_type == stat.S_IFLNK:
                    raise DeploymentError(
                        f"Decky package contains an unsupported symlink: {member.filename}"
                    )
                if file_type not in (0, stat.S_IFREG, stat.S_IFDIR):
                    raise DeploymentError(
                        f"Decky package contains an unsupported file type: {member.filename}"
                    )
                total_size += member.file_size
                if total_size > MAX_UNCOMPRESSED_SIZE:
                    raise DeploymentError("Decky package expands beyond the safety limit")

            package.extractall(destination)
            for member in members:
                extracted = destination.joinpath(*PurePosixPath(member.filename).parts)
                mode = (member.external_attr >> 16) & 0o777
                if mode and extracted.exists():
                    extracted.chmod(mode)
    except (OSError, zipfile.BadZipFile) as error:
        raise DeploymentError(f"Could not extract MAKO Decky package: {error}") from error

    package_root = destination / PACKAGE_ROOT
    if not package_root.is_dir():
        raise DeploymentError(f"Decky package is missing its {PACKAGE_ROOT}/ root")
    return package_root


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _validate_renderer_payload(package_root: Path) -> None:
    manifest = _read_json(package_root / "package.json")
    metadata = manifest.get("bundled_renderer")
    if not isinstance(metadata, dict) or "remote_binary" in manifest:
        raise DeploymentError(
            "Hardware deployment requires a self-contained local package with bundled_renderer"
        )

    archive_name = metadata.get("name")
    checksum = metadata.get("sha256hash")
    architectures = metadata.get("architectures", ["64", "32"])
    if (
        not isinstance(archive_name, str)
        or Path(archive_name).name != archive_name
        or not isinstance(checksum, str)
        or len(checksum) != 64
        or not isinstance(architectures, list)
        or "64" not in architectures
        or any(value not in ("64", "32") for value in architectures)
    ):
        raise DeploymentError("bundled_renderer metadata is invalid")

    archive = package_root / "bin" / archive_name
    if not archive.is_file():
        raise DeploymentError(f"Bundled MAKO Renderer archive is missing: {archive}")
    actual_checksum = _sha256(archive)
    if actual_checksum.lower() != checksum.lower():
        raise DeploymentError(
            "Bundled MAKO Renderer checksum mismatch: "
            f"expected {checksum.lower()}, got {actual_checksum}"
        )


def _destination_writable(destination: Path) -> bool:
    if destination.exists() and destination.is_file():
        return os.access(destination, os.W_OK) or os.access(destination.parent, os.W_OK)
    return os.access(destination.parent, os.W_OK)


def _preflight_plugin_sync(package_root: Path, plugin_root: Path) -> None:
    if not plugin_root.is_dir():
        raise DeploymentError(
            f"Existing MAKO Decky installation not found: {plugin_root}"
        )
    _plugin_name(plugin_root)
    _plugin_name(package_root)

    for directory_name in MANAGED_DIRECTORIES:
        source = package_root / directory_name
        destination = plugin_root / directory_name
        if not source.is_dir():
            raise DeploymentError(f"Decky package is missing {directory_name}/")
        if destination.exists():
            if not destination.is_dir() or not os.access(destination, os.W_OK):
                raise DeploymentError(f"Installed package directory is not writable: {destination}")
        elif not os.access(plugin_root, os.W_OK):
            raise DeploymentError(f"Cannot create installed package directory: {destination}")

    for source in package_root.iterdir():
        if source.is_file():
            destination = plugin_root / source.name
            if destination.exists() and source.read_bytes() == destination.read_bytes():
                continue
            if not _destination_writable(destination):
                raise DeploymentError(
                    f"Installed package file cannot be updated safely: {destination}"
                )


def _copy_file_atomically(source: Path, destination: Path, mode: int | None = None) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists() and source.read_bytes() == destination.read_bytes():
        return
    selected_mode = mode if mode is not None else stat.S_IMODE(source.stat().st_mode)
    if os.access(destination.parent, os.W_OK):
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{destination.name}.", dir=destination.parent
        )
        temporary = Path(temporary_name)
        try:
            with os.fdopen(descriptor, "wb") as output, source.open("rb") as input_file:
                shutil.copyfileobj(input_file, output)
                output.flush()
                os.fsync(output.fileno())
            temporary.chmod(selected_mode)
            temporary.replace(destination)
        except Exception:
            temporary.unlink(missing_ok=True)
            raise
    elif destination.is_file() and os.access(destination, os.W_OK):
        shutil.copyfile(source, destination)
        destination.chmod(selected_mode)
    else:
        raise DeploymentError(f"Cannot update installed package file: {destination}")


def _clear_directory(directory: Path) -> None:
    for child in directory.iterdir():
        if child.is_dir() and not child.is_symlink():
            shutil.rmtree(child)
        else:
            child.unlink()


def _sync_plugin(package_root: Path, plugin_root: Path) -> None:
    for directory_name in MANAGED_DIRECTORIES:
        source_directory = package_root / directory_name
        destination_directory = plugin_root / directory_name
        destination_directory.mkdir(exist_ok=True)
        _clear_directory(destination_directory)
        shutil.copytree(
            source_directory,
            destination_directory,
            dirs_exist_ok=True,
            copy_function=shutil.copy2,
        )

    for source in package_root.iterdir():
        if source.is_file():
            _copy_file_atomically(source, plugin_root / source.name)


def deploy(archive: Path, plugin_root: Path) -> None:
    archive = archive.expanduser().resolve()
    plugin_root = plugin_root.expanduser().resolve()
    if not archive.is_file():
        raise DeploymentError(f"MAKO Decky archive not found: {archive}")

    work_parent = Path(
        os.environ.get(
            "MAKO_BUILD_WORK_ROOT",
            str(archive.parent),
        )
    ).expanduser()
    work_parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mako-package-deploy.", dir=work_parent) as temporary:
        staging_root = Path(temporary)
        package_root = _safe_extract(archive, staging_root)
        _preflight_plugin_sync(package_root, plugin_root)
        _validate_renderer_payload(package_root)
        _sync_plugin(package_root, plugin_root)

    print(f"Deployed verified MAKO Decky package: {archive}")
    print("Reload MAKO Decky and run its installer to activate the bundled Renderer.")


def main() -> int:
    arguments = _arguments()
    try:
        deploy(arguments.archive, arguments.plugin_dir)
    except (DeploymentError, OSError, shutil.Error) as error:
        print(f"Hardware package deployment failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
