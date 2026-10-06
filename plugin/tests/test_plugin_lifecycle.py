"""Regression tests for the Decky plugin lifecycle."""

import asyncio
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock, Mock, patch
from pathlib import Path
import tempfile


class _Logger:
    def __getattr__(self, _name):
        return lambda *_args, **_kwargs: None


sys.modules.setdefault("decky", SimpleNamespace(logger=_Logger()))

from py_modules.mako_plugin.plugin import Plugin  # noqa: E402


class PluginLifecycleTests(unittest.TestCase):
    def test_main_runs_every_current_startup_maintenance_task(self):
        calls = []
        plugin = Plugin.__new__(Plugin)
        plugin._start_flatpak_vrr_monitor = AsyncMock()
        plugin.configuration_service = SimpleNamespace(
            enforce_unsupported_host_passthrough_if_needed=lambda: calls.append(
                "host-passthrough"
            ) or False,
            migrate_profile_metadata_if_needed=lambda: calls.append(
                "profile-metadata"
            ) or False,
            migrate_launch_script_if_needed=lambda: calls.append(
                "launch-script"
            ) or False,
        )
        plugin.installation_service = SimpleNamespace(
            current_package_host_compatibility=lambda: (
                "x86_64", True, None
            ),
            prepare_active_standalone_for_decky=lambda: calls.append(
                "standalone-adoption"
            ) or False,
            migrate_gamescope_wsi_compatibility_manifest_if_needed=lambda: calls.append(
                "gamescope-wsi-manifest"
            ) or False,
            refresh_guarded_postprocess_manifests_if_needed=lambda: calls.append(
                "postprocess-manifests"
            ) or False,
            migrate_diagnostics_helper_if_needed=lambda: calls.append(
                "diagnostics-helper"
            ) or False,
        )
        plugin.flatpak_service = SimpleNamespace(
            disable_incompatible_host_overrides=lambda: calls.append(
                "flatpak-host-boundary"
            ) or {"success": True, "disabled_apps": []},
        )

        plugin.remote_play_service = SimpleNamespace(
            refresh_installed_payload=lambda: calls.append("remote-play-payload") or False,
        )
        async def inline(function, *arguments):
            return function(*arguments)
        with patch("py_modules.mako_plugin.plugin.asyncio.to_thread", inline):
            asyncio.run(plugin._main())
        plugin._start_flatpak_vrr_monitor.assert_awaited_once()

        self.assertEqual(calls, [
            "profile-metadata",
            "launch-script",
            "standalone-adoption",
            "gamescope-wsi-manifest",
            "postprocess-manifests",
            "diagnostics-helper",
            "remote-play-payload",
        ])

    def test_main_stops_before_migrations_on_unsupported_host(self):
        calls = []
        plugin = Plugin.__new__(Plugin)
        plugin._start_flatpak_vrr_monitor = AsyncMock()
        plugin.configuration_service = SimpleNamespace(
            enforce_unsupported_host_passthrough_if_needed=lambda: calls.append(
                "host-passthrough"
            ) or True,
            migrate_profile_metadata_if_needed=lambda: calls.append(
                "profile-metadata"
            ),
        )
        plugin.installation_service = SimpleNamespace(
            current_package_host_compatibility=lambda: (
                "aarch64", False, "unsupported"
            ),
        )
        plugin.flatpak_service = SimpleNamespace(
            disable_incompatible_host_overrides=lambda: calls.append(
                "flatpak-host-boundary"
            ) or {
                "success": True,
                "disabled_apps": ["org.example.Game"],
            },
        )

        asyncio.run(plugin._main())
        plugin._start_flatpak_vrr_monitor.assert_not_called()

        self.assertEqual(calls, [
            "host-passthrough",
            "flatpak-host-boundary",
        ])

    def test_monitor_lifecycle_is_idempotent_and_does_not_kill_game_leases(self):
        async def exercise():
            with tempfile.TemporaryDirectory() as temporary:
                home = Path(temporary)
                helper = home / "mako-vrr-lease"
                helper.touch()
                plugin = Plugin.__new__(Plugin)
                plugin.installation_service = SimpleNamespace(user_home=home, vrr_lease_file=helper)
                plugin.configuration_service = SimpleNamespace(config_file_path=home / "conf.toml")
                process = Mock(returncode=None, wait=AsyncMock(return_value=0))
                with patch("asyncio.create_subprocess_exec", AsyncMock(return_value=process)) as spawn:
                    await plugin._start_flatpak_vrr_monitor()
                    await plugin._start_flatpak_vrr_monitor()
                    spawn.assert_awaited_once()
                    self.assertEqual(spawn.call_args.args, (str(helper), "--watch-flatpak"))
                    self.assertEqual(spawn.call_args.kwargs["env"]["HOME"], str(home))
                    await plugin._unload()
                    await plugin._stop_flatpak_vrr_monitor()
                    process.terminate.assert_called_once()
                    process.kill.assert_not_called()
        asyncio.run(exercise())


if __name__ == "__main__":
    unittest.main()
