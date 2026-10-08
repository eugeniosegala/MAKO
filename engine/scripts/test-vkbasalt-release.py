#!/usr/bin/env python3
"""Portable contract tests for MAKO's pinned vkBasalt payload."""

from __future__ import annotations

import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import subprocess
import sys
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
            "vulkan_headers_revision": "v1.4.365",
            "vulkan_headers_commit": "3" * 40,
            "vulkan_api_version": "1.4.365",
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

    def _archive(self, root: Path, *, manifest_api: str | None = None, source_revision: str | None = None) -> Path:
        live_reload_markers = b" ".join(MODULE.LIVE_RELOAD_MARKERS)
        manifest = lambda architecture: (json.dumps({
            "file_format_version": "1.2.1",
            "layer": {
                "name": MODULE.LAYER_NAME,
                "type": "GLOBAL",
                "api_version": manifest_api or self.pin["vulkan_api_version"],
                "library_path": "libvkbasalt.so",
                "library_arch": architecture,
                "enable_environment": MODULE.ENABLE_ENVIRONMENT,
                "disable_environment": MODULE.DISABLE_ENVIRONMENT,
            },
        }) + "\n").encode("utf-8")
        members = {
            MODULE.SOURCE_PATHS["lib64"]: (
                b"\x7fELF\x02vkBasalt_GetInstanceProcAddr "
                b"vkBasalt_GetDeviceProcAddr " + live_reload_markers
            ),
            MODULE.SOURCE_PATHS["lib32"]: (
                b"\x7fELF\x01vkBasalt_GetInstanceProcAddr "
                b"vkBasalt_GetDeviceProcAddr " + live_reload_markers
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
                f"vulkan_headers_revision={source_revision or self.pin['vulkan_headers_revision']}\n"
                f"vulkan_headers_commit={self.pin['vulkan_headers_commit']}\n"
                f"vulkan_api_version={self.pin['vulkan_api_version']}\n"
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

    def test_derives_pin_from_verified_archive(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            archive = self._archive(Path(directory))
            self.assertEqual(MODULE._pin_from_archive(archive), self.pin)

    def test_stale_flatpak_manifest_is_detected_without_rewriting(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pin = root / "pin.json"
            pin.write_text(json.dumps(self.pin))
            module = root / "vkbasalt-module.json"
            command = [sys.executable, str(SCRIPT), "--pin", str(pin)]
            subprocess.run(command + ["--generate-flatpak-module", str(module)], check=True, capture_output=True)
            manifest = root / "vkBasalt.flatpak.x86.json"
            stale = manifest.read_text().replace('1.4.365', '1.3.223')
            manifest.write_text(stale)
            result = subprocess.run(command + ["--check-flatpak-module", str(module)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"manifest is stale", result.stderr)
            self.assertEqual(manifest.read_text(), stale)

    def test_invalid_archive_does_not_replace_existing_pin(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = self._archive(root, manifest_api="1.3.223")
            pin = root / "pin.json"
            pin.write_text("preserve existing pin\n")
            result = subprocess.run([sys.executable, str(SCRIPT), "--pin", str(pin),
                                     "--update-from-archive", str(archive)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(pin.read_text(), "preserve existing pin\n")

    def test_rejects_divergent_header_pin(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "pin.json"
            self.pin["vulkan_headers_revision"] = "v1.3.223"
            path.write_text(json.dumps(self.pin))
            with self.assertRaisesRegex(ValueError, "header pin must match"):
                MODULE._read_pin(path)

    def test_rejects_stale_api_declaration(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            archive = self._archive(Path(directory), manifest_api="1.3.223")
            with self.assertRaisesRegex(ValueError, "manifest contract"):
                MODULE._validate_archive(self.pin, archive)

    def test_rejects_archive_built_with_different_headers(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            archive = self._archive(Path(directory), source_revision="v1.3.223")
            with self.assertRaisesRegex(ValueError, "Vulkan provenance"):
                MODULE._validate_archive(self.pin, archive)

    def test_generated_flatpak_manifests_match_both_architectures(self) -> None:
        for architecture, library in (("64", "lib64"), ("32", "lib/i386-linux-gnu")):
            layer = json.loads(MODULE._flatpak_manifest(self.pin, architecture))["layer"]
            self.assertEqual(layer["api_version"], self.pin["vulkan_api_version"])
            self.assertEqual(layer["library_arch"], architecture)
            self.assertIn(f"/{library}/vkbasalt/", layer["library_path"])

    def test_rejects_tampered_archive(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            archive = self._archive(root)
            self.pin["sha256"] = "f" * 64
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                MODULE._validate_archive(self.pin, archive)

    def test_rejects_library_without_complete_live_shader_catalog(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            archive = self._archive(root)
            members = MODULE._archive_members(archive)
            members[MODULE.SOURCE_PATHS["lib64"]] = members[
                MODULE.SOURCE_PATHS["lib64"]
            ].replace(b"makoHDRLook", b"missingHDRLook")

            with self.assertRaisesRegex(
                ValueError,
                "lib64 library is missing MAKO live reload markers: makoHDRLook",
            ):
                MODULE._validate_live_reload_markers(
                    "lib64",
                    members[MODULE.SOURCE_PATHS["lib64"]],
                )


if __name__ == "__main__":
    unittest.main()
