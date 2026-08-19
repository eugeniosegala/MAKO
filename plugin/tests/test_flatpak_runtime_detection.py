"""Tests for Flatpak runtime-to-Vulkan-layer compatibility detection."""

import sys
import tempfile
from types import SimpleNamespace
import unittest
from pathlib import Path


class _Logger:
    def __getattr__(self, _name):
        return lambda *_args, **_kwargs: None


sys.modules.setdefault("decky", SimpleNamespace(logger=_Logger()))

from py_modules.mako_plugin.flatpak_service import FlatpakService  # noqa: E402


def _result(stdout="", stderr="", returncode=0):
    return SimpleNamespace(stdout=stdout, stderr=stderr, returncode=returncode)


class FlatpakRuntimeDetectionTests(unittest.TestCase):
    app_id = "org.example.Application"

    def setUp(self):
        self.service = FlatpakService(logger=_Logger())

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
                    "--unset-env=VK_IMPLICIT_LAYER_PATH",
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
                    "--env=VK_ADD_IMPLICIT_LAYER_PATH="
                    "/usr/lib/extensions/vulkan/makorender/share/"
                    "vulkan/implicit_layer.d",
                    app_id,
                ],
                calls,
            )
            for variable in ("DISABLE_LSFG", "DISABLE_LSFGVK"):
                self.assertIn(
                    ["override", "--user", f"--env={variable}=1", app_id],
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

        complete_override = """[Context]
filesystems=/config;/dll;/wrapper;

[Environment]
MAKO_CONFIG=/config/conf.toml
ENABLE_MAKO=1
DISABLE_LSFG=1
DISABLE_LSFGVK=1
VK_ADD_IMPLICIT_LAYER_PATH=/usr/lib/extensions/vulkan/makorender/share/vulkan/implicit_layer.d
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

        overriding_path = complete_override.replace(
            "[Environment]\n",
            "[Environment]\nVK_IMPLICIT_LAYER_PATH=/legacy/isolated/path\n",
        )
        self.service._run_flatpak_command = lambda _args, **_kwargs: _result(
            overriding_path
        )

        status = self.service._check_app_override_status(app_id)

        self.assertTrue(status["legacy_env"])
        self.assertFalse(status["required_env"])

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
