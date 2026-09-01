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
    ACTIVE_RENDERER_OWNER_STANDALONE,
    ACTIVE_RENDERER_STATE_SCHEMA_VERSION,
    MAKO_LAYER_DISABLE_ENV,
    MAKO_LAYER_ENABLE_ENV,
    MAKO_LAYER_BUILD_MARKER,
    MAKO_PROFILE_FALLBACK_MARKER,
    MAKO_LAYER_NAME,
    SPATIAL_SCALING_LAYER_BUILD_MARKER,
    SPATIAL_SCALING_LAYER_DISABLE_ENV,
    SPATIAL_SCALING_LAYER_ENABLE_ENV,
    SPATIAL_SCALING_LAYER_NAME,
    SPATIAL_SCALING_JSON32_FILENAME,
    SPATIAL_SCALING_JSON_FILENAME,
    SPATIAL_SCALING_LIB_FILENAME,
    GAMESCOPE_WSI_DISABLE_ENV,
    GAMESCOPE_WSI_ENABLE_ENV,
    GAMESCOPE_WSI_LAYER_NAME_64,
    GAMESCOPE_WSI_MANIFEST_FILENAME_64,
    MANGOHUD_LAYER_NAME_64,
    MANGOHUD_MANIFEST_FILENAME_64,
    MANGOHUD_LAYER_NAME_32,
    MANGOHUD_MANIFEST_FILENAME_32,
    VKBASALT_LAYER_NAME_64,
    VKBASALT_MANIFEST_FILENAME_64,
    VKBASALT_LAYER_NAME_32,
    VKBASALT_MANIFEST_FILENAME_32,
    JSON32_FILENAME,
    JSON_FILENAME,
    LIB_FILENAME,
)
from py_modules.mako_plugin.config_schema import (  # noqa: E402
    ConfigurationManager,
    DEFAULT_PROFILE_NAME,
)
from py_modules.mako_plugin.installation import InstallationService  # noqa: E402
from py_modules.mako_plugin import installation as installation_module  # noqa: E402
from py_modules.mako_plugin import managed_files as managed_files_module  # noqa: E402
from py_modules.mako_plugin import host_environment as host_environment_module  # noqa: E402


class DualArchInstallationTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)
        self.service = InstallationService(logger=_Logger())
        self.service.lib_file = self.root / "lib" / LIB_FILENAME
        self.service.lib32_file = self.root / "lib32" / LIB_FILENAME
        self.service.spatial_scaling_lib_file = (
            self.root / "lib" / SPATIAL_SCALING_LIB_FILENAME
        )
        self.service.spatial_scaling_lib32_file = (
            self.root / "lib32" / SPATIAL_SCALING_LIB_FILENAME
        )
        self.service.local_lib_dir = self.service.lib_file.parent
        self.service.local_lib32_dir = self.service.lib32_file.parent
        manifest_dir = self.root / "share/vulkan/implicit_layer.d"
        self.service.local_share_dir = manifest_dir
        self.service.json_file = manifest_dir / JSON_FILENAME
        self.service.json32_file = manifest_dir / JSON32_FILENAME
        scaling_manifest_dir = self.root / "share/vulkan/spatial_scaling.d"
        self.service.spatial_scaling_layer_dir = scaling_manifest_dir
        self.service.spatial_scaling_json_file = (
            scaling_manifest_dir / SPATIAL_SCALING_JSON_FILENAME
        )
        self.service.spatial_scaling_json32_file = (
            scaling_manifest_dir / SPATIAL_SCALING_JSON32_FILENAME
        )
        self.service.gamescope_wsi_compatibility_dir = (
            self.root / "share/vulkan/gamescope_wsi_compatibility.d"
        )
        self.service.gamescope_wsi_compatibility_manifest = (
            self.service.gamescope_wsi_compatibility_dir /
            GAMESCOPE_WSI_MANIFEST_FILENAME_64
        )
        self.service.gamescope_wsi_compatibility_library = (
            self.service.gamescope_wsi_compatibility_dir /
            "libVkLayer_FROG_gamescope_wsi_x86_64.so"
        )
        self.service.mangohud_layer_dir = (
            self.root / "share/vulkan/mangohud.d"
        )
        self.service.vkbasalt_layer_dir = (
            self.root / "share/vulkan/vkbasalt.d"
        )
        self.service.mangohud_manifest = (
            self.service.mangohud_layer_dir /
            MANGOHUD_MANIFEST_FILENAME_64
        )
        self.service.mangohud_manifest32 = (
            self.service.mangohud_layer_dir /
            MANGOHUD_MANIFEST_FILENAME_32
        )
        self.service.vkbasalt_manifest = (
            self.service.vkbasalt_layer_dir /
            VKBASALT_MANIFEST_FILENAME_64
        )
        self.service.vkbasalt_manifest32 = (
            self.service.vkbasalt_layer_dir /
            VKBASALT_MANIFEST_FILENAME_32
        )
        registered_dir = self.root / "registered/vulkan/implicit_layer.d"
        self.service.user_vulkan_layer_dir = registered_dir
        self.service.registered_json_file = registered_dir / JSON_FILENAME
        self.service.registered_json32_file = registered_dir / JSON32_FILENAME
        self.service.cli_file = self.root / "bin/mako-cli"
        self.service.mako_launch_script_path = self.root / "bin/mako-run"
        self.service.diagnostics_script_path = self.root / "bin/mako-diagnostics"
        self.service.engine_state_file = self.root / "installed-engine.json"
        self.service.active_renderer_state_file = (
            self.root / "active-renderer.json"
        )
        self.service.standalone_install_prefix = self.root / "standalone"
        self.service.standalone_lib_file = (
            self.service.standalone_install_prefix / "lib" / LIB_FILENAME
        )
        self.service.standalone_lib32_file = (
            self.service.standalone_install_prefix / "lib32" / LIB_FILENAME
        )
        self.service.standalone_spatial_scaling_lib_file = (
            self.service.standalone_install_prefix
            / "lib"
            / SPATIAL_SCALING_LIB_FILENAME
        )
        self.service.standalone_spatial_scaling_lib32_file = (
            self.service.standalone_install_prefix
            / "lib32"
            / SPATIAL_SCALING_LIB_FILENAME
        )
        self.service.standalone_installer_state_file = (
            self.service.standalone_install_prefix
            / "share/mako-render/installer/installed-files.sha256"
        )
        self.service.config_dir = self.root / "config"
        self.service.config_file_path = self.service.config_dir / "conf.toml"

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_legacy_dll_placeholder_cleanup_preserves_real_user_paths(self):
        placeholder = "/games/Lossless Scaling/Lossless.dll"
        dll_service = SimpleNamespace(
            check_lossless_scaling_dll=lambda: {"detected": False}
        )
        defaults = ConfigurationManager.get_defaults()
        profile = {
            key: value
            for key, value in defaults.items()
            if key not in {"dll", "allow_fp16"}
        }

        cases = (
            (placeholder, False, ""),
            (placeholder, True, placeholder),
            ("/games/custom/Lossless.dll", False, "/games/custom/Lossless.dll"),
        )
        for configured_path, path_exists, expected_path in cases:
            with self.subTest(
                configured_path=configured_path,
                path_exists=path_exists,
            ), patch.object(Path, "exists", return_value=path_exists):
                merged = self.service._merge_config_with_defaults(
                    {
                        "current_profile": DEFAULT_PROFILE_NAME,
                        "global_config": {
                            "dll": configured_path,
                            "allow_fp16": True,
                        },
                        "profiles": {DEFAULT_PROFILE_NAME: profile},
                    },
                    dll_service,
                )

            self.assertEqual(merged["global_config"]["dll"], expected_path)

    @staticmethod
    def _manifest(arch: str, *, spatial_scaling: bool = False) -> bytes:
        return json.dumps({
            "file_format_version": "1.2.1",
            "layer": {
                "name": (
                    SPATIAL_SCALING_LAYER_NAME if spatial_scaling
                    else "VK_LAYER_MAKO_frame_generation"
                ),
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
            f"lib/{SPATIAL_SCALING_LIB_FILENAME}": (
                b"ELF64" + SPATIAL_SCALING_LAYER_BUILD_MARKER
                + MAKO_PROFILE_FALLBACK_MARKER
            ),
            f"share/vulkan/implicit_layer.d/{SPATIAL_SCALING_JSON_FILENAME}": (
                self._manifest("64", spatial_scaling=True)
            ),
        }
        if include_32bit:
            members.update({
                f"lib32/{LIB_FILENAME}": (
                    b"ELF32" + MAKO_LAYER_BUILD_MARKER
                    + MAKO_PROFILE_FALLBACK_MARKER
                ),
                f"share/vulkan/implicit_layer.d/{JSON32_FILENAME}": self._manifest("32"),
                f"lib32/{SPATIAL_SCALING_LIB_FILENAME}": (
                    b"ELF32" + SPATIAL_SCALING_LAYER_BUILD_MARKER
                    + MAKO_PROFILE_FALLBACK_MARKER
                ),
                f"share/vulkan/implicit_layer.d/{SPATIAL_SCALING_JSON32_FILENAME}": (
                    self._manifest("32", spatial_scaling=True)
                ),
            })

        with tarfile.open(archive_path, "w:xz") as archive:
            for name, content in members.items():
                info = tarfile.TarInfo(name)
                info.size = len(content)
                archive.addfile(info, io.BytesIO(content))
        return archive_path

    def _touch_minimal_64bit_installation(self) -> None:
        for path in (
            self.service.lib_file,
            self.service.json_file,
            self.service.spatial_scaling_lib_file,
            self.service.spatial_scaling_json_file,
            self.service.registered_json_file,
            self.service.mako_launch_script_path,
        ):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        self.service.json_file.write_text(
            json.dumps({
                "layer": {
                    "library_path": str(self.service.lib_file),
                },
            }),
            encoding="utf-8",
        )

    def _touch_minimal_standalone_64bit_installation(self) -> None:
        for path in (
            self.service.standalone_lib_file,
            self.service.json_file,
            self.service.standalone_spatial_scaling_lib_file,
            self.service.spatial_scaling_json_file,
            self.service.registered_json_file,
            self.service.mako_launch_script_path,
        ):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        self.service.json_file.write_text(
            json.dumps({
                "layer": {
                    "library_path": str(self.service.standalone_lib_file),
                },
            }),
            encoding="utf-8",
        )

    def test_installs_and_rewrites_both_layer_architectures(self):
        self.service._extract_and_install_files(self._archive())
        self.service._register_layer_manifests()

        self.assertTrue(self.service.lib_file.read_bytes().startswith(b"ELF64"))
        self.assertTrue(self.service.lib32_file.read_bytes().startswith(b"ELF32"))
        self.assertTrue(
            self.service.spatial_scaling_lib_file.read_bytes().startswith(b"ELF64")
        )
        self.assertTrue(
            self.service.spatial_scaling_lib32_file.read_bytes().startswith(b"ELF32")
        )
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

        scaling_manifest64 = json.loads(
            self.service.spatial_scaling_json_file.read_text(encoding="utf-8")
        )
        scaling_manifest32 = json.loads(
            self.service.spatial_scaling_json32_file.read_text(encoding="utf-8")
        )
        self.assertEqual(
            scaling_manifest64["layer"]["library_path"],
            "../../lib/libmako-render-scaling.so",
        )
        self.assertEqual(
            scaling_manifest32["layer"]["library_path"],
            "../../lib32/libmako-render-scaling.so",
        )
        for manifest in (scaling_manifest64, scaling_manifest32):
            self.assertEqual(
                manifest["layer"]["name"], SPATIAL_SCALING_LAYER_NAME
            )
            self.assertEqual(
                manifest["layer"]["enable_environment"],
                {SPATIAL_SCALING_LAYER_ENABLE_ENV: "1"},
            )
            self.assertEqual(
                manifest["layer"]["disable_environment"],
                {SPATIAL_SCALING_LAYER_DISABLE_ENV: "1"},
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

    def test_installs_only_a_valid_64_bit_gamescope_wsi_manifest(self):
        self.service.lib_file.parent.mkdir(parents=True)
        self.service.lib_file.write_bytes(b"installed")
        system_dir = self.root / "system-implicit"
        system_dir.mkdir()
        library = self.root / "libVkLayer_FROG_gamescope_wsi_x86_64.so"
        library.write_bytes(b"wsi")
        source = system_dir / GAMESCOPE_WSI_MANIFEST_FILENAME_64
        source.write_text(json.dumps({
            "file_format_version": "1.0.0",
            "layer": {
                "name": GAMESCOPE_WSI_LAYER_NAME_64,
                "type": "GLOBAL",
                "library_path": str(library),
                "enable_environment": {GAMESCOPE_WSI_ENABLE_ENV: "1"},
                "disable_environment": {GAMESCOPE_WSI_DISABLE_ENV: "1"},
            },
        }), encoding="utf-8")

        with patch.object(
            installation_module,
            "HOST_SYSTEM_IMPLICIT_LAYER_DIR",
            system_dir,
        ):
            self.assertTrue(
                self.service.migrate_gamescope_wsi_compatibility_manifest_if_needed()
            )
            self.assertFalse(
                self.service.migrate_gamescope_wsi_compatibility_manifest_if_needed()
            )

        installed = json.loads(
            self.service.gamescope_wsi_compatibility_manifest.read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(
            installed["layer"]["name"],
            GAMESCOPE_WSI_LAYER_NAME_64,
        )
        self.assertEqual(
            installed["layer"]["library_path"],
            str(self.service.gamescope_wsi_compatibility_library),
        )
        self.assertEqual(
            self.service.gamescope_wsi_compatibility_library.read_bytes(),
            b"wsi",
        )

    def test_invalid_gamescope_wsi_manifest_fails_closed(self):
        self.service.lib_file.parent.mkdir(parents=True)
        self.service.lib_file.write_bytes(b"installed")
        system_dir = self.root / "system-implicit"
        system_dir.mkdir()
        source = system_dir / GAMESCOPE_WSI_MANIFEST_FILENAME_64
        source.write_text('{"layer":{"name":"unexpected"}}', encoding="utf-8")
        destination = self.service.gamescope_wsi_compatibility_manifest
        destination.parent.mkdir(parents=True)
        destination.write_text("stale", encoding="utf-8")
        self.service.gamescope_wsi_compatibility_library.write_bytes(b"stale")

        with patch.object(
            installation_module,
            "HOST_SYSTEM_IMPLICIT_LAYER_DIR",
            system_dir,
        ):
            self.assertFalse(
                self.service.migrate_gamescope_wsi_compatibility_manifest_if_needed()
            )

        self.assertFalse(destination.exists())
        self.assertFalse(
            self.service.gamescope_wsi_compatibility_library.exists()
        )

    def test_gamescope_wsi_startup_migration_skips_uninstalled_renderer(self):
        system_dir = self.root / "system-implicit"
        system_dir.mkdir()
        source = system_dir / GAMESCOPE_WSI_MANIFEST_FILENAME_64
        source.write_text("{}", encoding="utf-8")

        with patch.object(
            installation_module,
            "HOST_SYSTEM_IMPLICIT_LAYER_DIR",
            system_dir,
        ):
            self.assertFalse(
                self.service.migrate_gamescope_wsi_compatibility_manifest_if_needed()
            )

        self.assertFalse(self.service.gamescope_wsi_compatibility_manifest.exists())

    def test_stages_only_valid_selected_postprocess_manifests(self):
        self.service.lib_file.parent.mkdir(parents=True)
        self.service.lib_file.write_bytes(b"installed")
        system_dir = self.root / "system-postprocess"
        system_dir.mkdir()
        mangohud_library = self.root / "libMangoHud.so"
        mangohud_library.write_bytes(b"mangohud")
        mangohud_library32 = self.root / "libMangoHud32.so"
        mangohud_library32.write_bytes(b"mangohud32")
        (system_dir / MANGOHUD_MANIFEST_FILENAME_64).write_text(
            json.dumps({
                "file_format_version": "1.0.0",
                "layer": {
                    "name": MANGOHUD_LAYER_NAME_64,
                    "type": "GLOBAL",
                    "library_path": str(mangohud_library),
                    "enable_environment": {"MANGOHUD": "1"},
                    "disable_environment": {"DISABLE_MANGOHUD": "1"},
                },
            }),
            encoding="utf-8",
        )
        (system_dir / MANGOHUD_MANIFEST_FILENAME_32).write_text(
            json.dumps({
                "file_format_version": "1.0.0",
                "layer": {
                    "name": MANGOHUD_LAYER_NAME_32,
                    "type": "GLOBAL",
                    "library_path": str(mangohud_library32),
                    "enable_environment": {"MANGOHUD": "1"},
                    "disable_environment": {"DISABLE_MANGOHUD": "1"},
                },
            }),
            encoding="utf-8",
        )
        (system_dir / VKBASALT_MANIFEST_FILENAME_64).write_text(
            json.dumps({
                "file_format_version": "1.0.0",
                "layer": {
                    "name": VKBASALT_LAYER_NAME_64,
                    "type": "GLOBAL",
                    "library_path": "libvkbasalt.so",
                    "enable_environment": {"ENABLE_VKBASALT": "1"},
                    "disable_environment": {"DISABLE_VKBASALT": "1"},
                },
            }),
            encoding="utf-8",
        )
        (system_dir / VKBASALT_MANIFEST_FILENAME_32).write_text(
            json.dumps({
                "file_format_version": "1.0.0",
                "layer": {
                    "name": VKBASALT_LAYER_NAME_32,
                    "type": "GLOBAL",
                    "library_path": "libvkbasalt32.so",
                    "library_arch": "32",
                    "enable_environment": {"ENABLE_VKBASALT": "1"},
                    "disable_environment": {"DISABLE_VKBASALT": "1"},
                },
            }),
            encoding="utf-8",
        )
        (system_dir / "unrelated-capture.json").write_text(
            '{"layer":{"name":"VK_LAYER_unrelated"}}',
            encoding="utf-8",
        )

        with patch.object(
            installation_module,
            "HOST_SYSTEM_IMPLICIT_LAYER_DIR",
            system_dir,
        ):
            self.assertTrue(
                self.service.refresh_guarded_postprocess_manifests_if_needed()
            )
            self.assertFalse(
                self.service.refresh_guarded_postprocess_manifests_if_needed()
            )

        mangohud64 = json.loads(self.service.mangohud_manifest.read_text(
            encoding="utf-8"
        ))["layer"]
        self.assertEqual(mangohud64["name"], MANGOHUD_LAYER_NAME_64)
        self.assertEqual(mangohud64["library_arch"], "64")
        vkbasalt64 = json.loads(self.service.vkbasalt_manifest.read_text(
            encoding="utf-8"
        ))["layer"]
        self.assertEqual(vkbasalt64["name"], VKBASALT_LAYER_NAME_64)
        self.assertEqual(vkbasalt64["library_arch"], "64")
        self.assertEqual(
            json.loads(self.service.mangohud_manifest32.read_text(
                encoding="utf-8"
            ))["layer"]["name"],
            MANGOHUD_LAYER_NAME_32,
        )
        self.assertEqual(
            json.loads(self.service.vkbasalt_manifest32.read_text(
                encoding="utf-8"
            ))["layer"]["library_arch"],
            "32",
        )
        self.assertEqual(
            set(self.service.mangohud_layer_dir.iterdir()),
            {
                self.service.mangohud_manifest,
                self.service.mangohud_manifest32,
            },
        )
        self.assertEqual(
            set(self.service.vkbasalt_layer_dir.iterdir()),
            {
                self.service.vkbasalt_manifest,
                self.service.vkbasalt_manifest32,
            },
        )

    def test_invalid_postprocess_manifest_removes_stale_staged_copy(self):
        self.service.lib_file.parent.mkdir(parents=True)
        self.service.lib_file.write_bytes(b"installed")
        system_dir = self.root / "invalid-postprocess"
        system_dir.mkdir()
        (system_dir / MANGOHUD_MANIFEST_FILENAME_64).write_text(
            '{"layer":{"name":"VK_LAYER_unrelated"}}',
            encoding="utf-8",
        )
        self.service.mangohud_manifest.parent.mkdir(parents=True)
        self.service.mangohud_manifest.write_text("stale", encoding="utf-8")

        with patch.object(
            installation_module,
            "HOST_SYSTEM_IMPLICIT_LAYER_DIR",
            system_dir,
        ):
            self.assertTrue(
                self.service.refresh_guarded_postprocess_manifests_if_needed()
            )

        self.assertFalse(self.service.mangohud_manifest.exists())

    def test_installs_64bit_only_archive_and_removes_stale_32bit_files(self):
        self.service.lib32_file.parent.mkdir(parents=True, exist_ok=True)
        self.service.lib32_file.write_bytes(b"stale")
        self.service.json32_file.parent.mkdir(parents=True, exist_ok=True)
        self.service.json32_file.write_text("stale", encoding="utf-8")
        self.service.registered_json32_file.parent.mkdir(parents=True, exist_ok=True)
        self.service.registered_json32_file.write_text("stale", encoding="utf-8")
        self.service.spatial_scaling_lib32_file.write_bytes(b"stale")
        self.service.spatial_scaling_json32_file.parent.mkdir(
            parents=True, exist_ok=True
        )
        self.service.spatial_scaling_json32_file.write_text(
            "stale", encoding="utf-8"
        )

        self.service._extract_and_install_files(self._archive(include_32bit=False))
        self.service._register_layer_manifests()

        self.assertTrue(self.service.lib_file.read_bytes().startswith(b"ELF64"))
        self.assertTrue(self.service.json_file.exists())
        self.assertTrue(self.service.registered_json_file.exists())
        self.assertFalse(self.service.lib32_file.exists())
        self.assertFalse(self.service.json32_file.exists())
        self.assertFalse(self.service.registered_json32_file.exists())
        self.assertFalse(self.service.spatial_scaling_lib32_file.exists())
        self.assertFalse(self.service.spatial_scaling_json32_file.exists())

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

    def test_reads_legacy_release_remote_binary_metadata_for_2_0_upgrade(self):
        package_manifest = {
            "version": "2.0.0",
            "remote_binary": [{
                "name": "MAKO-Renderer-v2.0.0-linux.tar.xz",
                "version": "2.0.0",
                "sha256hash": "a" * 64,
                "host_architectures": ["x86_64"],
            }],
        }
        (self.root / "package.json").write_text(
            json.dumps(package_manifest), encoding="utf-8"
        )

        metadata = self.service._bundled_archive_metadata(self.root)

        self.assertEqual(metadata["name"], "MAKO-Renderer-v2.0.0-linux.tar.xz")
        self.assertEqual(metadata["version"], "2.0.0")
        self.assertEqual(metadata["architectures"], ["64", "32"])
        self.assertEqual(metadata["host_architectures"], ["x86_64"])

    def test_reads_self_contained_local_renderer_metadata(self):
        package_manifest = {
            "version": "2.1.0.local.test",
            "bundled_renderer": {
                "name": "MAKO-Renderer-v2.0.0-local.test-linux.tar.xz",
                "version": "2.0.0-local.test",
                "sha256hash": "b" * 64,
                "architectures": ["64"],
                "host_architectures": ["x86_64"],
            },
        }
        (self.root / "package.json").write_text(
            json.dumps(package_manifest), encoding="utf-8"
        )

        metadata = self.service._bundled_archive_metadata(self.root)

        self.assertEqual(
            metadata["name"], "MAKO-Renderer-v2.0.0-local.test-linux.tar.xz"
        )
        self.assertEqual(metadata["version"], "2.0.0-local.test")
        self.assertEqual(metadata["architectures"], ["64"])

    def test_rejects_ambiguous_remote_and_bundled_renderer_metadata(self):
        package_manifest = {
            "bundled_renderer": {},
            "remote_binary": [{}],
        }
        (self.root / "package.json").write_text(
            json.dumps(package_manifest), encoding="utf-8"
        )

        with self.assertRaisesRegex(
            OSError, "must not define both bundled_renderer and remote_binary"
        ):
            self.service._bundled_archive_metadata(self.root)

    def test_armada_host_is_detected_when_decky_runs_through_fex(self):
        armada_marker = self.root / "usr/libexec/armada/device-env"
        armada_marker.parent.mkdir(parents=True)
        armada_marker.write_text("", encoding="utf-8")

        with (
            patch.object(installation_module, "ARMADA_DEVICE_ENV", armada_marker),
            patch.object(
                host_environment_module.platform,
                "machine",
                return_value="x86_64",
            ),
        ):
            self.assertEqual(
                self.service._detect_native_host_architecture(),
                "aarch64",
            )

    def test_pid_one_elf_identifies_translated_aarch64_host(self):
        native_process = self.root / "native-init"
        elf_header = bytearray(20)
        elf_header[:4] = b"\x7fELF"
        elf_header[5] = 1
        elf_header[18:20] = (183).to_bytes(2, "little")
        native_process.write_bytes(elf_header)

        environment = host_environment_module.detect_host_environment(
            process_machine="x86_64",
            armada_marker=self.root / "missing-device-env",
            native_process=native_process,
        )

        self.assertEqual(environment.process_architecture, "x86_64")
        self.assertEqual(environment.native_architecture, "aarch64")
        self.assertTrue(environment.translated)
        self.assertFalse(environment.armada)

    def test_armada_host_rejects_x86_only_renderer_before_installation(self):
        with patch.object(
            self.service,
            "_detect_native_host_architecture",
            return_value="aarch64",
        ):
            with self.assertRaisesRegex(
                OSError,
                "does not include a validated native Armada/AArch64 Renderer",
            ):
                self.service._validate_host_architecture({
                    "host_architectures": ["x86_64"],
                })

    def test_existing_x86_files_are_not_reported_installed_on_armada(self):
        for path in (
            self.service.lib_file,
            self.service.lib32_file,
            self.service.json_file,
            self.service.json32_file,
            self.service.spatial_scaling_lib_file,
            self.service.spatial_scaling_lib32_file,
            self.service.spatial_scaling_json_file,
            self.service.spatial_scaling_json32_file,
            self.service.registered_json_file,
            self.service.registered_json32_file,
            self.service.mako_launch_script_path,
        ):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        self.service.engine_state_file.write_text(
            json.dumps({
                "archive": "renderer.tar.xz",
                "version": "2.0.0",
                "sha256hash": "a" * 64,
            }),
            encoding="utf-8",
        )
        metadata = {
            "name": "renderer.tar.xz",
            "version": "2.0.0",
            "sha256hash": "a" * 64,
            "architectures": ["64", "32"],
            "host_architectures": ["x86_64"],
        }

        with (
            patch.object(
                self.service,
                "_bundled_archive_metadata",
                return_value=metadata,
            ),
            patch.object(
                self.service,
                "_detect_native_host_architecture",
                return_value="aarch64",
            ),
        ):
            status = self.service.check_installation()

        self.assertFalse(status["installed"])
        self.assertTrue(status["lib_exists"])
        self.assertEqual(status["host_architecture"], "aarch64")
        self.assertFalse(status["host_architecture_supported"])
        self.assertIn("Games remain on their normal Armada launch path", status["error"])

    def test_standalone_active_version_drives_decky_update_status(self):
        self._touch_minimal_standalone_64bit_installation()
        metadata = {
            "name": "renderer.tar.xz",
            "version": "3.0.0",
            "sha256hash": "a" * 64,
            "architectures": ["64"],
            "host_architectures": ["x86_64"],
        }
        self.service.standalone_installer_state_file.parent.mkdir(
            parents=True, exist_ok=True
        )
        self.service.standalone_installer_state_file.write_text(
            f"{'0' * 64}  bin/mako-ui\n",
            encoding="utf-8",
        )

        for active_version, expected_update in (
            ("3.0.0", False),
            ("3.1.0", True),
        ):
            with self.subTest(active_version=active_version):
                self.service.active_renderer_state_file.write_text(
                    json.dumps({
                        "schema_version": ACTIVE_RENDERER_STATE_SCHEMA_VERSION,
                        "owner": ACTIVE_RENDERER_OWNER_STANDALONE,
                        "version": active_version,
                    }),
                    encoding="utf-8",
                )
                with (
                    patch.object(
                        self.service,
                        "_bundled_archive_metadata",
                        return_value=metadata,
                    ),
                    patch.object(
                        self.service,
                        "_host_compatibility",
                        return_value=("x86_64", True, None),
                    ),
                ):
                    status = self.service.check_installation()

                self.assertTrue(status["installed"])
                self.assertEqual(
                    status["installed_engine_version"], active_version
                )
                self.assertEqual(
                    status["engine_update_required"], expected_update
                )

    def test_legacy_decky_engine_state_remains_readable(self):
        self._touch_minimal_64bit_installation()
        metadata = {
            "name": "renderer.tar.xz",
            "version": "3.0.0",
            "sha256hash": "a" * 64,
            "architectures": ["64"],
            "host_architectures": ["x86_64"],
        }
        self.service.engine_state_file.write_text(
            json.dumps({
                "archive": metadata["name"],
                "version": metadata["version"],
                "sha256hash": metadata["sha256hash"],
            }),
            encoding="utf-8",
        )

        with (
            patch.object(
                self.service,
                "_bundled_archive_metadata",
                return_value=metadata,
            ),
            patch.object(
                self.service,
                "_host_compatibility",
                return_value=("x86_64", True, None),
            ),
        ):
            status = self.service.check_installation()

        self.assertTrue(status["installed"])
        self.assertEqual(status["installed_engine_version"], "3.0.0")
        self.assertFalse(status["engine_update_required"])

    def test_legacy_standalone_override_invalidates_stale_decky_state(self):
        self._touch_minimal_64bit_installation()
        metadata = {
            "name": "renderer.tar.xz",
            "version": "3.0.0",
            "sha256hash": "a" * 64,
            "architectures": ["64"],
            "host_architectures": ["x86_64"],
        }
        self.service.engine_state_file.write_text(
            json.dumps({
                "archive": metadata["name"],
                "version": metadata["version"],
                "sha256hash": metadata["sha256hash"],
            }),
            encoding="utf-8",
        )
        self.service._write_active_renderer_state(metadata)
        standalone_library = (
            self.service.standalone_install_prefix
            / "lib/libmako-render.so"
        )
        for path in (
            standalone_library,
            self.service.standalone_spatial_scaling_lib_file,
        ):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        self.service.json_file.write_text(
            json.dumps({
                "layer": {
                    "library_path": str(standalone_library),
                },
            }),
            encoding="utf-8",
        )

        with (
            patch.object(
                self.service,
                "_bundled_archive_metadata",
                return_value=metadata,
            ),
            patch.object(
                self.service,
                "_host_compatibility",
                return_value=("x86_64", True, None),
            ),
        ):
            status = self.service.check_installation()

        self.assertTrue(status["installed"])
        self.assertFalse(status["engine_version_known"])
        self.assertIsNone(status["installed_engine_version"])
        self.assertTrue(status["engine_update_required"])

    def test_uninstall_removes_decky_and_standalone_renderer_files(self):
        decky_file = self.service.lib_file
        decky_file.parent.mkdir(parents=True, exist_ok=True)
        decky_file.write_bytes(b"decky-renderer")
        self.service.active_renderer_state_file.write_text(
            json.dumps({
                "schema_version": ACTIVE_RENDERER_STATE_SCHEMA_VERSION,
                "owner": ACTIVE_RENDERER_OWNER_STANDALONE,
                "version": "3.1.0",
            }),
            encoding="utf-8",
        )

        standalone_file = self.service.standalone_install_prefix / "bin/mako-ui"
        modified_file = (
            self.service.standalone_install_prefix
            / "share/applications/io.github.eugeniosegala.mako.desktop"
        )
        standalone_file.parent.mkdir(parents=True, exist_ok=True)
        modified_file.parent.mkdir(parents=True, exist_ok=True)
        standalone_file.write_bytes(b"standalone-ui")
        modified_file.write_bytes(b"user-modified")
        self.service.standalone_installer_state_file.parent.mkdir(
            parents=True, exist_ok=True
        )
        self.service.standalone_installer_state_file.write_text(
            "\n".join((
                f"{hashlib.sha256(standalone_file.read_bytes()).hexdigest()}  bin/mako-ui",
                f"{'0' * 64}  share/applications/io.github.eugeniosegala.mako.desktop",
            )) + "\n",
            encoding="utf-8",
        )
        self.service.config_file_path.parent.mkdir(parents=True, exist_ok=True)
        self.service.config_file_path.write_text("version = 2\n", encoding="utf-8")

        result = self.service.uninstall()

        self.assertTrue(result["success"], result.get("error"))
        self.assertFalse(decky_file.exists())
        self.assertFalse(self.service.active_renderer_state_file.exists())
        self.assertFalse(standalone_file.exists())
        self.assertTrue(modified_file.exists())
        self.assertFalse(self.service.standalone_installer_state_file.exists())
        self.assertTrue(self.service.config_file_path.exists())

    def test_installed_files_use_deterministic_permissions(self):
        self.service._extract_and_install_files(self._archive())

        self.assertEqual(self.service.lib_file.stat().st_mode & 0o777, 0o644)
        self.assertEqual(self.service.lib32_file.stat().st_mode & 0o777, 0o644)
        self.assertEqual(self.service.json_file.stat().st_mode & 0o777, 0o644)
        self.assertEqual(self.service.json32_file.stat().st_mode & 0o777, 0o644)

    def test_install_atomically_replaces_read_only_managed_files(self):
        for path in (self.service.lib_file, self.service.lib32_file):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"stale")
            path.chmod(0o444)

        self.service._extract_and_install_files(self._archive())

        self.assertTrue(self.service.lib_file.read_bytes().startswith(b"ELF64"))
        self.assertTrue(self.service.lib32_file.read_bytes().startswith(b"ELF32"))
        self.assertEqual(self.service.lib_file.stat().st_mode & 0o777, 0o644)
        self.assertEqual(self.service.lib32_file.stat().st_mode & 0o777, 0o644)

    def test_install_replaces_managed_symlink_without_following_it(self):
        unrelated_file = self.root / "unrelated-library.so"
        unrelated_file.write_bytes(b"leave-this-alone")
        self.service.lib_file.parent.mkdir(parents=True, exist_ok=True)
        self.service.lib_file.symlink_to(unrelated_file)

        self.service._extract_and_install_files(self._archive())

        self.assertFalse(self.service.lib_file.is_symlink())
        self.assertTrue(self.service.lib_file.read_bytes().startswith(b"ELF64"))
        self.assertEqual(unrelated_file.read_bytes(), b"leave-this-alone")

    def test_fresh_install_does_not_require_chmod(self):
        with patch.object(
            managed_files_module.os,
            "fchmod",
            side_effect=PermissionError(1, "Operation not permitted"),
        ) as chmod:
            self.service._extract_and_install_files(self._archive())
            self.service._register_layer_manifests()

        chmod.assert_not_called()
        self.assertTrue(self.service.lib_file.read_bytes().startswith(b"ELF64"))
        self.assertTrue(self.service.registered_json_file.exists())

    def test_atomic_copy_failure_preserves_existing_file(self):
        source = self.root / "replacement"
        destination = self.root / "managed-file"
        source.write_bytes(b"replacement")
        destination.write_bytes(b"existing")

        with patch.object(
            managed_files_module.os,
            "fsync",
            side_effect=OSError("simulated write failure"),
        ):
            with self.assertRaisesRegex(OSError, "atomically replace"):
                managed_files_module.copy_managed_file_atomically(
                    source,
                    destination,
                    0o644,
                    self.service.log,
                )

        self.assertEqual(destination.read_bytes(), b"existing")
        self.assertEqual(list(self.root.glob(".managed-file.*")), [])

    def test_install_preserves_unmanaged_neighbouring_files(self):
        unmanaged_file = self.service.local_lib_dir.parent / "user-note.txt"
        unmanaged_file.parent.mkdir(parents=True, exist_ok=True)
        unmanaged_file.write_text("keep", encoding="utf-8")

        self.service._extract_and_install_files(self._archive())

        self.assertEqual(unmanaged_file.read_text(encoding="utf-8"), "keep")

    def test_install_reports_read_only_user_configuration(self):
        self.service.config_dir = self.root / "config"
        self.service.config_file_path = self.service.config_dir / "conf.toml"
        self.service.config_dir.mkdir(parents=True)
        self.service.config_file_path.write_text("version = 2\n", encoding="utf-8")
        self.service.config_file_path.chmod(0o444)

        with self.assertRaisesRegex(
            OSError,
            "configuration is read-only.*Restore owner write permission",
        ):
            self.service._create_config_file()

        self.assertEqual(
            self.service.config_file_path.read_text(encoding="utf-8"),
            "version = 2\n",
        )


if __name__ == "__main__":
    unittest.main()
