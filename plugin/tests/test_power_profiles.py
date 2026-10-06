"""AC/battery persistence and independent Renderer/Decky contracts."""

import tempfile
import tomllib
from pathlib import Path
from unittest.mock import patch
import unittest
from types import SimpleNamespace

from tests.test_game_profiles import _Logger
from py_modules.mako_plugin.config_schema import ConfigurationManager, POWER_PROFILE_FIELDS
from py_modules.mako_plugin.configuration import ConfigurationService
from py_modules.mako_plugin.host_environment import detect_power_source
from py_modules.mako_plugin.installation import InstallationService


class PowerProfileTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        root = Path(self.directory.name)
        self.service = ConfigurationService(logger=_Logger())
        self.service.config_dir = root / "config"
        self.service.config_file_path = self.service.config_dir / "conf.toml"
        self.service.profile_metadata_path = self.service.config_dir / "profile-metadata.json"
        self.service.wrapper_profile_settings_path = self.service.config_dir / "profile-wrapper-settings.json"
        self.service.mako_script_path = root / "mako-run"
        self.service.user_home = root
        self.service._save_profile_data(ConfigurationManager.parse_toml_content_multi_profile('''version = 2
[global]
allow_fp16 = false
[[profile]]
name = "mako"
[[profile]]
name = "game"
active_in = ["Game.exe"]
adaptive = true
target_fps = 90
'''))

    def test_cloning_and_variant_edits_stay_isolated(self):
        original = self.service.get_profile_config("game")["config"]
        for _ in range(2):
            self.assertTrue(self.service.set_profile_power_modes("game", True)["success"])
        self.assertEqual(original, self.service.get_profile_config("game", "handheld")["config"])
        self.assertTrue(self.service.update_profile_config_fields("game", {"target_fps": 144, "scaling_factor": 1.7}, "docked")["success"])
        self.assertEqual(144, self.service.get_profile_config("game", "docked")["config"]["target_fps"])
        for mode in ("handheld", "shared"):
            self.assertEqual(90, self.service.get_profile_config("game", mode)["config"]["target_fps"])
        self.assertFalse(self.service.get_profile_config("game", "docked")["config"]["allow_fp16"])
        self.assertEqual("Game.exe", self.service.get_profile_config("game", "docked")["config"]["active_in"])
        for source, target in (("docked", 144), ("handheld", 90), ("", 90)):
            with patch("py_modules.mako_plugin.configuration.detect_power_source", return_value=source):
                self.assertEqual(target, self.service.get_profile_config("game")["config"]["target_fps"])

    def test_refresh_target_defaults_and_power_mode_round_trip(self):
        self.assertFalse(self.service.get_profile_config("game")["config"]["adaptive_target_refresh_rate"])
        self.service.set_profile_power_modes("game", True)
        self.assertTrue(self.service.update_profile_config_fields("game", {
            "adaptive_target_refresh_rate": True, "target_fps": 100,
        }, "docked")["success"])
        saved = ConfigurationManager.parse_toml_content_multi_profile(self.service.config_file_path.read_text())
        self.assertTrue(saved["power_profiles"]["game"]["docked"]["adaptive_target_refresh_rate"])
        self.assertFalse(saved["power_profiles"]["game"]["handheld"]["adaptive_target_refresh_rate"])
        self.assertFalse(saved["profiles"]["game"]["adaptive_target_refresh_rate"])
        self.assertEqual(100, saved["power_profiles"]["game"]["docked"]["target_fps"])
        self.assertEqual(saved, ConfigurationManager.parse_toml_content_multi_profile(
            ConfigurationManager.generate_toml_content_multi_profile(saved)
        ))

    def test_shared_fields_and_lifecycle_preserve_variants(self):
        self.service.set_profile_power_modes("game", True)
        result = self.service.update_profile_config_fields("game", {"target_fps": 120, "active_in": "New.exe", "allow_fp16": True, "enable_zink": True}, "docked")
        self.assertTrue(result["success"], result)
        handheld = self.service.get_profile_config("game", "handheld")["config"]
        self.assertEqual(90, handheld["target_fps"])
        self.assertEqual("New.exe", handheld["active_in"])
        self.assertTrue(handheld["allow_fp16"])
        self.assertTrue(handheld["enable_zink"])
        self.assertTrue(self.service.rename_profile("game", "renamed")["success"])
        self.assertEqual(120, self.service.get_profile_config("renamed", "docked")["config"]["target_fps"])
        self.assertTrue(self.service.create_profile("clone", "renamed")["success"])
        self.service.update_profile_config_fields("clone", {"target_fps": 150}, "docked")
        self.assertEqual(120, self.service.get_profile_config("renamed", "docked")["config"]["target_fps"])
        self.assertTrue(self.service.delete_profile("clone")["success"])
        self.assertNotIn("clone", self.service._get_profile_data()["power_profiles"])

    def test_disable_restores_fallback_and_rejects_late_writes(self):
        self.service.set_profile_power_modes("game", True)
        self.service.update_profile_config_fields("game", {"target_fps": 144}, "docked")
        response = self.service.set_profile_power_modes("game", False)
        self.assertTrue(response["success"])
        self.assertFalse(response["separate_power_modes"])
        self.assertEqual(90, response["config"]["target_fps"])
        self.assertFalse(self.service.update_profile_config_fields("game", {"target_fps": 180}, "docked")["success"])
        self.assertEqual(90, self.service.get_profile_config("game")["config"]["target_fps"])
        self.assertFalse(self.service.set_profile_power_modes("game", "yes")["success"])
        self.assertFalse(self.service.get_profile_config("game", "invalid")["success"])

    def test_installation_default_merge_preserves_independent_power_profiles(self):
        self.service.set_profile_power_modes("game", True)
        self.service.update_profile_config_fields("game", {"target_fps": 144}, "docked")
        original = self.service._get_profile_data()
        dll_service = SimpleNamespace(check_lossless_scaling_dll=lambda: {
            "detected": False, "path": None,
        })
        merged = InstallationService(logger=_Logger())._merge_config_with_defaults(
            original, dll_service
        )
        self.assertEqual(original["power_profiles"], merged["power_profiles"])
        self.assertFalse(merged["global_config"]["allow_fp16"])
        serialized = ConfigurationManager.generate_toml_content_multi_profile(merged)
        self.assertEqual(merged, ConfigurationManager.parse_toml_content_multi_profile(serialized))
        merged["power_profiles"]["game"]["docked"]["target_fps"] = 180
        self.assertEqual(144, original["power_profiles"]["game"]["docked"]["target_fps"])

    def test_sparse_native_tables_inherit_and_round_trip(self):
        base = self.service.config_file_path.read_text()
        data = ConfigurationManager.parse_toml_content_multi_profile(base + '\n[profile.handheld]\ntarget_fps = 60\n[profile.docked]\ntarget_fps = 120\n')
        self.assertEqual(60, data["power_profiles"]["game"]["handheld"]["target_fps"])
        self.assertTrue(data["power_profiles"]["game"]["docked"]["adaptive"])
        self.assertEqual(data, ConfigurationManager.parse_toml_content_multi_profile(ConfigurationManager.generate_toml_content_multi_profile(data)))
        for invalid in ('\n[profile.handheld]\n', '\n[profile.handheld]\nname = "other"\n[profile.docked]\n', '\n[profile.handheld]\ntarget_fps = 0\n[profile.docked]\n'):
            with self.assertRaises(ValueError):
                ConfigurationManager.parse_toml_content_multi_profile(base + invalid)

    def test_wrapper_keeps_renderer_discovery_for_either_power_mode(self):
        self.service.update_profile_config_fields("game", {"frame_generation_provisioned": False, "scaling_enabled": False})
        self.service.set_profile_power_modes("game", True)
        self.service.update_profile_config_fields("game", {"scaling_enabled": True}, "docked")
        script = self.service.mako_script_path.read_text()
        game_case = script.split("    game)", 1)[1].split(";;", 1)[0]
        self.assertIn("mako_renderer_required=1", game_case)
        self.assertFalse(self.service.get_profile_config("game", "handheld")["config"]["scaling_enabled"])

    def test_native_serializer_and_shared_field_contract(self):
        native = (Path(__file__).resolve().parents[2] / "engine/mako-common/src/configuration/config.cpp").read_text()
        for field in POWER_PROFILE_FIELDS:
            self.assertIn('"' + field + '"', native)
        for shared in ("active_in", "dll", "allow_fp16", "enable_zink", "external_vulkan_layer"):
            self.assertNotIn(shared, POWER_PROFILE_FIELDS)

    def test_every_native_setting_is_saved_in_each_power_table(self):
        self.service.set_profile_power_modes("game", True)
        self.service.update_profile_config_fields("game", {"gpu": 'GPU "A"'}, "shared")
        data = self.service._get_profile_data()
        tables = tomllib.loads(ConfigurationManager.generate_toml_content_multi_profile(data))
        profile = next(item for item in tables["profile"] if item["name"] == "game")
        self.assertEqual(POWER_PROFILE_FIELDS, set(profile) - {"name", "active_in", "handheld", "docked"})
        for mode in ("handheld", "docked"):
            with self.subTest(mode=mode):
                self.assertEqual(POWER_PROFILE_FIELDS, set(profile[mode]))
                self.assertEqual(data["power_profiles"]["game"][mode], profile[mode])
        self.assertEqual(data, ConfigurationManager.parse_toml_content_multi_profile(
            ConfigurationManager.generate_toml_content_multi_profile(data)
        ))

    def test_shared_live_shader_edits_preserve_both_power_sets(self):
        self.service.set_profile_power_modes("game", True)
        self.service.update_profile_config_fields("game", {"target_fps": 60}, "handheld")
        self.service.update_profile_config_fields("game", {"target_fps": 144}, "docked")
        original = self.service._get_profile_data()["power_profiles"]
        for mode, strength in (("handheld", 0.8), ("docked", 0.3)):
            result = self.service.update_profile_config_fields("game", {
                "external_vulkan_layer": "vkbasalt", "vkbasalt_sharpness": strength,
            }, mode)
            self.assertTrue(result["success"], result)
            self.assertEqual(original, self.service._get_profile_data()["power_profiles"])
            for selected in ("shared", "handheld", "docked"):
                self.assertEqual(strength, self.service.get_profile_config("game", selected)["config"]["vkbasalt_sharpness"])


class PowerSupplyTests(unittest.TestCase):
    def test_system_supplies_and_unknown_reads(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            def supply(name, **fields):
                path = root / name
                path.mkdir(exist_ok=True)
                for key, value in fields.items():
                    (path / key).write_text(str(value) + "\n")
            self.assertEqual("", detect_power_source(root))
            supply("controller", type="Battery", scope="Device")
            supply("AC", type="USB_C", online=0)
            self.assertEqual("", detect_power_source(root))
            supply("BAT", type="Battery", present=1, status="Full")
            self.assertEqual("handheld", detect_power_source(root))
            supply("AC", online=1)
            self.assertEqual("docked", detect_power_source(root))
            supply("second", type="Mains", online=0)
            self.assertEqual("docked", detect_power_source(root))
            supply("AC", online="unknown")
            self.assertEqual("", detect_power_source(root))
            supply("AC", online=0)
            supply("second", scope="Device", online=1)
            self.assertEqual("handheld", detect_power_source(root))
            supply("BAT", present=0)
            self.assertEqual("", detect_power_source(root))
            self.assertEqual("", detect_power_source(root / "missing"))
