"""Regression tests for persistent game/process profiles and isolation."""

import json
import os
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch


class _Logger:
    def __getattr__(self, _name):
        return lambda *_args, **_kwargs: None


sys.modules.setdefault("decky", SimpleNamespace(logger=_Logger()))

from py_modules.mako_plugin import configuration as configuration_module  # noqa: E402
from py_modules.mako_plugin import managed_files as managed_files_module  # noqa: E402
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
        self.test_bin_dir = root / "test-bin"
        self.test_bin_dir.mkdir()
        test_uname = self.test_bin_dir / "uname"
        test_uname.write_text("#!/bin/sh\nprintf 'x86_64\\n'\n", encoding="utf-8")
        test_uname.chmod(0o755)
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

    def test_profile_save_failure_preserves_existing_renderer_config(self):
        original = self.service.config_file_path.read_text(encoding="utf-8")
        updated = ProfileData(
            current_profile="mako",
            profiles={
                "mako": {
                    **self.profile_data["profiles"]["mako"],
                    "multiplier": 3,
                },
            },
            global_config=dict(self.profile_data["global_config"]),
        )

        with patch.object(
                managed_files_module.os,
                "fsync",
                side_effect=OSError("simulated write failure"),
        ):
            with self.assertRaisesRegex(OSError, "atomically replace"):
                self.service._save_profile_data(updated)

        self.assertEqual(
            self.service.config_file_path.read_text(encoding="utf-8"),
            original,
        )
        self.assertEqual(
            list(self.service.config_dir.glob(".conf.toml.*")),
            [],
        )

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

    def test_missing_steady_cap_uses_current_default_for_existing_profiles(self):
        content = "\n".join([
            "version = 2",
            "[global]",
            "allow_fp16 = true",
            "[[profile]]",
            'name = "mako"',
            "adaptive = true",
            "",
        ])

        self.assertTrue(
            ConfigurationManager.get_defaults()["adaptive_auto_base_fps_cap"]
        )
        profile_data = ConfigurationManager.parse_toml_content_multi_profile(content)
        self.assertTrue(
            profile_data["profiles"]["mako"]["adaptive_auto_base_fps_cap"]
        )

    def test_missing_scaling_fields_are_inert_for_existing_profiles(self):
        content = "\n".join([
            "version = 2",
            "[global]",
            "allow_fp16 = true",
            "[[profile]]",
            'name = "mako"',
            "frame_generation_enabled = false",
            "",
        ])

        profile = ConfigurationManager.parse_toml_content_multi_profile(
            content
        )["profiles"]["mako"]

        self.assertFalse(profile["scaling_enabled"])
        self.assertEqual(profile["scaling_factor"], 1.5)
        self.assertEqual(profile["scaling_sharpness"], 0.5)
        self.assertFalse(profile["frame_generation_enabled"])

    def test_explicit_fractional_choice_is_preserved_for_existing_profiles(self):
        content = "\n".join([
            "version = 2",
            "[global]",
            "allow_fp16 = true",
            "[[profile]]",
            'name = "mako"',
            "adaptive = true",
            "adaptive_auto_base_fps_cap = false",
            "",
        ])

        profile_data = ConfigurationManager.parse_toml_content_multi_profile(content)

        self.assertFalse(
            profile_data["profiles"]["mako"]["adaptive_auto_base_fps_cap"]
        )

    def test_rpc_validation_discards_unknown_profile_options(self):
        validated = ConfigurationManager.validate_config({
            **ConfigurationManager.get_defaults(),
            "removed_profile_option": True,
        })

        self.assertNotIn("removed_profile_option", validated)

    def test_dynamic_cadence_recovery_disables_both_base_caps(self):
        validated = ConfigurationManager.validate_config({
            **ConfigurationManager.get_defaults(),
            "adaptive": True,
            "adaptive_auto_base_fps_cap": True,
            "base_fps_cap": 30,
            "dynamic_cadence_recovery": True,
        })

        self.assertFalse(validated["adaptive_auto_base_fps_cap"])
        self.assertEqual(validated["base_fps_cap"], 0)

        fixed = ConfigurationManager.validate_config({
            **validated,
            "adaptive": False,
            "adaptive_auto_base_fps_cap": True,
            "base_fps_cap": 30,
        })
        self.assertFalse(fixed["adaptive_auto_base_fps_cap"])
        self.assertEqual(fixed["base_fps_cap"], 0)

    def test_dynamic_cadence_probe_interval_is_validated_and_persisted(self):
        defaults = ConfigurationManager.get_defaults()
        self.assertEqual(defaults["dynamic_cadence_probe_interval_seconds"], 2.0)

        for interval in (0.1, 0.2, 0.25, 0.5):
            with self.subTest(interval=interval):
                configured = ConfigurationManager.validate_config({
                    **defaults,
                    "dynamic_cadence_probe_interval_seconds": interval,
                })
                content = ConfigurationManager.generate_toml_content(configured)
                self.assertIn(
                    f"dynamic_cadence_probe_interval_seconds = {interval}",
                    content,
                )
                self.assertEqual(
                    ConfigurationManager.parse_toml_content(content)[
                        "dynamic_cadence_probe_interval_seconds"
                    ],
                    interval,
                )

        for invalid in (0.09, 4):
            with self.subTest(invalid=invalid), self.assertRaisesRegex(
                ValueError,
                "dynamic_cadence_probe_interval_seconds must be between 0.1 and 3",
            ):
                ConfigurationManager.validate_config({
                    **defaults,
                    "dynamic_cadence_probe_interval_seconds": invalid,
                })

    def test_frame_generation_refresh_threshold_is_validated(self):
        defaults = ConfigurationManager.get_defaults()
        configured = ConfigurationManager.validate_config({
            **defaults,
            "frame_generation_refresh_threshold": 130,
        })
        self.assertEqual(configured["frame_generation_refresh_threshold"], 130)
        content = ConfigurationManager.generate_toml_content(configured)
        self.assertIn("frame_generation_refresh_threshold = 130", content)
        self.assertEqual(
            ConfigurationManager.parse_toml_content(content)[
                "frame_generation_refresh_threshold"
            ],
            130,
        )

        with self.assertRaisesRegex(
            ValueError, "frame_generation_refresh_threshold must be 0 or between"
        ):
            ConfigurationManager.validate_config({
                **defaults,
                "frame_generation_refresh_threshold": 29,
            })

    def test_scaling_values_are_validated_and_persisted(self):
        defaults = ConfigurationManager.get_defaults()
        configured = ConfigurationManager.validate_config({
            **defaults,
            "scaling_enabled": True,
            "scaling_factor": 1.7,
            "scaling_sharpness": 0.75,
        })

        content = ConfigurationManager.generate_toml_content(configured)
        self.assertIn("scaling_enabled = true", content)
        self.assertIn("scaling_factor = 1.7", content)
        self.assertIn("scaling_sharpness = 0.75", content)
        parsed = ConfigurationManager.parse_toml_content(content)
        self.assertTrue(parsed["scaling_enabled"])
        self.assertEqual(parsed["scaling_factor"], 1.7)
        self.assertEqual(parsed["scaling_sharpness"], 0.75)

        for field, invalid, message in (
            (
                "scaling_factor",
                0.9,
                "scaling_factor must be between 1.0 and 2.0",
            ),
            (
                "scaling_factor",
                2.1,
                "scaling_factor must be between 1.0 and 2.0",
            ),
            (
                "scaling_sharpness",
                -0.01,
                "scaling_sharpness must be between 0.0 and 1.0",
            ),
            (
                "scaling_sharpness",
                1.01,
                "scaling_sharpness must be between 0.0 and 1.0",
            ),
        ):
            with (
                self.subTest(field=field, invalid=invalid),
                self.assertRaisesRegex(ValueError, message),
            ):
                ConfigurationManager.validate_config({
                    **defaults,
                    field: invalid,
                })

    def test_scaling_and_frame_generation_patches_are_independent(self):
        existing = {
            **ConfigurationManager.get_defaults(),
            "frame_generation_enabled": False,
            "adaptive": True,
            "target_fps": 120,
        }
        self.assertTrue(
            self.service.update_profile_config("mako", existing)["success"]
        )

        scaling_result = self.service.update_profile_config_fields(
            "mako",
            {
                "scaling_enabled": True,
                "scaling_factor": 1.8,
                "scaling_sharpness": 0.7,
            },
        )
        self.assertTrue(scaling_result["success"])
        scaled = self.service.get_profile_config("mako")["config"]
        self.assertFalse(scaled["frame_generation_enabled"])
        self.assertTrue(scaled["adaptive"])
        self.assertEqual(scaled["target_fps"], 120)

        frame_generation_result = self.service.update_profile_config_fields(
            "mako", {"frame_generation_enabled": True, "adaptive": False}
        )
        self.assertTrue(frame_generation_result["success"])
        saved = self.service.get_profile_config("mako")["config"]
        self.assertTrue(saved["scaling_enabled"])
        self.assertEqual(saved["scaling_factor"], 1.8)
        self.assertEqual(saved["scaling_sharpness"], 0.7)

    def test_field_update_merges_with_latest_canonical_profile(self):
        existing = dict(ConfigurationManager.get_defaults())
        existing["performance_mode"] = True
        self.assertTrue(
            self.service.update_profile_config("mako", existing)["success"]
        )

        result = self.service.update_profile_config_fields(
            "mako", {"target_fps": 120}
        )

        self.assertTrue(result["success"])
        saved = self.service.get_profile_config("mako")["config"]
        self.assertEqual(saved["target_fps"], 120)
        self.assertTrue(saved["performance_mode"])

    def test_profile_updates_do_not_replace_unchanged_managed_artifacts(self):
        defaults = ConfigurationManager.get_defaults()
        self.assertTrue(
            self.service.update_profile_config("mako", defaults)["success"]
        )
        managed_results = []
        sidecar_results = []
        original_managed_write = (
            configuration_module.write_managed_text_atomically
        )
        original_sidecar_write = self.service._write_file

        def observe_managed_write(destination, content, mode, logger):
            changed = original_managed_write(
                destination, content, mode, logger
            )
            managed_results.append((destination, changed))
            return changed

        def observe_sidecar_write(path, content, mode=0o644):
            changed = original_sidecar_write(path, content, mode)
            sidecar_results.append((path, changed))
            return changed

        with (
            patch.object(
                configuration_module,
                "write_managed_text_atomically",
                side_effect=observe_managed_write,
            ),
            patch.object(
                self.service,
                "_write_file",
                side_effect=observe_sidecar_write,
            ),
        ):
            renderer_result = self.service.update_profile_config_fields(
                "mako", {"scaling_sharpness": 0.73}
            )

        self.assertTrue(renderer_result["success"])
        self.assertEqual(
            managed_results,
            [
                (self.service.config_file_path, True),
                (self.service.mako_script_path, False),
            ],
        )
        self.assertEqual(
            sidecar_results,
            [(self.service.wrapper_profile_settings_path, False)],
        )

        self.service.config_file_path.chmod(0o666)
        managed_results.clear()
        sidecar_results.clear()
        with (
            patch.object(
                configuration_module,
                "write_managed_text_atomically",
                side_effect=observe_managed_write,
            ),
            patch.object(
                self.service,
                "_write_file",
                side_effect=observe_sidecar_write,
            ),
        ):
            permission_result = self.service.update_profile_config_fields(
                "mako", {"scaling_sharpness": 0.73}
            )

        self.assertTrue(permission_result["success"])
        self.assertEqual(
            managed_results,
            [
                (self.service.config_file_path, True),
                (self.service.mako_script_path, False),
            ],
        )
        self.assertEqual(
            self.service.config_file_path.stat().st_mode & 0o777,
            0o644,
        )
        self.assertEqual(
            sidecar_results,
            [(self.service.wrapper_profile_settings_path, False)],
        )

        managed_results.clear()
        sidecar_results.clear()
        with (
            patch.object(
                configuration_module,
                "write_managed_text_atomically",
                side_effect=observe_managed_write,
            ),
            patch.object(
                self.service,
                "_write_file",
                side_effect=observe_sidecar_write,
            ),
        ):
            script_result = self.service.update_profile_config_fields(
                "mako", {"enable_zink": True}
            )

        self.assertTrue(script_result["success"])
        self.assertEqual(
            managed_results,
            [
                (self.service.config_file_path, False),
                (self.service.mako_script_path, True),
            ],
        )
        self.assertEqual(
            sidecar_results,
            [(self.service.wrapper_profile_settings_path, True)],
        )

    def test_field_update_rejects_unknown_options_without_rewriting_profile(self):
        original = self.service.config_file_path.read_text(encoding="utf-8")

        result = self.service.update_profile_config_fields(
            "mako", {"removed_profile_option": True}
        )

        self.assertFalse(result["success"])
        self.assertIn("removed_profile_option", result["error"])
        self.assertEqual(
            self.service.config_file_path.read_text(encoding="utf-8"),
            original,
        )

    def test_field_updates_are_serialized_at_the_service_boundary(self):
        active_calls = 0
        maximum_active_calls = 0

        def observe_serialization(_profile_name, _changes):
            nonlocal active_calls, maximum_active_calls
            active_calls += 1
            maximum_active_calls = max(maximum_active_calls, active_calls)
            time.sleep(0.02)
            active_calls -= 1
            return {
                "success": True,
                "message": "updated",
                "error": None,
                "config": ConfigurationManager.get_defaults(),
            }

        self.service._update_profile_config_fields = observe_serialization
        with ThreadPoolExecutor(max_workers=2) as executor:
            results = list(executor.map(
                lambda value: self.service.update_profile_config_fields(
                    "mako", {"target_fps": value}
                ),
                (90, 120),
            ))

        self.assertTrue(all(result["success"] for result in results))
        self.assertEqual(maximum_active_calls, 1)

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

    def test_capture_keeps_saved_games_with_the_same_process_name_separate(self):
        previous = configuration_module.detect_processes_for_steam_app
        configuration_module.detect_processes_for_steam_app = (
            lambda _app_id: ["Game.exe"]
        )
        try:
            first = self.service.capture_game_profile("12345", "First Game")
            second = self.service.capture_game_profile("67890", "Second Game")
        finally:
            configuration_module.detect_processes_for_steam_app = previous

        self.assertTrue(first["success"])
        self.assertTrue(second["success"])
        self.assertNotEqual(first["profile_name"], second["profile_name"])
        profiles = self.service.get_profiles()
        self.assertEqual(
            profiles["profiles"],
            ["mako", first["profile_name"], second["profile_name"]],
        )
        app_ids = {
            detail["profile_name"]: detail["steam_app_id"]
            for detail in profiles["profile_details"]
        }
        self.assertEqual(app_ids[first["profile_name"]], "12345")
        self.assertEqual(app_ids[second["profile_name"]], "67890")

    def test_capture_can_still_adopt_a_matching_manual_profile(self):
        manual = self.service.create_profile("Manual Game")
        profile_name = manual["profile_name"]
        config = self.service._get_profile_data()["profiles"][profile_name]
        config["active_in"] = "Game.exe"
        self.assertTrue(
            self.service.update_profile_config(profile_name, config)["success"]
        )

        previous = configuration_module.detect_processes_for_steam_app
        configuration_module.detect_processes_for_steam_app = (
            lambda _app_id: ["Game.exe"]
        )
        try:
            captured = self.service.capture_game_profile("12345", "Steam Game")
        finally:
            configuration_module.detect_processes_for_steam_app = previous

        self.assertTrue(captured["success"])
        self.assertEqual(captured["profile_name"], profile_name)
        self.assertEqual(
            self.service.get_profiles()["profiles"], ["mako", profile_name]
        )
        details = self.service.get_profiles()["profile_details"]
        self.assertEqual(details[1]["steam_app_id"], "12345")

    def test_profile_storage_is_not_limited_to_ten_entries(self):
        for index in range(1, 13):
            result = self.service.create_profile(f"Game {index}")
            self.assertTrue(result["success"])

        expected = ["mako", *(f"Game-{index}" for index in range(1, 13))]
        self.assertEqual(self.service.get_profiles()["profiles"], expected)
        self.assertEqual(
            list(self.service._get_profile_data()["profiles"]), expected
        )

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
                'printf "PROFILE=%s\\nFALLBACK=%s\\nSDL=%s\\nDLLS=%s\\nMANGOHUD=%s\\nVKBASALT=%s\\nIMPLICIT=%s\\n" "${MAKO_PROFILE:-}" "${MAKO_PROFILE_FALLBACK:-}" "${SDL_AUDIODRIVER:-}" "${WINEDLLOVERRIDES:-}" "${MANGOHUD:-}" "${ENABLE_VKBASALT:-}" "${VK_IMPLICIT_LAYER_PATH:-}"',
            ],
            check=True,
            capture_output=True,
            text=True,
            env={
                "PATH": f"{self.test_bin_dir}:{os.environ.get('PATH', '')}",
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

    def test_external_tool_selection_is_applied_per_game_profile(self):
        game = dict(ConfigurationManager.get_defaults())
        game["active_in"] = "CoolGame.exe"
        profile_data = ProfileData(
            current_profile="mako",
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
            "mako": {"external_vulkan_layer": "vkbasalt"},
            "cool-game": {"external_vulkan_layer": "mangohud"},
        })

        with tempfile.TemporaryDirectory() as system_dir:
            self.service.mangohud_layer_dir = Path(system_dir) / "mangohud"
            self.service.vkbasalt_layer_dir = Path(system_dir) / "vkbasalt"
            self.service.mangohud_layer_dir.mkdir()
            self.service.vkbasalt_layer_dir.mkdir()
            (
                self.service.mangohud_layer_dir /
                configuration_module.MANGOHUD_MANIFEST_FILENAME_64
            ).write_text("{}", encoding="utf-8")
            (
                self.service.vkbasalt_layer_dir /
                configuration_module.VKBASALT_MANIFEST_FILENAME_64
            ).write_text("{}", encoding="utf-8")
            result = self.service.update_mako_script_from_profile_data(
                profile_data
            )
            self.assertTrue(result["success"])
            matched = self._run_wrapper("12345")
            unrelated = self._run_wrapper("67890")

        self.assertEqual(matched["MANGOHUD"], "1")
        self.assertEqual(matched["VKBASALT"], "")
        self.assertEqual(
            matched["IMPLICIT"],
            f"{self.service.local_share_dir}:{self.service.mangohud_layer_dir}",
        )
        self.assertEqual(unrelated["MANGOHUD"], "")
        self.assertEqual(unrelated["VKBASALT"], "1")
        self.assertEqual(
            unrelated["IMPLICIT"],
            f"{self.service.local_share_dir}:{self.service.vkbasalt_layer_dir}",
        )

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
