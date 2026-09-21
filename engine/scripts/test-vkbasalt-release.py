#!/usr/bin/env python3
"""Portable contract tests for MAKO's pinned vkBasalt payload."""

from __future__ import annotations

import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("manage-vkbasalt-release.py")
SPEC = importlib.util.spec_from_file_location("manage_vkbasalt_release", SCRIPT)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"Could not load {SCRIPT}")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class VkBasaltReleaseTests(unittest.TestCase):
    def setUp(self) -> None:
        self.pin = {
            "schema_version": 1,
            "repository": "eugeniosegala/vkBasalt",
            "tag": "mako-v0.3.2.10-1",
            "source_commit": "1" * 40,
            "upstream_commit": "2" * 40,
            "asset": "vkBasalt-mako-v0.3.2.10-1-linux-x86.tar.xz",
            "url": (
                "https://github.com/eugeniosegala/vkBasalt/releases/"
                "download/mako-v0.3.2.10-1/"
                "vkBasalt-mako-v0.3.2.10-1-linux-x86.tar.xz"
            ),
            "sha256": "0" * 64,
        }

    def _archive(self, root: Path) -> Path:
        manifest = lambda architecture: (json.dumps({
            "file_format_version": "1.2.1",
            "layer": {
                "name": MODULE.LAYER_NAME,
                "type": "GLOBAL",
                "library_path": "libvkbasalt.so",
                "library_arch": architecture,
                "enable_environment": MODULE.ENABLE_ENVIRONMENT,
                "disable_environment": MODULE.DISABLE_ENVIRONMENT,
            },
        }) + "\n").encode("utf-8")
        members = {
            MODULE.SOURCE_PATHS["lib64"]: (
                b"\x7fELF\x02vkBasalt_GetInstanceProcAddr "
                b"vkBasalt_GetDeviceProcAddr"
            ),
            MODULE.SOURCE_PATHS["lib32"]: (
                b"\x7fELF\x01vkBasalt_GetInstanceProcAddr "
                b"vkBasalt_GetDeviceProcAddr"
            ),
            MODULE.SOURCE_PATHS["manifest64"]: manifest("64"),
            MODULE.SOURCE_PATHS["manifest32"]: manifest("32"),
            MODULE.SOURCE_PATHS["license"]: b"zlib license\n",
            MODULE.SOURCE_PATHS["reshade_license"]: b"BSD license\n",
            MODULE.SOURCE_PATHS["source"]: (
                "repository=https://github.com/eugeniosegala/vkBasalt\n"
                f"tag={self.pin['tag']}\n"
                f"commit={self.pin['source_commit']}\n"
                "upstream_repository=https://github.com/DadSchoorse/vkBasalt\n"
                f"upstream_commit={self.pin['upstream_commit']}\n"
            ).encode("utf-8"),
        }
        members[MODULE.SOURCE_PATHS["checksums"]] = "".join(
            f"{hashlib.sha256(content).hexdigest()}  {path}\n"
            for path, content in sorted(members.items())
        ).encode("utf-8")
        archive_path = root / self.pin["asset"]
        with tarfile.open(archive_path, "w:xz") as archive:
            for path, content in members.items():
                info = tarfile.TarInfo(path)
                info.size = len(content)
                archive.addfile(info, io.BytesIO(content))
        self.pin["sha256"] = hashlib.sha256(
            archive_path.read_bytes()
        ).hexdigest()
        return archive_path

    def test_validates_and_stages_both_architectures(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            archive = self._archive(root)
            members = MODULE._validate_archive(self.pin, archive)
            target = root / "target"
            MODULE._stage_native(self.pin, members, target, True)

            self.assertEqual(
                (target / "lib/vkbasalt/libvkbasalt.so").read_bytes()[4],
                2,
            )
            self.assertEqual(
                (target / "lib32/vkbasalt/libvkbasalt.so").read_bytes()[4],
                1,
            )
            manifest = json.loads((
                target /
                "share/mako-render/vulkan/vkbasalt.d/vkBasalt.json"
            ).read_text(encoding="utf-8"))
            self.assertEqual(
                manifest["layer"]["library_path"],
                "../../../../lib/vkbasalt/libvkbasalt.so",
            )

    def test_rejects_tampered_archive(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            archive = self._archive(root)
            self.pin["sha256"] = "f" * 64
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                MODULE._validate_archive(self.pin, archive)


if __name__ == "__main__":
    unittest.main()
