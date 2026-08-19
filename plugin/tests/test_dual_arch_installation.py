"""Tests for installing the matching 64-bit and 32-bit Vulkan layers."""

import io
import hashlib
import json
from pathlib import Path
import sys
import tarfile
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


class _Logger:
    def __getattr__(self, _name):
        return lambda *_args, **_kwargs: None


sys.modules.setdefault("decky", SimpleNamespace(logger=_Logger()))

from py_modules.mako_plugin.constants import (  # noqa: E402
    MAKO_LAYER_DISABLE_ENV,
    MAKO_LAYER_ENABLE_ENV,
    MAKO_LAYER_BUILD_MARKER,
    MAKO_PROFILE_FALLBACK_MARKER,
    MAKO_LAYER_NAME,
    JSON32_FILENAME,
    JSON_FILENAME,
    LIB_FILENAME,
)
from py_modules.mako_plugin.installation import InstallationService  # noqa: E402
from py_modules.mako_plugin import installation as installation_module  # noqa: E402


class DualArchInstallationTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)
        self.service = InstallationService(logger=_Logger())
        self.service.lib_file = self.root / "lib" / LIB_FILENAME
        self.service.lib32_file = self.root / "lib32" / LIB_FILENAME
        self.service.local_lib_dir = self.service.lib_file.parent
        self.service.local_lib32_dir = self.service.lib32_file.parent
        manifest_dir = self.root / "share/vulkan/implicit_layer.d"
        self.service.local_share_dir = manifest_dir
        self.service.json_file = manifest_dir / JSON_FILENAME
        self.service.json32_file = manifest_dir / JSON32_FILENAME
        registered_dir = self.root / "registered/vulkan/implicit_layer.d"
        self.service.user_vulkan_layer_dir = registered_dir
        self.service.registered_json_file = registered_dir / JSON_FILENAME
        self.service.registered_json32_file = registered_dir / JSON32_FILENAME
        self.service.cli_file = self.root / "bin/mako-cli"

    def tearDown(self):
        self.temp_dir.cleanup()

    @staticmethod
    def _manifest(arch: str) -> bytes:
        return json.dumps({
            "file_format_version": "1.2.1",
            "layer": {
                "name": "VK_LAYER_MAKO_frame_generation",
                "library_path": f"original-{arch}",
                "library_arch": arch,
            },
        }).encode("utf-8")

    def _archive(self, include_32bit: bool = True) -> Path:
        archive_path = self.root / "engine.tar.xz"
        members = {
            f"lib/{LIB_FILENAME}": (
                b"ELF64" + MAKO_LAYER_BUILD_MARKER + MAKO_PROFILE_FALLBACK_MARKER
            ),
            f"share/vulkan/implicit_layer.d/{JSON_FILENAME}": self._manifest("64"),
        }
        if include_32bit:
            members.update({
                f"lib32/{LIB_FILENAME}": (
                    b"ELF32" + MAKO_LAYER_BUILD_MARKER
                    + MAKO_PROFILE_FALLBACK_MARKER
                ),
                f"share/vulkan/implicit_layer.d/{JSON32_FILENAME}": self._manifest("32"),
            })

        with tarfile.open(archive_path, "w:xz") as archive:
            for name, content in members.items():
                info = tarfile.TarInfo(name)
                info.size = len(content)
                archive.addfile(info, io.BytesIO(content))
        return archive_path

    def test_installs_and_rewrites_both_layer_architectures(self):
        self.service._extract_and_install_files(self._archive())
        self.service._register_layer_manifests()

        self.assertTrue(self.service.lib_file.read_bytes().startswith(b"ELF64"))
        self.assertTrue(self.service.lib32_file.read_bytes().startswith(b"ELF32"))
        manifest64 = json.loads(self.service.json_file.read_text(encoding="utf-8"))
        manifest32 = json.loads(self.service.json32_file.read_text(encoding="utf-8"))
        self.assertEqual(manifest64["layer"]["library_arch"], "64")
        self.assertEqual(manifest64["layer"]["library_path"], "../../lib/libmako-render.so")
        self.assertEqual(manifest32["layer"]["library_arch"], "32")
        self.assertEqual(manifest32["layer"]["library_path"], "../../lib32/libmako-render.so")
        for manifest in (manifest64, manifest32):
            self.assertEqual(manifest["layer"]["name"], MAKO_LAYER_NAME)
            self.assertEqual(
                manifest["layer"]["enable_environment"],
                {MAKO_LAYER_ENABLE_ENV: "1"},
            )
            self.assertEqual(
                manifest["layer"]["disable_environment"],
                {MAKO_LAYER_DISABLE_ENV: "1"},
            )

        registered64 = json.loads(
            self.service.registered_json_file.read_text(encoding="utf-8")
        )
        registered32 = json.loads(
            self.service.registered_json32_file.read_text(encoding="utf-8")
        )
        self.assertEqual(
            registered64["layer"]["library_path"], str(self.service.lib_file)
        )
        self.assertEqual(
            registered32["layer"]["library_path"], str(self.service.lib32_file)
        )

    def test_installs_64bit_only_archive_and_removes_stale_32bit_files(self):
        self.service.lib32_file.parent.mkdir(parents=True, exist_ok=True)
        self.service.lib32_file.write_bytes(b"stale")
        self.service.json32_file.parent.mkdir(parents=True, exist_ok=True)
        self.service.json32_file.write_text("stale", encoding="utf-8")
        self.service.registered_json32_file.parent.mkdir(parents=True, exist_ok=True)
        self.service.registered_json32_file.write_text("stale", encoding="utf-8")

        self.service._extract_and_install_files(self._archive(include_32bit=False))
        self.service._register_layer_manifests()

        self.assertTrue(self.service.lib_file.read_bytes().startswith(b"ELF64"))
        self.assertTrue(self.service.json_file.exists())
        self.assertTrue(self.service.registered_json_file.exists())
        self.assertFalse(self.service.lib32_file.exists())
        self.assertFalse(self.service.json32_file.exists())
        self.assertFalse(self.service.registered_json32_file.exists())

    def test_rejects_payload_without_build_marker(self):
        archive_path = self._archive()
        replacement = self.root / "unidentified.tar.xz"
        with tarfile.open(archive_path, "r:xz") as source, tarfile.open(
            replacement, "w:xz"
        ) as output:
            for member in source.getmembers():
                content = source.extractfile(member).read()
                if member.name.endswith(LIB_FILENAME):
                    content = b"ELF-without-marker"
                copied = tarfile.TarInfo(member.name)
                copied.size = len(content)
                output.addfile(copied, io.BytesIO(content))

        with self.assertRaisesRegex(OSError, "build marker is missing"):
            self.service._extract_and_install_files(replacement)

        self.assertFalse(self.service.lib_file.exists())
        self.assertFalse(self.service.lib32_file.exists())

    def test_rejects_payload_without_profile_fallback_protocol(self):
        archive_path = self._archive()
        replacement = self.root / "old-wrapper-protocol.tar.xz"
        with tarfile.open(archive_path, "r:xz") as source, tarfile.open(
            replacement, "w:xz"
        ) as output:
            for member in source.getmembers():
                content = source.extractfile(member).read()
                if member.name.endswith(LIB_FILENAME):
                    content = content.replace(MAKO_PROFILE_FALLBACK_MARKER, b"")
                copied = tarfile.TarInfo(member.name)
                copied.size = len(content)
                output.addfile(copied, io.BytesIO(content))

        with self.assertRaisesRegex(OSError, "profile-fallback wrapper protocol"):
            self.service._extract_and_install_files(replacement)

        self.assertFalse(self.service.lib_file.exists())
        self.assertFalse(self.service.lib32_file.exists())

    def test_bundled_archive_checksum_is_verified_before_installation(self):
        archive_path = self._archive()
        expected = hashlib.sha256(archive_path.read_bytes()).hexdigest()

        self.service._validate_archive_checksum(archive_path, expected)
        with self.assertRaisesRegex(OSError, "checksum mismatch"):
            self.service._validate_archive_checksum(archive_path, "0" * 64)

    def test_armada_host_is_detected_when_decky_runs_through_fex(self):
        armada_marker = self.root / "usr/libexec/armada/device-env"
        armada_marker.parent.mkdir(parents=True)
        armada_marker.write_text("", encoding="utf-8")

        with (
            patch.object(installation_module, "ARMADA_DEVICE_ENV", armada_marker),
            patch.object(installation_module.platform, "machine", return_value="x86_64"),
        ):
            self.assertEqual(
                self.service._detect_native_host_architecture(),
                "aarch64",
            )

    def test_armada_host_rejects_x86_only_renderer_before_installation(self):
        with patch.object(
            self.service,
            "_detect_native_host_architecture",
            return_value="aarch64",
        ):
            with self.assertRaisesRegex(
                OSError,
                "does not include a native Armada/AArch64 Renderer",
            ):
                self.service._validate_host_architecture({
                    "host_architectures": ["x86_64"],
                })

    def test_installed_files_use_deterministic_permissions(self):
        self.service._extract_and_install_files(self._archive())

        self.assertEqual(self.service.lib_file.stat().st_mode & 0o777, 0o644)
        self.assertEqual(self.service.lib32_file.stat().st_mode & 0o777, 0o644)
        self.assertEqual(self.service.json_file.stat().st_mode & 0o777, 0o644)
        self.assertEqual(self.service.json32_file.stat().st_mode & 0o777, 0o644)


if __name__ == "__main__":
    unittest.main()
