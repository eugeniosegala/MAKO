"""Checksummed native overlays written only by direct development deployment."""

import hashlib
import json
import logging
from pathlib import Path
from typing import TypedDict, cast

from .managed_files import write_managed_text_atomically


RECEIPT_PATH = Path("bin/development-renderer.json")


class DevelopmentRendererReceipt(TypedDict):
    schema_version: int
    package_sha256: str
    active_renderer_sha256: str | None
    files: dict[str, str]


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _active_checksum(path: Path) -> str | None:
    try:
        return file_sha256(path)
    except FileNotFoundError:
        return None


def read_development_receipt(
        plugin_root: Path, active_state: Path) -> DevelopmentRendererReceipt | None:
    """Reject overlays from another package or native installation baseline."""
    try:
        receipt = json.loads((plugin_root / RECEIPT_PATH).read_text(encoding="utf-8"))
        if (not isinstance(receipt, dict)
                or receipt.get("schema_version") != 1
                or "active_renderer_sha256" not in receipt
                or receipt.get("package_sha256") != file_sha256(plugin_root / "package.json")
                or receipt.get("active_renderer_sha256") != _active_checksum(active_state)
                or not isinstance(receipt.get("files"), dict)):
            return None
        for path, checksum in receipt["files"].items():
            if (not Path(path).is_absolute() or not isinstance(checksum, str)
                    or len(checksum) != 64
                    or any(character not in "0123456789abcdef" for character in checksum)):
                return None
        return cast(DevelopmentRendererReceipt, receipt)
    except (OSError, ValueError, TypeError):
        return None


def record_development_files(
        plugin_root: Path, active_state: Path,
        copies: list[tuple[Path, Path]]) -> None:
    """Verify copied files before atomically extending the current overlay."""
    previous = read_development_receipt(plugin_root, active_state)
    files = dict(previous["files"]) if previous else {}
    for source, installed in copies:
        checksum = file_sha256(source)
        if file_sha256(installed) != checksum:
            raise ValueError(f"Development copy differs from its source: {installed}")
        files[str(installed.absolute())] = checksum
    receipt: DevelopmentRendererReceipt = {
        "schema_version": 1,
        "package_sha256": file_sha256(plugin_root / "package.json"),
        "active_renderer_sha256": _active_checksum(active_state),
        "files": files,
    }
    write_managed_text_atomically(
        plugin_root / RECEIPT_PATH, json.dumps(receipt, indent=2) + "\n",
        0o644, logging.getLogger("MAKO Decky"))
