#!/usr/bin/env python3
"""Verify that Renderer libraries stay separate from MAKO FG and sit beside its active release; bind archives to it."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys
from typing import TypedDict

ENGINE_ROOT = Path(__file__).resolve().parent.parent
# MAKO FG's release command mirrors every open-weight release here; versions.json names the active one.
RELEASES = ENGINE_ROOT / "models/mako-fg"
OPEN_FORMAT = 2
MODEL = "model"
PROGRAM = "mako-fg-host"
PROGRAM_PATH = f"runtime/x64/bin/{PROGRAM}"


class PackageError(Exception):
    pass


class ArtifactReceipt(TypedDict):
    schema_version: int
    release_version: str
    model_sha256: str
    artifact_sha256: str


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def release_models(releases: Path) -> tuple[tuple[str, dict[str, bytes]], list[tuple[str, dict[str, bytes]]]]:
    """The active release's model files and programs, which packages install, and every other listed release's."""
    index = json.loads((releases / "versions.json").read_text(encoding="utf-8"))
    loaded = []
    for entry in index.get("versions", []):
        folder = releases / entry["folder"]
        if folder.parent != releases or folder.name in {"", ".", ".."} or folder.is_symlink():
            raise PackageError(f"Unsafe MAKO FG release folder: {folder}")
        manifest = json.loads((folder / "MANIFEST.json").read_text(encoding="utf-8"))
        if manifest.get("format") != OPEN_FORMAT:
            raise PackageError(f"MAKO FG release {entry['version']} is not an open-weight release")
        files = {}
        for item in manifest["files"]:
            if item["name"].startswith(f"{MODEL}/") or item["name"].endswith(f"/bin/{PROGRAM}"):
                data = (folder / item["name"]).read_bytes()
                if hashlib.sha256(data).hexdigest() != item["sha256"]:
                    raise PackageError(f"MAKO FG release {entry['version']}: {item['name']} differs from its MANIFEST.json")
                files[item["name"]] = data
        if f"{MODEL}/weights.bin" not in files:
            raise PackageError(f"MAKO FG release {entry['version']} has no model weights")
        loaded.append((entry["version"], files))
    active = [release for release in loaded if release[0] == index.get("active")]
    if len(active) != 1:
        raise PackageError(f"{releases / 'versions.json'} has no active MAKO FG release")
    return active[0], [release for release in loaded if release[0] != index.get("active")]


def model_digest(files: dict[str, bytes]) -> str:
    lines = [f"{name} {hashlib.sha256(data).hexdigest()}" for name, data in sorted(files.items())]
    return hashlib.sha256("\n".join(lines).encode()).hexdigest()


def program_directory(library: Path) -> Path:
    """Where a Renderer library finds the one 64-bit program, matching hostProgram() in mako_fg.cpp."""
    for directory in (library.parent / "mako-fg", library.parent.parent / "lib/mako-fg",
                      library.parent.parent.parent / "lib64/mako-fg"):
        if directory.is_dir():
            return directory
    return library.parent / "mako-fg"


def verify(releases: Path, libraries: list[Path], installed: bool = True) -> str:
    """No library contains a release's weights; installed libraries sit beside the active release in mako-fg/."""
    if not installed and not (releases / "versions.json").exists():
        return "none"  # A source build without MAKO FG's private release has no weights to look for.
    (version, files), others = release_models(releases)
    weights = [files[f"{MODEL}/weights.bin"], *(other[f"{MODEL}/weights.bin"] for _, other in others)]
    for library in libraries:
        data = library.read_bytes()
        if any(model in data for model in weights):
            raise PackageError(f"{library} contains MAKO FG weights; MAKO FG must stay a separate program")
        if not installed:
            continue
        directory = program_directory(library)
        wrong = [name for name, content in files.items() if name.startswith(f"{MODEL}/")
                 and (not (directory / name).is_file() or (directory / name).read_bytes() != content)]
        if not (directory / PROGRAM).is_file() or (directory / PROGRAM).read_bytes() != files.get(PROGRAM_PATH):
            wrong.append(PROGRAM)
        if wrong:
            raise PackageError(f"{library} is not beside MAKO FG release {version} ({', '.join(wrong)})")
    return version


def artifact_receipt(releases: Path, artifact: Path) -> ArtifactReceipt:
    (version, files), _ = release_models(releases)
    return ArtifactReceipt(schema_version=3, release_version=version, model_sha256=model_digest(files),
                           artifact_sha256=digest(artifact))


def record_artifact(releases: Path, artifact: Path, libraries: list[Path]) -> None:
    if not libraries:
        raise PackageError("An artifact receipt requires verified MAKO FG libraries")
    verify(releases, libraries)
    receipt = artifact_receipt(releases, artifact)
    artifact.with_name(artifact.name + ".mako-fg.json").write_text(
        json.dumps(receipt, indent=2) + "\n", encoding="utf-8")


def check_artifact(releases: Path, artifact: Path) -> str:
    receipt = json.loads(artifact.with_name(artifact.name + ".mako-fg.json").read_text(encoding="utf-8"))
    expected = artifact_receipt(releases, artifact)
    if receipt != expected:
        raise PackageError(f"{artifact} has no matching active MAKO FG verification receipt; rebuild it")
    return expected["release_version"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    verify_command = commands.add_parser("verify", help="Fail unless every library sits beside only the active release")
    verify_command.add_argument("--releases", type=Path, default=RELEASES)
    verify_command.add_argument("--separate-only", action="store_true",
                                help="For a build tree: check only that no library contains MAKO FG")
    verify_command.add_argument("libraries", type=Path, nargs="+")
    for command, help_text in (("record-artifact", "Bind a built archive to its verified active MAKO FG release"),
                               ("check-artifact", "Verify a cached archive's checksum and active MAKO FG release")):
        command_parser = commands.add_parser(command, help=help_text)
        command_parser.add_argument("--releases", type=Path, default=RELEASES)
        command_parser.add_argument("artifact", type=Path)
        if command == "record-artifact":
            command_parser.add_argument("libraries", type=Path, nargs="+")
    arguments = parser.parse_args()
    try:
        if arguments.command == "record-artifact":
            record_artifact(arguments.releases, arguments.artifact, arguments.libraries)
            print(f"MAKO FG: recorded verified artifact {arguments.artifact}")
        elif arguments.command == "check-artifact":
            version = check_artifact(arguments.releases, arguments.artifact)
            print(f"MAKO FG: release {version} verified in cached artifact {arguments.artifact}")
        else:
            version = verify(arguments.releases, arguments.libraries, not arguments.separate_only)
            for library in arguments.libraries:
                print(f"MAKO FG: release {version} verified for {library}")
    except (OSError, ValueError, KeyError, PackageError) as error:
        print(f"MAKO FG packaging: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
