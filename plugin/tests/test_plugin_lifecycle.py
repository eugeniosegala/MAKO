"""Regression tests for the Decky plugin lifecycle."""

import asyncio
import sys
from types import SimpleNamespace
import unittest


class _Logger:
    def __getattr__(self, _name):
        return lambda *_args, **_kwargs: None


sys.modules.setdefault("decky", SimpleNamespace(logger=_Logger()))

from py_modules.mako_plugin.plugin import Plugin  # noqa: E402


class PluginLifecycleTests(unittest.TestCase):
    def test_main_runs_every_current_startup_migration(self):
        calls = []
        plugin = Plugin.__new__(Plugin)
        plugin.configuration_service = SimpleNamespace(
            enforce_unsupported_host_passthrough_if_needed=lambda: calls.append(
                "host-passthrough"
            ) or False,
            migrate_profile_metadata_if_needed=lambda: calls.append(
                "profile-metadata"
            ) or False,
            sanitize_captured_processes_if_needed=lambda: calls.append(
                "captured-processes"
            ) or False,
            migrate_wrapper_profile_settings_if_needed=lambda: calls.append(
                "wrapper-settings"
            ) or False,
            migrate_legacy_base_fps_caps_if_needed=lambda: calls.append(
                "base-fps-cap"
            ) or False,
            remove_legacy_vkbasalt_exports=lambda: calls.append(
                "vkbasalt"
            ) or False,
            migrate_launch_script_if_needed=lambda: calls.append(
                "launch-script"
            ) or False,
        )
        plugin.installation_service = SimpleNamespace(
            current_package_host_compatibility=lambda: (
                "x86_64", True, None
            ),
            migrate_diagnostics_helper_if_needed=lambda: calls.append(
                "diagnostics-helper"
            ) or False,
        )
        plugin.flatpak_service = SimpleNamespace(
            disable_incompatible_host_overrides=lambda: calls.append(
                "flatpak-host-boundary"
            ) or {"success": True, "disabled_apps": []},
        )

        asyncio.run(plugin._main())

        self.assertEqual(calls, [
            "profile-metadata",
            "captured-processes",
            "wrapper-settings",
            "base-fps-cap",
            "vkbasalt",
            "launch-script",
            "diagnostics-helper",
        ])

    def test_main_stops_before_migrations_on_unsupported_host(self):
        calls = []
        plugin = Plugin.__new__(Plugin)
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

        self.assertEqual(calls, [
            "host-passthrough",
            "flatpak-host-boundary",
        ])


if __name__ == "__main__":
    unittest.main()
