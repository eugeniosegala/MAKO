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
            migrate_profile_metadata_if_needed=lambda: calls.append(
                "profile-metadata"
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
            migrate_diagnostics_helper_if_needed=lambda: calls.append(
                "diagnostics-helper"
            ) or False,
        )

        asyncio.run(plugin._main())

        self.assertEqual(calls, [
            "profile-metadata",
            "wrapper-settings",
            "base-fps-cap",
            "vkbasalt",
            "launch-script",
            "diagnostics-helper",
        ])


if __name__ == "__main__":
    unittest.main()
