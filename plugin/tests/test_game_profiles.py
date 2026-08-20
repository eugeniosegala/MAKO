"""Regression tests for persistent game/process profiles and isolation."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest


class _Logger:
    def __getattr__(self, _name):
        return lambda *_args, **_kwargs: None


sys.modules.setdefault("decky", SimpleNamespace(logger=_Logger()))

from py_modules.mako_plugin import configuration as configuration_module  # noqa: E402
from py_modules.mako_plugin.config_schema import (  # noqa: E402
    ConfigurationManager,
    ProfileData,
)
from py_modules.mako_plugin.configuration import ConfigurationService  # noqa: E402
from py_modules.mako_plugin.process_detection import (  # noqa: E402
    detect_processes_for_steam_app,
)


class GameProfileTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp_dir.cleanup)
        root = Path(self.temp_dir.name)
        self.service = ConfigurationService(logger=_Logger(), development_build=False)
        self.service.config_dir = root / "config"
        self.service.config_file_path = self.service.config_dir / "conf.toml"
        self.service.wrapper_profile_settings_path = (
            self.service.config_dir / "profile-wrapper-settings.json"
        )
        self.service.profile_metadata_path = (
            self.service.config_dir / "profile-metadata.json"
        )
        self.service.mako_script_path = root / "bin" / "mako-run"
        self.service.local_share_dir = root / "implicit_layer.d"
        self.service.mako_script_path.parent.mkdir(parents=True)

        defaults = ConfigurationManager.get_defaults()
        self.profile_data = ProfileData(
            current_profile="mako",
            profiles={"mako": defaults},
            global_config={
                "dll": defaults["dll"],
                "allow_fp16": defaults["allow_fp16"],
            },
        )
        self.service._save_profile_data(self.profile_data)

    def test_initial_metadata_migration_is_idempotent_and_keeps_renderer_config(self):
        original = self.service.config_file_path.read_text(encoding="utf-8")

        self.assertTrue(self.service.migrate_profile_metadata_if_needed())
        self.assertFalse(self.service.migrate_profile_metadata_if_needed())

        payload = json.loads(self.service.profile_metadata_path.read_text(encoding="utf-8"))
        self.assertEqual(payload["version"], 1)
        self.assertEqual(payload["profiles"]["mako"]["kind"], "default")
        self.assertEqual(
            self.service.config_file_path.read_text(encoding="utf-8"), original
        )

    def test_unknown_profile_options_are_inert_then_removed_on_canonical_save(self):
        content = "\n".join([
            "version = 2",
            '# decky-current-profile = "mako"',
            "[global]",
            "allow_fp16 = true",
            'removed_global_option = "inert"',
            "[[profile]]",
            'name = "mako"',
            "multiplier = 3",
            "removed_profile_option = true",
            "",
        ])

        profile_data = ConfigurationManager.parse_toml_content_multi_profile(content)

        self.assertEqual(profile_data["profiles"]["mako"]["multiplier"], 3)
        self.assertNotIn("removed_profile_option", profile_data["profiles"]["mako"])
        self.assertNotIn("removed_global_option", profile_data["global_config"])

        rewritten = ConfigurationManager.generate_toml_content_multi_profile(
            profile_data
        )
        self.assertNotIn("removed_profile_option", rewritten)
        self.assertNotIn("removed_global_option", rewritten)

    def test_rpc_validation_discards_unknown_profile_options(self):
        validated = ConfigurationManager.validate_config({
            **ConfigurationManager.get_defaults(),
            "removed_profile_option": True,
        })

        self.assertNotIn("removed_profile_option", validated)

    def test_capture_creates_then_updates_one_profile_for_the_same_app(self):
        previous = configuration_module.detect_processes_for_steam_app
        detected_processes = ["CoolGame.exe"]
        configuration_module.detect_processes_for_steam_app = (
            lambda _app_id: list(detected_processes)
        )
        try:
            created = self.service.capture_game_profile("12345", "Cool Game")
            captured_config = self.service._get_profile_data()["profiles"]["Cool-Game"]
            captured_config["active_in"] = "CoolGame.exe, UserAlias.exe"
            self.service.update_profile_config("Cool-Game", captured_config)
            detected_processes[:] = ["CoolGame2.exe"]
            updated = self.service.capture_game_profile("12345", "Cool Game")
        finally:
            configuration_module.detect_processes_for_steam_app = previous

        self.assertTrue(created["success"])
        self.assertTrue(updated["success"])
        self.assertEqual(created["profile_name"], updated["profile_name"])
        profiles = self.service.get_profiles()
        self.assertEqual(profiles["profiles"], ["mako", "Cool-Game"])
        detail = profiles["profile_details"][1]
        self.assertEqual(detail["steam_app_id"], "12345")
        self.assertEqual(detail["processes"], ["UserAlias.exe", "CoolGame2.exe"])

    def test_named_profile_can_be_loaded_without_activating_it(self):
        created = self.service.create_profile("Offline Editor")
        self.assertTrue(created["success"])
        profile_name = created["profile_name"]
        profile = self.service._get_profile_data()["profiles"][profile_name]
        profile["multiplier"] = 4
        self.assertTrue(
            self.service.update_profile_config(profile_name, profile)["success"]
        )
        self.assertTrue(self.service.mako_script_path.exists())

        loaded = self.service.get_profile_config(profile_name)

        self.assertTrue(loaded["success"])
        self.assertEqual(loaded["config"]["multiplier"], 4)
        self.assertEqual(
            self.service._get_profile_data()["current_profile"], "mako"
        )

    def test_unknown_named_profile_does_not_change_runtime_selection(self):
        loaded = self.service.get_profile_config("missing-profile")

        self.assertFalse(loaded["success"])
        self.assertEqual(
            self.service._get_profile_data()["current_profile"], "mako"
        )

    def test_profile_sync_reselects_saved_game_then_restores_default(self):
        previous = configuration_module.detect_processes_for_steam_app
        detected_processes = ["CoolGame.exe"]
        configuration_module.detect_processes_for_steam_app = (
            lambda _app_id: list(detected_processes)
        )
        try:
            created = self.service.capture_game_profile("12345", "Cool Game")
            stopped = self.service.sync_current_profile("")
            resumed = self.service.sync_current_profile("12345")
            unchanged = self.service.sync_current_profile("12345")
            detected_processes.clear()
            exited = self.service.sync_current_profile("12345")
        finally:
            configuration_module.detect_processes_for_steam_app = previous

        self.assertTrue(created["success"])
        self.assertEqual(stopped["profile_name"], "mako")
        self.assertTrue(stopped["changed"])
        self.assertFalse(stopped["game_running"])
        self.assertEqual(resumed["profile_name"], created["profile_name"])
        self.assertTrue(resumed["changed"])
        self.assertTrue(resumed["game_running"])
        self.assertFalse(unchanged["changed"])
        self.assertTrue(unchanged["game_running"])
        self.assertEqual(exited["profile_name"], "mako")
        self.assertTrue(exited["changed"])
        self.assertFalse(exited["game_running"])
        self.assertEqual(
            self.service._get_profile_data()["current_profile"], "mako"
        )

    def test_profile_sync_keeps_default_for_unsaved_running_game(self):
        previous = configuration_module.detect_processes_for_steam_app
        configuration_module.detect_processes_for_steam_app = (
            lambda _app_id: ["UnknownGame.exe"]
        )
        try:
            result = self.service.sync_current_profile("67890")
        finally:
            configuration_module.detect_processes_for_steam_app = previous

        self.assertTrue(result["success"])
        self.assertEqual(result["profile_name"], "mako")
        self.assertFalse(result["changed"])
        self.assertEqual(self.service.get_profiles()["profiles"], ["mako"])

    def test_unsaved_game_cannot_match_different_saved_game_helpers(self):
        quake = dict(ConfigurationManager.get_defaults())
        quake["active_in"] = "Quake4.exe, steam.exe, python3.9"
        profile_data = ProfileData(
            current_profile="Quake-4",
            profiles={"mako": ConfigurationManager.get_defaults(), "Quake-4": quake},
            global_config=self.profile_data["global_config"],
        )
        self.service._save_profile_data(profile_data)
        self.service._write_profile_metadata({
            "mako": {
                "display_name": "Default",
                "kind": "default",
                "steam_app_id": None,
                "captured_processes": [],
            },
            "Quake-4": {
                "display_name": "Quake 4",
                "kind": "game",
                "steam_app_id": "12345",
                "captured_processes": ["Quake4.exe", "steam.exe", "python3.9"],
            },
        })

        previous = configuration_module.detect_processes_for_steam_app
        configuration_module.detect_processes_for_steam_app = (
            lambda _app_id: ["ResidentEvil4.exe", "steam.exe", "python3.9"]
        )
        try:
            result = self.service.sync_current_profile("67890")
        finally:
            configuration_module.detect_processes_for_steam_app = previous

        self.assertTrue(result["success"])
        self.assertEqual(result["profile_name"], "mako")
        self.assertTrue(result["changed"])

    def test_migration_removes_only_automatically_captured_helpers(self):
        quake = dict(ConfigurationManager.get_defaults())
        quake["active_in"] = (
            "Quake4.exe, steam.exe, python3.9, UserAlias.exe"
        )
        profile_data = ProfileData(
            current_profile="mako",
            profiles={"mako": ConfigurationManager.get_defaults(), "Quake-4": quake},
            global_config=self.profile_data["global_config"],
        )
        self.service._save_profile_data(profile_data)
        self.service._write_profile_metadata({
            "mako": {
                "display_name": "Default",
                "kind": "default",
                "steam_app_id": None,
                "captured_processes": [],
            },
            "Quake-4": {
                "display_name": "Quake 4",
                "kind": "game",
                "steam_app_id": "12345",
                "captured_processes": ["Quake4.exe", "steam.exe", "python3.9"],
            },
        })

        self.assertTrue(self.service.sanitize_captured_processes_if_needed())
        self.assertFalse(self.service.sanitize_captured_processes_if_needed())

        profiles = self.service.get_profiles()
        quake_detail = next(
            detail for detail in profiles["profile_details"]
            if detail["profile_name"] == "Quake-4"
        )
        self.assertEqual(
            quake_detail["processes"], ["Quake4.exe", "UserAlias.exe"]
        )
        metadata = json.loads(
            self.service.profile_metadata_path.read_text(encoding="utf-8")
        )
        self.assertEqual(
            metadata["profiles"]["Quake-4"]["captured_processes"],
            ["Quake4.exe"],
        )

    def test_profile_sync_can_reselect_existing_process_only_profile(self):
        created = self.service.create_profile("Manual Game")
        profile_name = created["profile_name"]
        profile = self.service._get_profile_data()["profiles"][profile_name]
        profile["active_in"] = "ManualGame.exe"
        self.service.update_profile_config(profile_name, profile)
        self.service.set_current_profile("mako")

        previous = configuration_module.detect_processes_for_steam_app
        configuration_module.detect_processes_for_steam_app = (
            lambda _app_id: ["manualgame.EXE"]
        )
        try:
            result = self.service.sync_current_profile("67890")
        finally:
            configuration_module.detect_processes_for_steam_app = previous

        self.assertTrue(result["success"])
        self.assertEqual(result["profile_name"], profile_name)
        self.assertTrue(result["changed"])

    def _run_wrapper(self, app_id: str, extra_environment=None):
        result = subprocess.run(
            [
                str(self.service.mako_script_path),
                "/bin/bash",
                "-c",
                'printf "PROFILE=%s\\nFALLBACK=%s\\nSDL=%s\\nDLLS=%s\\n" "${MAKO_PROFILE:-}" "${MAKO_PROFILE_FALLBACK:-}" "${SDL_AUDIODRIVER:-}" "${WINEDLLOVERRIDES:-}"',
            ],
            check=True,
            capture_output=True,
            text=True,
            env={
                "PATH": os.environ.get("PATH", ""),
                "SteamAppId": app_id,
                **(extra_environment or {}),
            },
        )
        return dict(line.split("=", 1) for line in result.stdout.splitlines())

    def test_wrapper_applies_alsa_only_to_the_matching_game(self):
        game = dict(ConfigurationManager.get_defaults())
        game["active_in"] = "CoolGame.exe"
        profile_data = ProfileData(
            current_profile="cool-game",
            profiles={"mako": ConfigurationManager.get_defaults(), "cool-game": game},
            global_config=self.profile_data["global_config"],
        )
        self.service._save_profile_data(profile_data)
        self.service._write_profile_metadata({
            "mako": {
                "display_name": "Default",
                "kind": "default",
                "steam_app_id": None,
            },
            "cool-game": {
                "display_name": "Cool Game",
                "kind": "game",
                "steam_app_id": "12345",
            },
        })
        self.service._write_wrapper_profile_settings({
            "cool-game": {"force_alsa_audio": True},
        })
        result = self.service.update_mako_script_from_profile_data(profile_data)
        self.assertTrue(result["success"])

        matched = self._run_wrapper("12345", {
            "SDL_AUDIODRIVER": "pulse",
            "WINEDLLOVERRIDES": "d3d11=n",
        })
        unrelated = self._run_wrapper("67890", {
            "SDL_AUDIODRIVER": "pipewire",
            "WINEDLLOVERRIDES": "dxgi=n",
        })
        already_forced = self._run_wrapper("12345", {
            "WINEDLLOVERRIDES": "d3d11=n;winepulse.drv=d",
        })
        explicit = self._run_wrapper("67890", {
            "MAKO_PROFILE": "cool-game",
            "WINEDLLOVERRIDES": "dxgi=n",
        })

        self.assertEqual(matched["PROFILE"], "cool-game")
        self.assertEqual(matched["FALLBACK"], "")
        self.assertEqual(matched["SDL"], "alsa")
        self.assertEqual(
            matched["DLLS"],
            "d3d11=n;winepulse.drv=d;winealsa.drv=b",
        )
        self.assertEqual(unrelated["PROFILE"], "")
        self.assertEqual(unrelated["FALLBACK"], "mako")
        self.assertEqual(unrelated["SDL"], "pipewire")
        self.assertEqual(unrelated["DLLS"], "dxgi=n")
        self.assertEqual(
            already_forced["DLLS"],
            "d3d11=n;winepulse.drv=d;winealsa.drv=b",
        )
        self.assertEqual(explicit["PROFILE"], "cool-game")
        self.assertEqual(explicit["SDL"], "alsa")
        self.assertEqual(
            explicit["DLLS"],
            "dxgi=n;winepulse.drv=d;winealsa.drv=b",
        )

        disabled_config = dict(game)
        disabled_config["force_alsa_audio"] = False
        disabled = self.service.update_profile_config(
            "cool-game", disabled_config
        )
        self.assertTrue(disabled["success"])
        restored = self._run_wrapper("12345", {
            "SDL_AUDIODRIVER": "pipewire",
            "WINEDLLOVERRIDES": "dxgi=n",
        })
        self.assertEqual(restored["PROFILE"], "cool-game")
        self.assertEqual(restored["SDL"], "pipewire")
        self.assertEqual(restored["DLLS"], "dxgi=n")

    def test_current_wrapper_is_never_reverse_migrated_as_one_profile(self):
        self.service.migrate_profile_metadata_if_needed()
        script = self.service._generate_script_content_for_profile(self.profile_data)
        self.service.mako_script_path.write_text(script, encoding="utf-8")

        self.assertFalse(self.service.migrate_wrapper_profile_settings_if_needed())
        self.assertFalse(self.service.wrapper_profile_settings_path.exists())

    def test_process_name_selects_manual_launcher_profile_without_steam_id(self):
        game = dict(ConfigurationManager.get_defaults())
        game["active_in"] = "CoolGame.exe"
        profile_data = ProfileData(
            current_profile="mako",
            profiles={"mako": ConfigurationManager.get_defaults(), "manual-game": game},
            global_config=self.profile_data["global_config"],
        )
        self.service._write_wrapper_profile_settings({
            "manual-game": {"force_alsa_audio": True},
        })
        lines = self.service._wrapper_profile_configuration_lines(profile_data)
        script = "\n".join(lines + [
            'printf "PROFILE=%s\\nSDL=%s\\n" "${MAKO_PROFILE:-}" "${SDL_AUDIODRIVER:-}"',
        ])

        result = subprocess.run(
            ["bash", "-c", script, "mako-run", "/games/CoolGame.exe"],
            check=True,
            capture_output=True,
            text=True,
            env={"PATH": os.environ.get("PATH", "")},
        )
        values = dict(line.split("=", 1) for line in result.stdout.splitlines())

        self.assertEqual(values["PROFILE"], "manual-game")
        self.assertEqual(values["SDL"], "alsa")

    def test_manual_process_text_cannot_inject_wrapper_commands(self):
        marker = Path(self.temp_dir.name) / "injected"
        game = dict(ConfigurationManager.get_defaults())
        game["active_in"] = f"$(touch {marker}).exe"
        profile_data = ProfileData(
            current_profile="mako",
            profiles={"mako": ConfigurationManager.get_defaults(), "manual-game": game},
            global_config=self.profile_data["global_config"],
        )
        script = "\n".join(
            self.service._wrapper_profile_configuration_lines(profile_data)
            + [":"]
        )

        subprocess.run(
            ["bash", "-c", script, "mako-run", "/games/SafeGame.exe"],
            check=True,
            env={"PATH": os.environ.get("PATH", "")},
        )

        self.assertFalse(marker.exists())

    def test_deleting_profile_removes_identity_and_wrapper_settings(self):
        profile = self.service.create_profile("Manual Game")
        self.assertTrue(profile["success"])
        name = profile["profile_name"]
        self.assertTrue(self.service.set_current_profile(name)["success"])
        deleted = self.service.delete_profile(name)
        self.assertTrue(deleted["success"])
        self.assertEqual(deleted["current_profile"], "mako")

        profiles = self.service.get_profiles()
        self.assertEqual(profiles["current_profile"], "mako")
        self.assertNotIn(name, profiles["profiles"])

        metadata = json.loads(self.service.profile_metadata_path.read_text(encoding="utf-8"))
        settings = json.loads(
            self.service.wrapper_profile_settings_path.read_text(encoding="utf-8")
        )
        self.assertNotIn(name, metadata["profiles"])
        self.assertNotIn(name, settings["profiles"])


class ProcessDetectionTests(unittest.TestCase):
    def test_scanner_only_returns_processes_with_the_requested_app_id(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            proc_root = Path(temp_dir)
            wanted = proc_root / "101"
            other = proc_root / "202"
            wanted.mkdir()
            other.mkdir()
            (wanted / "environ").write_bytes(b"SteamAppId=12345\0")
            (wanted / "comm").write_text("wine64-preloader\n", encoding="utf-8")
            (wanted / "cmdline").write_bytes(b"wine\0Z:\\Games\\CoolGame.exe\0")
            (wanted / "maps").write_text(
                "/games/CoolGame.exe\n/usr/lib/wine/services.exe\n",
                encoding="utf-8",
            )
            (other / "environ").write_bytes(b"SteamAppId=67890\0")
            (other / "comm").write_text("OtherGame\n", encoding="utf-8")
            (other / "cmdline").write_bytes(b"/games/OtherGame\0")
            (other / "maps").write_text("", encoding="utf-8")

            detected = detect_processes_for_steam_app("12345", proc_root)

        self.assertEqual(detected, ["CoolGame.exe"])


if __name__ == "__main__":
    unittest.main()
