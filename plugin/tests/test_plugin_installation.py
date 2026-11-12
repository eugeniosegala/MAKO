"""Tests for coordinated host and Flatpak Renderer installation."""

import asyncio
import sys
from pathlib import Path
import tempfile
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock, Mock, patch


class _Logger:
    def __getattr__(self, _name):
        return lambda *_args, **_kwargs: None


sys.modules.setdefault("decky", SimpleNamespace(logger=_Logger()))

from py_modules.mako_plugin.plugin import Plugin  # noqa: E402
from py_modules.mako_plugin.installation import InstallationService  # noqa: E402


class PluginInstallationTests(unittest.IsolatedAsyncioTestCase):
    def plugin(self):
        plugin = Plugin.__new__(Plugin)
        plugin._start_flatpak_vrr_monitor = AsyncMock()
        plugin._stop_flatpak_vrr_monitor = AsyncMock()
        return plugin

    async def test_install_refreshes_existing_flatpak_runtimes(self):
        plugin = self.plugin()
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
        plugin._stop_flatpak_vrr_monitor.assert_awaited_once()
        plugin._start_flatpak_vrr_monitor.assert_awaited_once()
        self.assertEqual(
            result["flatpak_extensions_updated"], ["24.08", "25.08"]
        )
        self.assertIn("refreshed Flatpak runtimes 24.08, 25.08", result["message"])

    async def test_flatpak_refresh_failure_keeps_host_install_successful(self):
        plugin = self.plugin()
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
        plugin = self.plugin()
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
        plugin._stop_flatpak_vrr_monitor.assert_not_called()
        plugin._start_flatpak_vrr_monitor.assert_not_called()
        self.assertEqual(result["error"], "bad archive")

    async def test_stale_flatpak_runtime_offers_the_renderer_update(self):
        for installed, native_update, stale, expected in (
            (True, False, ["26.08"], True),
            (True, False, [], False),
            (True, True, [], True),
            (False, False, ["26.08"], False),
        ):
            with self.subTest(installed=installed, native_update=native_update, stale=stale):
                plugin = self.plugin()
                plugin.installation_service = SimpleNamespace(
                    check_installation=lambda: {
                        "installed": installed,
                        "engine_update_required": native_update,
                    },
                )
                checks = []
                plugin.flatpak_service = SimpleNamespace(
                    stale_runtime_versions=lambda: checks.append(True) or stale,
                )

                status = await plugin.check_mako_installed()

                self.assertEqual(status["engine_update_required"], expected)
                # A missing or already outdated Renderer needs no Flatpak query.
                self.assertEqual(bool(checks), installed and not native_update)

    async def test_busy_native_uninstall_leaves_remote_play_and_monitor_untouched(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch("py_modules.mako_plugin.base_service.resolve_user_home", return_value=Path(temporary)):
                service = InstallationService(logger=_Logger())
            plugin = self.plugin()
            plugin.installation_service = service
            plugin.remote_play_service = SimpleNamespace(restore_before_uninstall=Mock())
            with service.native_installation_lock():
                result = await plugin.uninstall_mako()
            self.assertFalse(result["success"])
            self.assertIn("in progress", result["error"])
            plugin.remote_play_service.restore_before_uninstall.assert_not_called()
            plugin._stop_flatpak_vrr_monitor.assert_not_awaited()

    async def test_native_uninstall_holds_lock_through_restore_and_monitor_shutdown(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch("py_modules.mako_plugin.base_service.resolve_user_home", return_value=Path(temporary)):
                service = InstallationService(logger=_Logger())
            service.lib_file.parent.mkdir(parents=True)
            service.lib_file.write_bytes(b"managed payload")
            def assert_locked():
                self.assertTrue(service.lib_file.exists())
                with self.assertRaisesRegex(OSError, "in progress"):
                    with service.native_installation_lock():
                        self.fail("native lock was released before cleanup")
            plugin = self.plugin()
            plugin.installation_service = service
            plugin.remote_play_service = SimpleNamespace(restore_before_uninstall=Mock(side_effect=assert_locked))
            plugin._stop_flatpak_vrr_monitor.side_effect = assert_locked
            result = await plugin.uninstall_mako()
            self.assertTrue(result["success"], result)
            plugin.remote_play_service.restore_before_uninstall.assert_called_once()
            plugin._stop_flatpak_vrr_monitor.assert_awaited_once()
            self.assertFalse(service.lib_file.exists())
            with service.native_installation_lock():
                pass

    async def test_cancelled_native_uninstall_retains_lock_until_restore_worker_finishes(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch("py_modules.mako_plugin.base_service.resolve_user_home", return_value=Path(temporary)):
                service = InstallationService(logger=_Logger())
            service.lib_file.parent.mkdir(parents=True)
            service.lib_file.write_bytes(b"managed payload")
            started, release = threading.Event(), threading.Event()
            def restore():
                started.set()
                if not release.wait(5):
                    raise TimeoutError("restoration fixture was not released")
            plugin = self.plugin()
            plugin.installation_service = service
            plugin.remote_play_service = SimpleNamespace(restore_before_uninstall=restore)
            removal = asyncio.create_task(plugin.uninstall_mako())
            try:
                self.assertTrue(await asyncio.to_thread(started.wait, 2))
                for _ in range(2):
                    removal.cancel()
                    await asyncio.sleep(0)
                    self.assertFalse(removal.done())
                    with self.assertRaisesRegex(OSError, "in progress"):
                        with service.native_installation_lock():
                            self.fail("native lease was released during restoration")
            finally:
                release.set()
                with self.assertRaises(asyncio.CancelledError):
                    await removal
            self.assertTrue(service.lib_file.exists())
            plugin._stop_flatpak_vrr_monitor.assert_not_awaited()
            with service.native_installation_lock():
                pass


if __name__ == "__main__":
    unittest.main()
