"""Tests for coordinated host and Flatpak Renderer installation."""

import sys
from types import SimpleNamespace
import unittest


class _Logger:
    def __getattr__(self, _name):
        return lambda *_args, **_kwargs: None


sys.modules.setdefault("decky", SimpleNamespace(logger=_Logger()))

from py_modules.mako_plugin.plugin import Plugin  # noqa: E402


class PluginInstallationTests(unittest.IsolatedAsyncioTestCase):
    async def test_install_refreshes_existing_flatpak_runtimes(self):
        plugin = Plugin.__new__(Plugin)
        plugin.installation_service = SimpleNamespace(
            install=lambda: {
                "success": True,
                "message": "MAKO Renderer installed successfully",
                "error": None,
            },
            log=_Logger(),
        )
        plugin.flatpak_service = SimpleNamespace(
            refresh_installed_extensions=lambda: {
                "success": True,
                "updated_versions": ["24.08", "25.08"],
            }
        )

        result = await plugin.install_mako()

        self.assertTrue(result["success"])
        self.assertEqual(
            result["flatpak_extensions_updated"], ["24.08", "25.08"]
        )
        self.assertIn("refreshed Flatpak runtimes 24.08, 25.08", result["message"])

    async def test_flatpak_refresh_failure_keeps_host_install_successful(self):
        plugin = Plugin.__new__(Plugin)
        plugin.installation_service = SimpleNamespace(
            install=lambda: {
                "success": True,
                "message": "MAKO Renderer installed successfully",
                "error": None,
            },
            log=_Logger(),
        )
        plugin.flatpak_service = SimpleNamespace(
            refresh_installed_extensions=lambda: {
                "success": False,
                "updated_versions": ["25.08"],
                "error": "24.08: broken bundle",
            }
        )

        result = await plugin.install_mako()

        self.assertTrue(result["success"])
        self.assertEqual(result["flatpak_extensions_updated"], ["25.08"])
        self.assertEqual(result["flatpak_refresh_error"], "24.08: broken bundle")
        self.assertIn("Flatpak Setup", result["message"])

    async def test_failed_host_install_does_not_touch_flatpak(self):
        plugin = Plugin.__new__(Plugin)
        plugin.installation_service = SimpleNamespace(
            install=lambda: {
                "success": False,
                "message": "",
                "error": "bad archive",
            },
            log=_Logger(),
        )

        class _UnexpectedFlatpakService:
            def refresh_installed_extensions(self):
                raise AssertionError("Flatpak refresh should not run")

        plugin.flatpak_service = _UnexpectedFlatpakService()

        result = await plugin.install_mako()

        self.assertFalse(result["success"])
        self.assertEqual(result["error"], "bad archive")


if __name__ == "__main__":
    unittest.main()
