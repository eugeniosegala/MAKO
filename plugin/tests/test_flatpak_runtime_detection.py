"""Tests for Flatpak runtime-to-Vulkan-layer compatibility detection."""

import sys
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from pathlib import Path


class _Logger:
    def __getattr__(self, _name):
        return lambda *_args, **_kwargs: None


sys.modules.setdefault("decky", SimpleNamespace(logger=_Logger()))

from py_modules.mako_plugin.flatpak_service import FlatpakService  # noqa: E402
from py_modules.mako_plugin.constants import (  # noqa: E402
    FLATPAK_23_08_FILENAME,
    FLATPAK_24_08_FILENAME,
    FLATPAK_25_08_FILENAME,
    FLATPAK_EXTENSION_NAME,
    FLATPAK_EXTENSION_PREFIX,
    FLATPAK_HOST_ARCHITECTURE,
    FLATPAK_IMPLICIT_LAYER_DIR,
    FLATPAK_RUNTIME_BUNDLES,
    LIB_FILENAME,
    LOCAL_LIB,
    LOCAL_LIB32,
    SPATIAL_SCALING_JSON32_FILENAME,
    SPATIAL_SCALING_JSON_FILENAME,
    SPATIAL_SCALING_LAYER_DIR,
    SPATIAL_SCALING_LIB_FILENAME,
)
from shared_config import SUPPORTED_FLATPAK_RUNTIME_VERSIONS  # noqa: E402


def _result(stdout="", stderr="", returncode=0):
    return SimpleNamespace(stdout=stdout, stderr=stderr, returncode=returncode)


class FlatpakRuntimeDetectionTests(unittest.TestCase):
    app_id = "org.example.Application"

    def setUp(self):
        self.service = FlatpakService(logger=_Logger())
        # Flatpak runtime tests model the supported x86_64 package unless a
        # case explicitly overrides this boundary. Host detection itself has
        # dedicated coverage in test_dual_arch_installation.py.
        self.service._host_architecture_supported = lambda: True

    def test_runtime_descriptor_owns_bundle_and_extension_identity(self):
        self.assertEqual(
            tuple(FLATPAK_RUNTIME_BUNDLES),
            SUPPORTED_FLATPAK_RUNTIME_VERSIONS,
        )
        for version, bundle in FLATPAK_RUNTIME_BUNDLES.items():
            with self.subTest(version=version):
                self.assertEqual(
                    bundle.filename,
                    f"{FLATPAK_EXTENSION_NAME}-{version}.flatpak",
                )
                self.assertEqual(
                    bundle.extension_id,
                    f"{FLATPAK_EXTENSION_NAME}/{FLATPAK_HOST_ARCHITECTURE}/{version}",
                )
                self.assertEqual(
                    self.service._get_extension_id(version),
                    bundle.extension_id,
                )

        self.assertEqual(
            (
                FLATPAK_23_08_FILENAME,
                FLATPAK_24_08_FILENAME,
                FLATPAK_25_08_FILENAME,
            ),
            tuple(
                bundle.filename
                for bundle in FLATPAK_RUNTIME_BUNDLES.values()
            ),
        )

        self.service.extension_id_25_08 = "patched-extension-id"
        self.assertEqual(
            self.service._get_extension_id("25.08"),
            "patched-extension-id",
        )
        self.assertIsNone(self.service._get_extension_id("26.08"))

    def test_shell_tools_read_the_same_runtime_contract(self):
        helper = (
            Path(__file__).resolve().parents[1]
            / "scripts/read_flatpak_runtime_contract.py"
        )

        def read(field):
            return subprocess.run(
                [sys.executable, str(helper), field],
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()

        self.assertEqual(
            read("versions").splitlines(),
            list(SUPPORTED_FLATPAK_RUNTIME_VERSIONS),
        )
        self.assertEqual(
            read("bundles").splitlines(),
            [bundle.filename for bundle in FLATPAK_RUNTIME_BUNDLES.values()],
        )
        self.assertEqual(read("summary"), "23.08, 24.08, and 25.08")
        self.assertEqual(
            read("renderer-paths").splitlines(),
            [
                LIB_FILENAME,
                f"{LOCAL_LIB}/{LIB_FILENAME}",
                f"{LOCAL_LIB32}/{LIB_FILENAME}",
                SPATIAL_SCALING_LIB_FILENAME,
                f"{LOCAL_LIB}/{SPATIAL_SCALING_LIB_FILENAME}",
                f"{LOCAL_LIB32}/{SPATIAL_SCALING_LIB_FILENAME}",
                f"{SPATIAL_SCALING_LAYER_DIR}/{SPATIAL_SCALING_JSON_FILENAME}",
                f"{SPATIAL_SCALING_LAYER_DIR}/{SPATIAL_SCALING_JSON32_FILENAME}",
            ],
        )

    def test_renderer_and_decky_runtime_matrices_stay_aligned(self):
        renderer_matrix_dir = (
            Path(__file__).resolve().parents[2]
            / "engine/dist/flatpak/mako-render"
        )
        renderer_versions = tuple(
            line.strip()
            for line in (
                renderer_matrix_dir / "runtime-versions.txt"
            ).read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        )

        self.assertEqual(
            renderer_versions,
            SUPPORTED_FLATPAK_RUNTIME_VERSIONS,
        )
        llvm_versions = {
            "23.08": "18",
            "24.08": "20",
            "25.08": "21",
        }
        normalized_manifests = []
        for version in renderer_versions:
            with self.subTest(version=version):
                manifest = (
                    renderer_matrix_dir
                    / f"{FLATPAK_EXTENSION_NAME}_{version}.yml"
                ).read_text(encoding="utf-8")
                self.assertIn(f"id: {FLATPAK_EXTENSION_NAME}", manifest)
                self.assertIn(f"runtime-version: '{version}'", manifest)
                self.assertIn(f"branch: '{version}'", manifest)
                self.assertIn(f"prefix: {FLATPAK_EXTENSION_PREFIX}", manifest)
                self.assertEqual(
                    manifest.count(
                        "path: ../../../..\n"
                        "        skip:\n"
                        "          - engine/build"
                    ),
                    2,
                )
                self.assertIn(
                    f"MAKO_LAYER_LIBRARY_PATH={FLATPAK_EXTENSION_PREFIX}"
                    f"/lib64/{LIB_FILENAME}",
                    manifest,
                )
                self.assertIn(
                    f"MAKO_LAYER_LIBRARY_PATH={FLATPAK_EXTENSION_PREFIX}"
                    f"/lib/i386-linux-gnu/{LIB_FILENAME}",
                    manifest,
                )
                llvm_version = llvm_versions[version]
                normalized_manifests.append(
                    manifest.replace(version, "<runtime-version>")
                    .replace(f"llvm{llvm_version}", "llvm<toolchain-version>")
                )

        self.assertTrue(normalized_manifests)
        self.assertTrue(all(
            manifest == normalized_manifests[0]
            for manifest in normalized_manifests[1:]
        ))

    def test_runtime_status_preserves_public_fields_and_order(self):
        self.service.check_flatpak_available = lambda: True
        self.service._run_flatpak_command = lambda args, **_kwargs: (
            _result(
                f"{FLATPAK_EXTENSION_NAME}\tx86_64\t25.08\n"
                f"{FLATPAK_EXTENSION_NAME}\tx86_64\t23.08\n"
            )
            if args == ["list", "--runtime"]
            else self.fail(f"unexpected Flatpak command: {args}")
        )

        self.assertEqual(
            self.service.get_extension_status(),
            {
                "success": True,
                "message": (
                    "23.08 runtime extension installed; "
                    "25.08 runtime extension installed"
                ),
                "error": None,
                "installed_23_08": True,
                "installed_24_08": False,
                "installed_25_08": True,
            },
        )

    def test_runtime_status_failure_uses_the_same_generated_fields(self):
        self.service.check_flatpak_available = lambda: False
        self.service.flatpak_command = None

        response = self.service.get_extension_status()

        self.assertFalse(response["success"])
        self.assertEqual(
            {
                key: value
                for key, value in response.items()
                if key.startswith("installed_")
            },
            {
                f"installed_{version.replace('.', '_')}": False
                for version in SUPPORTED_FLATPAK_RUNTIME_VERSIONS
            },
        )

    def test_invalid_runtime_error_text_remains_stable(self):
        expected_error = (
            "Invalid version. Must be '23.08', '24.08', or '25.08'"
        )
        self.service._host_architecture_supported = lambda: True

        install = self.service.install_extension("26.08")
        uninstall = self.service.uninstall_extension("26.08")

        self.assertEqual(install["error"], expected_error)
        self.assertEqual(uninstall["error"], expected_error)

    def test_unsupported_host_cannot_install_or_activate_x86_flatpak_payload(self):
        self.service._host_architecture_supported = lambda: False
        self.service.check_flatpak_available = lambda: self.fail(
            "unsupported host must be rejected before invoking Flatpak"
        )

        extension = self.service.install_extension("25.08")
        override = self.service.set_app_override("org.example.Application")
        refresh = self.service.refresh_installed_extensions()

        self.assertFalse(extension["success"])
        self.assertFalse(override["success"])
        self.assertFalse(refresh["success"])
        self.assertIn("native AArch64/Armada", extension["error"])

    def test_direct_freedesktop_runtime_uses_its_branch(self):
        def run(args, **_kwargs):
            self.assertEqual(args, ["info", "--show-runtime", self.app_id])
            return _result("org.freedesktop.Platform/x86_64/25.08\n")

        self.service._run_flatpak_command = run

        self.assertEqual(self.service._get_app_runtime_version(self.app_id), "25.08")

    def test_kde_runtime_uses_inherited_freedesktop_vulkan_layer_version(self):
        kde_runtime = "org.kde.Platform/x86_64/6.10"
        kde_metadata = """[Runtime]
name=org.kde.Platform

[Extension org.freedesktop.Platform.GL]
version=25.08

[Extension org.freedesktop.Platform.VulkanLayer]
version=25.08
directory=lib/extensions/vulkan
subdirectories=true

[Extension org.kde.KStyle]
version=6.10
"""

        def run(args, **_kwargs):
            if args == ["info", "--show-runtime", self.app_id]:
                return _result(f"{kde_runtime}\n")
            if args == ["info", "--show-metadata", kde_runtime]:
                return _result(kde_metadata)
            self.fail(f"unexpected Flatpak command: {args}")

        self.service._run_flatpak_command = run

        self.assertEqual(self.service._get_app_runtime_version(self.app_id), "25.08")

    def test_inherited_vulkan_layer_version_must_be_supported(self):
        runtime = "org.kde.Platform/x86_64/6.11"
        metadata = """[Extension org.freedesktop.Platform.VulkanLayer]
version=26.08
"""

        def run(args, **_kwargs):
            if args == ["info", "--show-runtime", self.app_id]:
                return _result(f"{runtime}\n")
            if args == ["info", "--show-metadata", runtime]:
                return _result(metadata)
            self.fail(f"unexpected Flatpak command: {args}")

        self.service._run_flatpak_command = run

        self.assertIsNone(self.service._get_app_runtime_version(self.app_id))

    def test_metadata_parser_accepts_extension_version_lists(self):
        metadata = """[Extension org.freedesktop.Platform.VulkanLayer]
versions=26.08;25.08;24.08
"""

        self.assertEqual(
            self.service._get_inherited_vulkan_layer_version(metadata),
            "25.08",
        )

    def test_direct_flatpak_preparation_persists_config_and_layer_path(self):
        app_id = "org.DolphinEmu.dolphin-emu"
        with tempfile.TemporaryDirectory() as temp_dir:
            temporary_path = Path(temp_dir)
            wrapper = temporary_path / "mako-run"
            wrapper.touch()
            self.service.config_dir = temporary_path / "config"
            self.service.mako_launch_script_path = wrapper
            self.service.check_flatpak_available = lambda: True
            self.service._get_app_runtime_version = lambda _app_id: "25.08"
            self.service._is_extension_installed = lambda _version: True
            calls = []

            def run(args, **_kwargs):
                calls.append(args)
                return _result()

            self.service._run_flatpak_command = run

            response = self.service.set_app_override(app_id)

            self.assertTrue(response["success"])
            self.assertIn(
                [
                    "override",
                    "--user",
                    f"--env=MAKO_CONFIG={self.service.config_dir}/conf.toml",
                    app_id,
                ],
                calls,
            )
            self.assertIn(
                [
                    "override",
                    "--user",
                    f"--filesystem={self.service.gamescope_wsi_compatibility_dir}:ro",
                    app_id,
                ],
                calls,
            )
            self.assertIn(
                [
                    "override",
                    "--user",
                    f"--env=VK_IMPLICIT_LAYER_PATH={FLATPAK_IMPLICIT_LAYER_DIR}",
                    app_id,
                ],
                calls,
            )
            self.assertIn(
                [
                    "override",
                    "--user",
                    "--env=ENABLE_MAKO=1",
                    app_id,
                ],
                calls,
            )
            self.assertIn(
                [
                    "override",
                    "--user",
                    "--unset-env=VK_ADD_IMPLICIT_LAYER_PATH",
                    app_id,
                ],
                calls,
            )
            for variable in ("DISABLE_LSFG", "DISABLE_LSFGVK"):
                self.assertIn(
                    ["override", "--user", f"--env={variable}=1", app_id],
                    calls,
                )
            self.assertIn(
                [
                    "override",
                    "--user",
                    "--env=DISABLE_GAMESCOPE_WSI=1",
                    app_id,
                ],
                calls,
            )
            self.assertIn(
                [
                    "override",
                    "--user",
                    "--unset-env=ENABLE_GAMESCOPE_WSI",
                    app_id,
                ],
                calls,
            )
            self.assertIn(
                [
                    "override",
                    "--user",
                    "--env=MAKO_DISABLE_HDR_EXPOSURE=1",
                    app_id,
                ],
                calls,
            )
            self.assertIn(
                ["override", "--user", "--unset-env=DXVK_HDR", app_id],
                calls,
            )

    def test_legacy_runtime_uses_same_deterministic_layer_path(self):
        app_id = "org.DolphinEmu.dolphin-emu"
        with tempfile.TemporaryDirectory() as temp_dir:
            temporary_path = Path(temp_dir)
            wrapper = temporary_path / "mako-run"
            wrapper.touch()
            self.service.config_dir = temporary_path / "config"
            self.service.mako_launch_script_path = wrapper
            self.service.check_flatpak_available = lambda: True
            self.service._get_app_runtime_version = lambda _app_id: "24.08"
            self.service._is_extension_installed = lambda _version: True
            calls = []

            def run(args, **_kwargs):
                calls.append(args)
                return _result()

            self.service._run_flatpak_command = run

            response = self.service.set_app_override(app_id)

            self.assertTrue(response["success"])
            self.assertIn(
                [
                    "override",
                    "--user",
                    f"--env=VK_IMPLICIT_LAYER_PATH={FLATPAK_IMPLICIT_LAYER_DIR}",
                    app_id,
                ],
                calls,
            )
            self.assertIn(
                [
                    "override",
                    "--user",
                    "--unset-env=VK_ADD_IMPLICIT_LAYER_PATH",
                    app_id,
                ],
                calls,
            )

    def test_heroic_preparation_keeps_environment_per_game(self):
        app_id = "com.heroicgameslauncher.hgl"
        with tempfile.TemporaryDirectory() as temp_dir:
            temporary_path = Path(temp_dir)
            wrapper = temporary_path / "mako-run"
            wrapper.touch()
            self.service.config_dir = temporary_path / "config"
            self.service.mako_launch_script_path = wrapper
            self.service.check_flatpak_available = lambda: True
            self.service._get_app_runtime_version = lambda _app_id: "25.08"
            self.service._is_extension_installed = lambda _version: True
            calls = []

            def run(args, **_kwargs):
                calls.append(args)
                return _result()

            self.service._run_flatpak_command = run

            response = self.service.set_app_override(app_id)

            self.assertTrue(response["success"])
            self.assertIn(
                ["override", "--user", "--unset-env=MAKO_CONFIG", app_id],
                calls,
            )
            self.assertNotIn(
                [
                    "override",
                    "--user",
                    f"--env=MAKO_CONFIG={self.service.config_dir}/conf.toml",
                    app_id,
                ],
                calls,
            )
            for variable in (
                "MAKO_CONFIG",
                "ENABLE_MAKO",
                "DISABLE_LSFG",
                "DISABLE_LSFGVK",
                "DISABLE_GAMESCOPE_WSI",
                "ENABLE_GAMESCOPE_WSI",
                "MAKO_DISABLE_HDR_EXPOSURE",
                "DXVK_HDR",
                "VK_IMPLICIT_LAYER_PATH",
                "VK_ADD_IMPLICIT_LAYER_PATH",
            ):
                self.assertIn(
                    ["override", "--user", f"--unset-env={variable}", app_id],
                    calls,
                )

    def test_direct_override_status_requires_complete_activation_environment(self):
        app_id = "org.DolphinEmu.dolphin-emu"
        self.service._get_mako_paths = lambda: ("/config", "/dll")
        self.service.mako_launch_script_path = Path("/wrapper")
        self.service.gamescope_wsi_compatibility_dir = Path("/wsi")

        complete_override = """[Context]
filesystems=/config;/dll;/wrapper;/wsi;

[Environment]
MAKO_CONFIG=/config/conf.toml
ENABLE_MAKO=1
DISABLE_LSFG=1
DISABLE_LSFGVK=1
DISABLE_GAMESCOPE_WSI=1
MAKO_DISABLE_HDR_EXPOSURE=1
VK_IMPLICIT_LAYER_PATH=/usr/lib/extensions/vulkan/makorender/share/vulkan/implicit_layer.d
"""
        self.service._run_flatpak_command = lambda _args, **_kwargs: _result(
            complete_override
        )

        status = self.service._check_app_override_status(app_id)

        self.assertTrue(status["filesystem"])
        self.assertTrue(status["wrapper"])
        self.assertTrue(status["required_env"])

        incomplete_override = complete_override.replace(
            "ENABLE_MAKO=1\n", ""
        )
        self.service._run_flatpak_command = lambda _args, **_kwargs: _result(
            incomplete_override
        )

        status = self.service._check_app_override_status(app_id)

        self.assertTrue(status["legacy_env"])
        self.assertFalse(status["required_env"])

        legacy_additive_path = complete_override.replace(
            f"VK_IMPLICIT_LAYER_PATH={FLATPAK_IMPLICIT_LAYER_DIR}",
            f"VK_ADD_IMPLICIT_LAYER_PATH={FLATPAK_IMPLICIT_LAYER_DIR}",
        )
        self.service._run_flatpak_command = lambda _args, **_kwargs: _result(
            legacy_additive_path
        )

        status = self.service._check_app_override_status(app_id)

        self.assertFalse(status["required_env"])

        missing_hdr_boundary = complete_override.replace(
            "MAKO_DISABLE_HDR_EXPOSURE=1\n", ""
        )
        self.service._run_flatpak_command = lambda _args, **_kwargs: _result(
            missing_hdr_boundary
        )

        status = self.service._check_app_override_status(app_id)

        self.assertFalse(status["required_env"])

        overriding_path = complete_override.replace(
            FLATPAK_IMPLICIT_LAYER_DIR,
            f"/legacy/isolated/path:{FLATPAK_IMPLICIT_LAYER_DIR}",
        )
        self.service._run_flatpak_command = lambda _args, **_kwargs: _result(
            overriding_path
        )

        status = self.service._check_app_override_status(app_id)

        self.assertTrue(status["legacy_env"])
        self.assertFalse(status["required_env"])

        missing_gamescope_guard = complete_override.replace(
            "DISABLE_GAMESCOPE_WSI=1\n", ""
        )
        self.service._run_flatpak_command = lambda _args, **_kwargs: _result(
            missing_gamescope_guard
        )

        status = self.service._check_app_override_status(app_id)

        self.assertFalse(status["required_env"])

    def test_unsupported_host_cleanup_removes_only_mako_owned_overrides(self):
        self.service._host_architecture_supported = lambda: False
        self.service.check_flatpak_available = lambda: True
        self.service._run_flatpak_command = lambda args, **_kwargs: (
            _result(
                "MAKO game\torg.example.MakoGame\n"
                "Other game\torg.example.OtherGame\n"
            )
            if args == ["list", "--app"]
            else self.fail(f"unexpected Flatpak command: {args}")
        )
        self.service._check_app_override_status = lambda app_id: {
            "filesystem": False,
            "wrapper": False,
            "legacy_env": True,
            "mako_env": app_id == "org.example.MakoGame",
            "required_env": False,
        }
        removed = []
        self.service.remove_app_override = lambda app_id: (
            removed.append(app_id)
            or {"success": True, "app_id": app_id}
        )

        response = self.service.disable_incompatible_host_overrides()

        self.assertTrue(response["success"])
        self.assertEqual(removed, ["org.example.MakoGame"])
        self.assertEqual(response["disabled_apps"], ["org.example.MakoGame"])

    def test_competitor_only_flatpak_override_is_not_mako_owned(self):
        self.service._run_flatpak_command = lambda _args, **_kwargs: _result(
            "[Environment]\nDISABLE_LSFG=1\nDISABLE_LSFGVK=1\n"
        )

        status = self.service._check_app_override_status(
            "org.example.OtherGame"
        )

        self.assertTrue(status["legacy_env"])
        self.assertFalse(status["mako_env"])

    def test_removing_direct_override_clears_all_layer_environment(self):
        app_id = "org.DolphinEmu.dolphin-emu"
        with tempfile.TemporaryDirectory() as temp_dir:
            temporary_path = Path(temp_dir)
            self.service.config_dir = temporary_path / "config"
            self.service.mako_launch_script_path = temporary_path / "wrapper"
            self.service.check_flatpak_available = lambda: True
            calls = []

            def run(args, **_kwargs):
                calls.append(args)
                return _result()

            self.service._run_flatpak_command = run

            response = self.service.remove_app_override(app_id)

            self.assertTrue(response["success"])
            for variable in (
                "MAKO_CONFIG",
                "ENABLE_MAKO",
                "DISABLE_LSFG",
                "DISABLE_LSFGVK",
                "DISABLE_GAMESCOPE_WSI",
                "ENABLE_GAMESCOPE_WSI",
                "MAKO_DISABLE_HDR_EXPOSURE",
                "DXVK_HDR",
                "VK_IMPLICIT_LAYER_PATH",
                "VK_ADD_IMPLICIT_LAYER_PATH",
            ):
                self.assertIn(
                    ["override", "--user", f"--unset-env={variable}", app_id],
                    calls,
                )

    def test_removal_reports_partial_cleanup_failure(self):
        app_id = "org.DolphinEmu.dolphin-emu"
        self.service.check_flatpak_available = lambda: True

        def run(args, **_kwargs):
            if "--unset-env=ENABLE_MAKO" in args:
                return _result(stderr="override failed", returncode=1)
            return _result()

        self.service._run_flatpak_command = run

        response = self.service.remove_app_override(app_id)

        self.assertFalse(response["success"])
        self.assertIn("ENABLE_MAKO", response["error"])

    def test_refresh_reinstalls_only_existing_runtime_branches(self):
        self.service.check_flatpak_available = lambda: True
        self.service._is_extension_installed = lambda version: version in {
            "24.08", "25.08"
        }
        refreshed = []

        def install(version):
            refreshed.append(version)
            return {"success": True, "message": "updated"}

        self.service.install_extension = install

        response = self.service.refresh_installed_extensions()

        self.assertTrue(response["success"])
        self.assertEqual(refreshed, ["24.08", "25.08"])
        self.assertEqual(response["updated_versions"], ["24.08", "25.08"])

    def test_refresh_reports_a_partial_runtime_failure(self):
        self.service.check_flatpak_available = lambda: True
        self.service._is_extension_installed = lambda _version: True
        self.service.install_extension = lambda version: (
            {"success": False, "error": "broken bundle"}
            if version == "24.08"
            else {"success": True, "message": "updated"}
        )

        response = self.service.refresh_installed_extensions()

        self.assertFalse(response["success"])
        self.assertEqual(response["updated_versions"], ["23.08", "25.08"])
        self.assertIn("24.08: broken bundle", response["error"])
