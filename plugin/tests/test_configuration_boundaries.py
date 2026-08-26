"""Characterization tests for Decky profile storage and wrapper generation."""

import hashlib
import json
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

from shared_config import (  # noqa: E402
    DEFAULT_PROFILE_NAME as SHARED_DEFAULT_PROFILE_NAME,
)
from py_modules.mako_plugin.config_schema import (  # noqa: E402
    ConfigurationManager,
    DEFAULT_PROFILE_NAME,
    ProfileData,
)
from py_modules.mako_plugin.configuration import ConfigurationService  # noqa: E402
from py_modules.mako_plugin import profile_storage, wrapper_generation  # noqa: E402


def _sha256(content: str) -> str:
    return hashlib.sha256(content.encode("utf-8")).hexdigest()


class ConfigurationBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.service = ConfigurationService(
            logger=_Logger(),
            development_build=False,
        )
        self.service.config_dir = Path("/home/deck/.config/mako-render")
        self.service.config_file_path = self.service.config_dir / "conf.toml"
        self.service.mako_script_path = Path("/home/deck/.local/bin/mako-run")
        self.service.local_share_dir = Path(
            "/home/deck/.local/share/vulkan/implicit_layer.d"
        )
        self.service.gamescope_wsi_compatibility_dir = Path(
            "/home/deck/.local/share/mako/gamescope_wsi_compatibility.d"
        )

    def test_default_profile_name_comes_from_the_shared_contract(self):
        self.assertEqual(DEFAULT_PROFILE_NAME, SHARED_DEFAULT_PROFILE_NAME)
        self.assertEqual(DEFAULT_PROFILE_NAME, "mako")

    def test_profile_metadata_owner_preserves_the_persisted_shape(self):
        captured_processes = ["Game.exe"]
        metadata = {
            "old-name": profile_storage.profile_metadata_entry(
                "Old Name", "game", "12345", captured_processes
            )
        }
        captured_processes.append("LateMutation.exe")

        profile_storage.rename_profile_metadata(
            metadata, "old-name", "new-name", "New Name"
        )
        profile_storage.replace_captured_processes(
            metadata["new-name"], ["Game.exe", "Helper.exe"]
        )

        self.assertEqual(metadata, {
            "new-name": {
                "display_name": "New Name",
                "kind": "game",
                "steam_app_id": "12345",
                "captured_processes": ["Game.exe", "Helper.exe"],
            },
        })
        self.assertEqual(
            profile_storage.metadata_steam_app_id(metadata, "new-name"),
            "12345",
        )
        self.assertEqual(
            profile_storage.metadata_captured_processes(metadata, "new-name"),
            ["Game.exe", "Helper.exe"],
        )
        returned_processes = profile_storage.metadata_captured_processes(
            metadata, "new-name"
        )
        returned_processes.append("CallerMutation.exe")
        self.assertEqual(
            metadata["new-name"]["captured_processes"],
            ["Game.exe", "Helper.exe"],
        )

    def test_profile_transforms_do_not_alias_mutable_input_dictionaries(self):
        defaults = ConfigurationManager.get_defaults()
        game = {**defaults, "multiplier": 3}
        profile_data = ProfileData(
            current_profile="mako",
            profiles={"mako": defaults, "game": game},
            global_config={
                "dll": defaults["dll"],
                "allow_fp16": defaults["allow_fp16"],
            },
        )

        transformed = (
            ConfigurationManager.create_profile(profile_data, "Copy", "game"),
            ConfigurationManager.delete_profile(profile_data, "game"),
            ConfigurationManager.rename_profile(profile_data, "game", "Renamed"),
            ConfigurationManager.set_current_profile(profile_data, "game"),
        )
        for result in transformed:
            result["profiles"]["mako"]["multiplier"] = 4
            result["global_config"]["allow_fp16"] = False

        self.assertEqual(profile_data["profiles"]["mako"]["multiplier"], 2)
        self.assertTrue(profile_data["global_config"]["allow_fp16"])

    def test_default_wrapper_bytes_are_characterized(self):
        defaults = ConfigurationManager.get_defaults()
        content = self.service._generate_script_content(defaults)

        self.assertEqual(len(content.encode("utf-8")), 4421)
        self.assertEqual(
            _sha256(content),
            "15aff59a01f941e2a4f7ca08031e553900ef0b8fb7585b80a8aa9f2ab8c55d35",
        )
        self.assertEqual(
            wrapper_generation.generate_script_content(
                defaults,
                self.service._wrapper_generation_context(),
            ),
            content,
        )

    def test_multi_profile_wrapper_bytes_are_characterized(self):
        defaults = ConfigurationManager.get_defaults()
        game_config = {
            **defaults,
            "active_in": "CoolGame.exe, launcher-safe",
            "multiplier": 3,
        }
        profile_data = ProfileData(
            current_profile="cool-game",
            profiles={
                "mako": defaults,
                "cool-game": game_config,
            },
            global_config={
                "dll": defaults["dll"],
                "allow_fp16": defaults["allow_fp16"],
            },
        )
        wrapper_settings = {
            "mako": self.service._wrapper_settings_defaults(),
            "cool-game": {
                **self.service._wrapper_settings_defaults(),
                "force_alsa_audio": True,
                "external_vulkan_layer": "mangohud",
            },
        }
        metadata = {
            "mako": {
                "display_name": "Default",
                "kind": "default",
                "steam_app_id": None,
                "captured_processes": [],
            },
            "cool-game": {
                "display_name": "Cool Game",
                "kind": "game",
                "steam_app_id": "12345",
                "captured_processes": ["CoolGame.exe"],
            },
        }

        with (
            patch.object(
                self.service,
                "_read_wrapper_profile_settings",
                return_value=wrapper_settings,
            ),
            patch.object(
                self.service,
                "_read_profile_metadata",
                return_value=metadata,
            ),
        ):
            content = self.service._generate_script_content_for_profile(
                profile_data
            )

        self.assertEqual(len(content.encode("utf-8")), 6421)
        self.assertEqual(
            _sha256(content),
            "a9db4c9039c637dbbd067d90a152ac149412bc0b5c68f4882f1eb55fca3a9ab6",
        )
        self.assertEqual(
            wrapper_generation.generate_profile_script_content(
                profile_data,
                wrapper_settings,
                metadata,
                self.service._wrapper_generation_context(),
            ),
            content,
        )

    def test_configuration_facade_preserves_wrapper_generation_seams(self):
        with (
            patch.object(
                self.service,
                "_generate_host_compatibility_guard_lines",
                return_value=["host guard"],
            ),
            patch.object(
                self.service,
                "_script_configuration_lines",
                return_value=["configuration"],
            ),
            patch.object(
                self.service,
                "_generate_layer_environment_lines",
                return_value=["layers"],
            ),
            patch.object(
                self.service,
                "_profile_selection_lines",
                return_value=["profile"],
            ),
        ):
            content = self.service._generate_script_content({})

        self.assertIn(
            "host guard\nconfiguration\nlayers\nprofile\n",
            content,
        )

    def test_scaling_settings_remain_toml_only(self):
        scaling = {
            **ConfigurationManager.get_defaults(),
            "scaling_enabled": True,
            "scaling_factor": 1.8,
            "scaling_sharpness": 0.7,
        }

        toml_content = ConfigurationManager.generate_toml_content(scaling)
        wrapper_content = self.service._generate_script_content(scaling)
        wrapper_settings = self.service._wrapper_settings_defaults()

        self.assertIn("scaling_enabled = true", toml_content)
        self.assertIn("scaling_factor = 1.8", toml_content)
        self.assertIn("scaling_sharpness = 0.7", toml_content)
        for field in (
            "scaling_enabled",
            "scaling_factor",
            "scaling_sharpness",
        ):
            with self.subTest(field=field):
                self.assertNotIn(field, wrapper_settings)
                self.assertNotIn(field, wrapper_content.lower())
        self.assertNotIn("MAKO_SCALING", wrapper_content)

    def test_unsupported_host_passthrough_bytes_are_characterized(self):
        lines = [
            "#!/bin/bash",
            self.service._WRAPPER_FORMAT_MARKER,
            self.service._HOST_COMPATIBILITY_MARKER,
            "# MAKO Renderer is unavailable for this native host in this release.",
            *self.service._generate_unsupported_host_passthrough_lines(),
        ]
        content = "\n".join(lines) + "\n"

        self.assertEqual(len(content.encode("utf-8")), 515)
        self.assertEqual(
            _sha256(content),
            "04a2deb158c55bf4086978be025f64185a421bce729e82a05907cca53e7d60b2",
        )

    def test_profile_sidecar_bytes_are_characterized(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            self.service.config_dir = Path(temp_dir)
            self.service.wrapper_profile_settings_path = (
                self.service.config_dir / "profile-wrapper-settings.json"
            )
            self.service.profile_metadata_path = (
                self.service.config_dir / "profile-metadata.json"
            )
            self.service._write_wrapper_profile_settings({
                "cool-game": {
                    "force_alsa_audio": True,
                    "external_vulkan_layer": "mangohud",
                    "retired_option": "discarded",
                },
            })
            self.service._write_profile_metadata({
                "cool-game": {
                    "display_name": "Cool Game",
                    "kind": "game",
                    "steam_app_id": "12345",
                    "captured_processes": ["CoolGame.exe"],
                },
            })
            wrapper_content = self.service.wrapper_profile_settings_path.read_text(
                encoding="utf-8"
            )
            metadata_content = self.service.profile_metadata_path.read_text(
                encoding="utf-8"
            )

        stored_wrapper_settings = json.loads(wrapper_content)["profiles"][
            "cool-game"
        ]
        self.assertNotIn("retired_option", stored_wrapper_settings)
        self.assertEqual(
            _sha256(wrapper_content),
            "28040de9fc3c8c0909013ee71449cc0439d8bb5bf96ed83eb3df1069df5acf9c",
        )
        self.assertEqual(
            _sha256(metadata_content),
            "11e17020c31dbb6c868fd268eb26715dfcd7cefe8bbaff3e47c709a4df573147",
        )


if __name__ == "__main__":
    unittest.main()
