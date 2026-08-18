"""Regression tests for Decky user-home and generated command portability."""

import asyncio
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


class _Logger:
    def __getattr__(self, _name):
        return lambda *_args, **_kwargs: None


sys.modules.setdefault("decky", SimpleNamespace(logger=_Logger()))

import decky  # noqa: E402
from py_modules.mako_plugin.base_service import BaseService  # noqa: E402
from py_modules.mako_plugin.dll_detection import DllDetectionService  # noqa: E402
from py_modules.mako_plugin.flatpak_service import FlatpakService  # noqa: E402
from py_modules.mako_plugin.plugin import Plugin  # noqa: E402


class PortableHomeTests(unittest.TestCase):
    def setUp(self):
        self.previous_decky_home = getattr(decky, "DECKY_USER_HOME", None)
        self.had_decky_home = hasattr(decky, "DECKY_USER_HOME")

    def tearDown(self):
        if self.had_decky_home:
            decky.DECKY_USER_HOME = self.previous_decky_home
        elif hasattr(decky, "DECKY_USER_HOME"):
            delattr(decky, "DECKY_USER_HOME")

    def test_services_prefer_decky_user_home_over_process_home(self):
        with tempfile.TemporaryDirectory() as temporary_home:
            decky.DECKY_USER_HOME = temporary_home
            with patch.dict(os.environ, {"HOME": "/home/wrong-service-user"}):
                service = BaseService(logger=_Logger())

            expected_home = Path(temporary_home)
            self.assertEqual(service.user_home, expected_home)
            self.assertEqual(
                service.mako_launch_script_path,
                expected_home / ".local" / "bin" / "mako-run",
            )

    def test_dll_detection_uses_the_decky_users_steam_library(self):
        with tempfile.TemporaryDirectory() as temporary_home:
            decky.DECKY_USER_HOME = temporary_home
            dll_path = (
                Path(temporary_home)
                / ".local/share/Steam/steamapps/common/Lossless Scaling/Lossless.dll"
            )
            dll_path.parent.mkdir(parents=True)
            dll_path.touch()

            with patch.dict(os.environ, {"HOME": "/home/wrong-service-user"}):
                result = DllDetectionService(
                    logger=_Logger()
                ).check_lossless_scaling_dll()

            self.assertTrue(result["detected"])
            self.assertEqual(result["path"], str(dll_path))

    def test_flatpak_discovery_uses_the_decky_users_local_bin(self):
        with tempfile.TemporaryDirectory() as temporary_home:
            decky.DECKY_USER_HOME = temporary_home
            service = FlatpakService(logger=_Logger())
            expected_flatpak = str(Path(temporary_home) / ".local/bin/flatpak")
            attempts = []

            def run(command, **_kwargs):
                attempts.append(command[0])
                if command[0] == expected_flatpak:
                    return SimpleNamespace(stdout="Flatpak 1.16.0\n")
                raise FileNotFoundError(command[0])

            with patch("subprocess.run", side_effect=run):
                self.assertTrue(service.check_flatpak_available())

            self.assertEqual(service.flatpak_command, expected_flatpak)
            self.assertIn(expected_flatpak, attempts)
            self.assertNotIn("/home/deck/.local/bin/flatpak", attempts)

    def test_launch_commands_are_generated_from_the_installed_wrapper(self):
        plugin = Plugin.__new__(Plugin)
        plugin.installation_service = SimpleNamespace(
            get_launch_script_path=lambda: "/var/home/test user/.local/bin/mako-run"
        )

        result = asyncio.run(plugin.get_launch_option())

        self.assertEqual(
            result["launch_option"],
            "'/var/home/test user/.local/bin/mako-run' %command%",
        )
        self.assertEqual(
            result["wrapper_path"],
            "/var/home/test user/.local/bin/mako-run",
        )


if __name__ == "__main__":
    unittest.main()
