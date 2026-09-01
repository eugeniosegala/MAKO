"""Deterministic tests for the generated Vulkan-layer search environment."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


class _Logger:
    def __getattr__(self, _name):
        return lambda *_args, **_kwargs: None


sys.modules.setdefault("decky", SimpleNamespace(logger=_Logger()))

from py_modules.mako_plugin import configuration as configuration_module  # noqa: E402
from py_modules.mako_plugin.configuration import (  # noqa: E402
    ConfigurationManager,
    ConfigurationService,
)
from py_modules.mako_plugin.config_schema import (  # noqa: E402
    CONFIG_SCHEMA,
    ProfileData,
)
from py_modules.mako_plugin.config_schema_generated import (  # noqa: E402
    ALL_FIELDS,
    get_script_generation_logic,
    get_script_parsing_logic,
)


class WrapperEnvironmentTests(unittest.TestCase):
    def setUp(self):
        self.service = ConfigurationService(
            logger=_Logger(),
            development_build=False,
        )
        self.service.local_share_dir = Path("/private/mako/implicit_layer.d")
        self.service.spatial_scaling_layer_dir = Path(
            "/private/mako/spatial_scaling.d"
        )
        self.service.gamescope_wsi_compatibility_dir = Path(
            "/private/mako/gamescope_wsi_compatibility.d"
        )
        self.service.mangohud_layer_dir = Path("/private/mako/mangohud.d")
        self.service.vkbasalt_layer_dir = Path("/private/mako/vkbasalt.d")
        self.gamescope_environment = {
            "GAMESCOPE_WAYLAND_DISPLAY": "gamescope-0",
        }
        self.last_stderr = ""

    def _evaluate(self, extra_environment=None, config=None):
        lines = []
        if config is not None:
            lines.extend(self.service._script_configuration_lines(config))
        lines.extend(self.service._generate_layer_environment_lines())
        script = "\n".join(lines + [
            'printf "ADD=%s\\n" "${VK_ADD_IMPLICIT_LAYER_PATH:-}"',
            'printf "IMPLICIT=%s\\n" "${VK_IMPLICIT_LAYER_PATH:-}"',
            'printf "ENABLE=%s\\n" "${ENABLE_MAKO:-}"',
            'printf "SPLIT=%s\\n" "${MAKO_SPLIT_LAYER_CHAIN:-}"',
            'printf "DISABLE_LSFG=%s\\n" "${DISABLE_LSFG:-}"',
            'printf "DISABLE_LSFGVK=%s\\n" "${DISABLE_LSFGVK:-}"',
            'printf "DISABLE_MAKO=%s\\n" "${DISABLE_MAKO:-}"',
            'printf "DISABLE_GAMESCOPE=%s\\n" "${DISABLE_GAMESCOPE_WSI:-}"',
            'printf "ENABLE_GAMESCOPE=%s\\n" "${ENABLE_GAMESCOPE_WSI:-}"',
            'printf "DISABLE_SCALING=%s\\n" "${DISABLE_MAKO_SPATIAL_SCALING:-}"',
            'printf "ENABLE_SCALING=%s\\n" "${ENABLE_MAKO_SPATIAL_SCALING:-}"',
            'printf "HDR_EXPOSURE_DISABLED=%s\\n" "${MAKO_DISABLE_HDR_EXPOSURE:-}"',
            'printf "DXVK_HDR=%s\\n" "${DXVK_HDR:-}"',
            'printf "INSTANCE=%s\\n" "${VK_INSTANCE_LAYERS:-}"',
            'printf "EXTERNAL_SELECTOR=%s\\n" "${MAKO_EXTERNAL_VULKAN_LAYER:-}"',
            'printf "MANGOHUD=%s\\n" "${MANGOHUD:-}"',
            'printf "MANGOHUD_DISABLED=%s\\n" "${DISABLE_MANGOHUD:-}"',
            'printf "MANGOHUD_CONFIG=%s\\n" "${MANGOHUD_CONFIG:-}"',
            'printf "VKBASALT=%s\\n" "${ENABLE_VKBASALT:-}"',
            'printf "VKBASALT_DISABLED=%s\\n" "${DISABLE_VKBASALT:-}"',
            'printf "DEVICE_SELECT_DISABLED=%s\\n" "${NODEVICE_SELECT:-}"',
            'printf "MESA_ANTI_LAG_DISABLED=%s\\n" "${DISABLE_LAYER_MESA_ANTI_LAG:-}"',
        ])
        environment = {
            "PATH": os.environ.get("PATH", ""),
            **(extra_environment or {}),
        }
        result = subprocess.run(
            ["bash", "-c", script],
            check=True,
            capture_output=True,
            text=True,
            env=environment,
        )
        self.last_stderr = result.stderr
        return dict(line.split("=", 1) for line in result.stdout.splitlines())

    def test_host_uses_deterministic_private_layer_boundary(self):
        values = self._evaluate(config=ConfigurationManager.get_defaults())
        self.assertEqual(values["ADD"], "")
        self.assertEqual(values["IMPLICIT"], "/private/mako/implicit_layer.d")
        self.assertEqual(values["ENABLE"], "1")
        self.assertEqual(values["SPLIT"], "")
        self.assertEqual(values["DISABLE_LSFG"], "1")
        self.assertEqual(values["DISABLE_LSFGVK"], "1")
        self.assertEqual(values["DISABLE_GAMESCOPE"], "1")
        self.assertEqual(values["ENABLE_GAMESCOPE"], "")
        self.assertEqual(values["DISABLE_SCALING"], "1")
        self.assertEqual(values["ENABLE_SCALING"], "")
        self.assertEqual(values["DXVK_HDR"], "")
        self.assertEqual(values["INSTANCE"], "")
        self.assertEqual(values["EXTERNAL_SELECTOR"], "")
        self.assertEqual(values["MANGOHUD"], "")
        self.assertEqual(values["VKBASALT"], "")

    def test_scaling_profile_admits_gamescope_presentation_split_at_start(self):
        with (
            tempfile.TemporaryDirectory() as compatibility_dir,
            tempfile.TemporaryDirectory() as scaling_dir,
        ):
            compatibility_path = Path(compatibility_dir)
            (
                compatibility_path /
                configuration_module.GAMESCOPE_WSI_MANIFEST_FILENAME_64
            ).write_text("{}", encoding="utf-8")
            self.service.gamescope_wsi_compatibility_dir = compatibility_path
            scaling_path = Path(scaling_dir)
            (
                scaling_path /
                configuration_module.SPATIAL_SCALING_JSON_FILENAME
            ).write_text("{}", encoding="utf-8")
            self.service.spatial_scaling_layer_dir = scaling_path
            config = ConfigurationManager.get_defaults()
            config["scaling_enabled"] = True
            values = self._evaluate(self.gamescope_environment, config)

        self.assertEqual(
            values["IMPLICIT"],
            f"/private/mako/implicit_layer.d:{compatibility_dir}:{scaling_dir}",
        )
        self.assertEqual(values["DISABLE_GAMESCOPE"], "")
        self.assertEqual(values["ENABLE_GAMESCOPE"], "")
        self.assertEqual(values["SPLIT"], "2")
        self.assertEqual(values["DISABLE_SCALING"], "")
        self.assertEqual(values["ENABLE_SCALING"], "")
        self.assertEqual(values["ENABLE"], "")
        self.assertEqual(
            values["INSTANCE"],
            "VK_LAYER_MAKO_render:"
            "VK_LAYER_FROG_gamescope_wsi_x86_64:"
            "VK_LAYER_MAKO_spatial_scaling",
        )
        self.assertEqual(values["DEVICE_SELECT_DISABLED"], "1")
        self.assertEqual(values["MESA_ANTI_LAG_DISABLED"], "1")

    def test_scaling_profile_fails_closed_without_staged_gamescope_wsi(self):
        with tempfile.TemporaryDirectory() as compatibility_dir:
            self.service.gamescope_wsi_compatibility_dir = Path(
                compatibility_dir
            )
            config = ConfigurationManager.get_defaults()
            config["scaling_enabled"] = True
            values = self._evaluate(
                {
                    **self.gamescope_environment,
                    "ENABLE_GAMESCOPE_WSI": "1",
                },
                config,
            )

        self.assertEqual(values["IMPLICIT"], "/private/mako/implicit_layer.d")
        self.assertEqual(values["DISABLE_GAMESCOPE"], "1")
        self.assertEqual(values["ENABLE_GAMESCOPE"], "")
        self.assertEqual(values["SPLIT"], "")
        self.assertEqual(values["DISABLE_SCALING"], "1")
        self.assertEqual(values["ENABLE_SCALING"], "")

    def test_scaling_profile_fails_closed_without_spatial_manifest(self):
        with tempfile.TemporaryDirectory() as compatibility_dir:
            compatibility_path = Path(compatibility_dir)
            (
                compatibility_path /
                configuration_module.GAMESCOPE_WSI_MANIFEST_FILENAME_64
            ).write_text("{}", encoding="utf-8")
            self.service.gamescope_wsi_compatibility_dir = compatibility_path
            config = ConfigurationManager.get_defaults()
            config["scaling_enabled"] = True
            values = self._evaluate(self.gamescope_environment, config)

        self.assertEqual(values["IMPLICIT"], "/private/mako/implicit_layer.d")
        self.assertEqual(values["DISABLE_GAMESCOPE"], "1")
        self.assertEqual(values["ENABLE_GAMESCOPE"], "")
        self.assertEqual(values["SPLIT"], "")
        self.assertEqual(values["DISABLE_SCALING"], "1")
        self.assertEqual(values["ENABLE_SCALING"], "")

    def test_disabled_profile_does_not_force_the_managed_scaling_chain(self):
        with (
            tempfile.TemporaryDirectory() as compatibility_dir,
            tempfile.TemporaryDirectory() as scaling_dir,
        ):
            compatibility_path = Path(compatibility_dir)
            (
                compatibility_path /
                configuration_module.GAMESCOPE_WSI_MANIFEST_FILENAME_64
            ).write_text("{}", encoding="utf-8")
            self.service.gamescope_wsi_compatibility_dir = compatibility_path
            scaling_path = Path(scaling_dir)
            (
                scaling_path /
                configuration_module.SPATIAL_SCALING_JSON_FILENAME
            ).write_text("{}", encoding="utf-8")
            self.service.spatial_scaling_layer_dir = scaling_path
            config = ConfigurationManager.get_defaults()
            config["scaling_enabled"] = True
            config["disable_mako"] = True
            values = self._evaluate(
                {"VK_INSTANCE_LAYERS": "VK_LAYER_existing"},
                config,
            )

        self.assertEqual(values["INSTANCE"], "VK_LAYER_existing")
        self.assertEqual(values["DISABLE_MAKO"], "1")
        self.assertEqual(values["DISABLE_GAMESCOPE"], "1")
        self.assertEqual(values["DISABLE_SCALING"], "1")

    def test_scaling_engine_combines_gamescope_wsi_with_mangohud(self):
        with (
            tempfile.TemporaryDirectory() as compatibility_dir,
            tempfile.TemporaryDirectory() as scaling_dir,
            tempfile.TemporaryDirectory() as system_dir,
        ):
            compatibility_path = Path(compatibility_dir)
            (
                compatibility_path /
                configuration_module.GAMESCOPE_WSI_MANIFEST_FILENAME_64
            ).write_text("{}", encoding="utf-8")
            self.service.gamescope_wsi_compatibility_dir = compatibility_path
            scaling_path = Path(scaling_dir)
            (
                scaling_path /
                configuration_module.SPATIAL_SCALING_JSON_FILENAME
            ).write_text("{}", encoding="utf-8")
            self.service.spatial_scaling_layer_dir = scaling_path
            self.service.mangohud_layer_dir = Path(system_dir)
            (
                self.service.mangohud_layer_dir /
                configuration_module.MANGOHUD_MANIFEST_FILENAME_64
            ).write_text("{}", encoding="utf-8")
            config = ConfigurationManager.get_defaults()
            config["scaling_enabled"] = True
            config["scaling_method"] = "native"
            config["external_vulkan_layer"] = "mangohud"
            values = self._evaluate(self.gamescope_environment, config)

        self.assertEqual(
            values["IMPLICIT"],
            f"/private/mako/implicit_layer.d:{compatibility_dir}:{scaling_dir}:{system_dir}",
        )
        self.assertEqual(values["MANGOHUD"], "")
        self.assertEqual(values["DISABLE_GAMESCOPE"], "")
        self.assertEqual(values["ENABLE_GAMESCOPE"], "")
        self.assertEqual(values["ENABLE_SCALING"], "")
        self.assertEqual(
            values["INSTANCE"],
            "VK_LAYER_MAKO_render:"
            "VK_LAYER_FROG_gamescope_wsi_x86_64:"
            "VK_LAYER_MAKO_spatial_scaling:"
            "VK_LAYER_MANGOHUD_overlay_x86_64",
        )

    def test_explicit_gamescope_wsi_combines_with_vkbasalt(self):
        with (
            tempfile.TemporaryDirectory() as compatibility_dir,
            tempfile.TemporaryDirectory() as system_dir,
        ):
            compatibility_path = Path(compatibility_dir)
            (
                compatibility_path /
                configuration_module.GAMESCOPE_WSI_MANIFEST_FILENAME_64
            ).write_text("{}", encoding="utf-8")
            self.service.gamescope_wsi_compatibility_dir = compatibility_path
            self.service.vkbasalt_layer_dir = Path(system_dir)
            (
                self.service.vkbasalt_layer_dir /
                configuration_module.VKBASALT_MANIFEST_FILENAME_64
            ).write_text("{}", encoding="utf-8")
            config = ConfigurationManager.get_defaults()
            config["gamescope_wsi_compatibility"] = True
            config["external_vulkan_layer"] = "vkbasalt"
            values = self._evaluate(self.gamescope_environment, config)

        self.assertEqual(
            values["IMPLICIT"],
            f"/private/mako/implicit_layer.d:{compatibility_dir}:{system_dir}",
        )
        self.assertEqual(values["VKBASALT"], "")
        self.assertEqual(values["MANGOHUD"], "")
        self.assertEqual(values["DISABLE_GAMESCOPE"], "")
        self.assertEqual(values["ENABLE_GAMESCOPE"], "")
        self.assertEqual(
            values["INSTANCE"],
            "VK_LAYER_MAKO_render:"
            "VK_LAYER_FROG_gamescope_wsi_x86_64:"
            "VK_LAYER_VKBASALT_post_processing",
        )

    def test_mangohud_profile_admits_only_its_guarded_manifest_directory(self):
        with tempfile.TemporaryDirectory() as system_dir:
            self.service.mangohud_layer_dir = Path(system_dir)
            (
                self.service.mangohud_layer_dir /
                configuration_module.MANGOHUD_MANIFEST_FILENAME_64
            ).write_text("{}", encoding="utf-8")
            config = ConfigurationManager.get_defaults()
            config["external_vulkan_layer"] = "mangohud"
            values = self._evaluate(
                {
                    "MANGOHUD_CONFIG": "fps,frametime,position=top-right",
                    "DISABLE_MANGOHUD": "1",
                },
                config,
            )

        self.assertEqual(
            values["IMPLICIT"],
            f"/private/mako/implicit_layer.d:{system_dir}",
        )
        self.assertEqual(values["MANGOHUD"], "1")
        self.assertEqual(values["MANGOHUD_DISABLED"], "")
        self.assertEqual(
            values["MANGOHUD_CONFIG"],
            "fps,frametime,position=top-right",
        )
        self.assertEqual(values["VKBASALT"], "")
        self.assertEqual(values["DEVICE_SELECT_DISABLED"], "1")
        self.assertEqual(values["MESA_ANTI_LAG_DISABLED"], "1")
        self.assertEqual(values["EXTERNAL_SELECTOR"], "")

    def test_mangohud_profile_retains_guarded_32bit_only_installation(self):
        with tempfile.TemporaryDirectory() as system_dir:
            self.service.mangohud_layer_dir = Path(system_dir)
            (
                self.service.mangohud_layer_dir /
                configuration_module.MANGOHUD_MANIFEST_FILENAME_32
            ).write_text("{}", encoding="utf-8")
            config = ConfigurationManager.get_defaults()
            config["external_vulkan_layer"] = "mangohud"
            values = self._evaluate(config=config)

        self.assertEqual(
            values["IMPLICIT"],
            f"/private/mako/implicit_layer.d:{system_dir}",
        )
        self.assertEqual(values["MANGOHUD"], "1")
        self.assertEqual(values["VKBASALT"], "")

    def test_selected_postprocess_tool_fails_closed_without_staged_manifest(self):
        with tempfile.TemporaryDirectory() as empty_dir:
            self.service.mangohud_layer_dir = Path(empty_dir)
            config = ConfigurationManager.get_defaults()
            config["external_vulkan_layer"] = "mangohud"
            values = self._evaluate(
                {"MANGOHUD": "1", "ENABLE_VKBASALT": "1"},
                config,
            )

        self.assertEqual(values["IMPLICIT"], "/private/mako/implicit_layer.d")
        self.assertEqual(values["MANGOHUD"], "")
        self.assertEqual(values["VKBASALT"], "")
        self.assertEqual(values["DEVICE_SELECT_DISABLED"], "")
        self.assertEqual(values["MESA_ANTI_LAG_DISABLED"], "")

    def test_vkbasalt_profile_is_mutually_exclusive_with_mangohud(self):
        with tempfile.TemporaryDirectory() as system_dir:
            self.service.vkbasalt_layer_dir = Path(system_dir)
            (
                self.service.vkbasalt_layer_dir /
                configuration_module.VKBASALT_MANIFEST_FILENAME_64
            ).write_text("{}", encoding="utf-8")
            config = ConfigurationManager.get_defaults()
            config["external_vulkan_layer"] = "vkbasalt"
            values = self._evaluate(
                {
                    "MANGOHUD": "1",
                    "DISABLE_VKBASALT": "1",
                },
                config,
            )

        self.assertEqual(
            values["IMPLICIT"],
            f"/private/mako/implicit_layer.d:{system_dir}",
        )
        self.assertEqual(values["MANGOHUD"], "")
        self.assertEqual(values["VKBASALT"], "1")
        self.assertEqual(values["VKBASALT_DISABLED"], "")
        self.assertEqual(values["DEVICE_SELECT_DISABLED"], "1")
        self.assertEqual(values["MESA_ANTI_LAG_DISABLED"], "1")

    def test_explicit_gamescope_wsi_compatibility_is_independent(self):
        with tempfile.TemporaryDirectory() as compatibility_dir:
            compatibility_path = Path(compatibility_dir)
            (
                compatibility_path /
                configuration_module.GAMESCOPE_WSI_MANIFEST_FILENAME_64
            ).write_text("{}", encoding="utf-8")
            self.service.gamescope_wsi_compatibility_dir = compatibility_path
            config = ConfigurationManager.get_defaults()
            config["gamescope_wsi_compatibility"] = True
            values = self._evaluate(
                {
                    **self.gamescope_environment,
                    "DISABLE_GAMESCOPE_WSI": "1",
                    "MANGOHUD": "1",
                    "ENABLE_VKBASALT": "1",
                },
                config,
            )

        self.assertEqual(
            values["IMPLICIT"],
            f"/private/mako/implicit_layer.d:{compatibility_dir}",
        )
        self.assertEqual(values["DISABLE_GAMESCOPE"], "")
        self.assertEqual(values["ENABLE_GAMESCOPE"], "")
        self.assertEqual(
            values["INSTANCE"],
            "VK_LAYER_MAKO_render:"
            "VK_LAYER_FROG_gamescope_wsi_x86_64",
        )
        self.assertEqual(values["MANGOHUD"], "")
        self.assertEqual(values["VKBASALT"], "")
        self.assertEqual(values["DEVICE_SELECT_DISABLED"], "1")
        self.assertEqual(values["MESA_ANTI_LAG_DISABLED"], "1")

    def test_gamescope_wsi_compatibility_fails_closed_without_its_manifest(self):
        with tempfile.TemporaryDirectory() as compatibility_dir:
            self.service.gamescope_wsi_compatibility_dir = Path(
                compatibility_dir
            )
            config = ConfigurationManager.get_defaults()
            config["gamescope_wsi_compatibility"] = True
            values = self._evaluate(
                {
                    **self.gamescope_environment,
                    "ENABLE_GAMESCOPE_WSI": "1",
                },
                config,
            )

        self.assertEqual(values["IMPLICIT"], "/private/mako/implicit_layer.d")
        self.assertEqual(values["DISABLE_GAMESCOPE"], "1")
        self.assertEqual(values["ENABLE_GAMESCOPE"], "")

    def test_default_profile_rejects_inherited_external_activation(self):
        values = self._evaluate(
            {
                "MANGOHUD": "1",
                "ENABLE_VKBASALT": "1",
            },
            ConfigurationManager.get_defaults(),
        )

        self.assertEqual(values["IMPLICIT"], "/private/mako/implicit_layer.d")
        self.assertEqual(values["MANGOHUD"], "")
        self.assertEqual(values["VKBASALT"], "")

    def test_external_layer_schema_rejects_unknown_tools(self):
        config = ConfigurationManager.get_defaults()
        config["external_vulkan_layer"] = "renderdoc"

        with self.assertRaisesRegex(ValueError, "external_vulkan_layer"):
            ConfigurationManager.validate_config(config)

    def test_external_layer_schema_rejects_released_v22_gamescope_value(self):
        config = ConfigurationManager.get_defaults()
        config["external_vulkan_layer"] = "gamescope-wsi"

        with self.assertRaisesRegex(ValueError, "external_vulkan_layer"):
            ConfigurationManager.validate_config(config)

    def test_existing_instance_layer_order_is_preserved(self):
        values = self._evaluate({
            "VK_INSTANCE_LAYERS": "VK_LAYER_existing_one:VK_LAYER_existing_two",
        })
        self.assertEqual(
            values["INSTANCE"],
            "VK_LAYER_existing_one:VK_LAYER_existing_two",
        )

    def test_caller_requested_instance_layer_is_untouched(self):
        values = self._evaluate({
            "VK_INSTANCE_LAYERS":
                "VK_LAYER_existing:VK_LAYER_MAKO_render",
        })
        self.assertEqual(
            values["INSTANCE"],
            "VK_LAYER_existing:VK_LAYER_MAKO_render",
        )

    def test_scaling_chain_precedes_caller_requested_instance_layers(self):
        with (
            tempfile.TemporaryDirectory() as compatibility_dir,
            tempfile.TemporaryDirectory() as scaling_dir,
        ):
            compatibility_path = Path(compatibility_dir)
            (
                compatibility_path /
                configuration_module.GAMESCOPE_WSI_MANIFEST_FILENAME_64
            ).write_text("{}", encoding="utf-8")
            self.service.gamescope_wsi_compatibility_dir = compatibility_path
            scaling_path = Path(scaling_dir)
            (
                scaling_path /
                configuration_module.SPATIAL_SCALING_JSON_FILENAME
            ).write_text("{}", encoding="utf-8")
            self.service.spatial_scaling_layer_dir = scaling_path
            config = ConfigurationManager.get_defaults()
            config["scaling_enabled"] = True
            values = self._evaluate(
                {
                    **self.gamescope_environment,
                    "VK_INSTANCE_LAYERS":
                        "VK_LAYER_existing_one:VK_LAYER_existing_two",
                },
                config,
            )

        self.assertEqual(
            values["INSTANCE"],
            "VK_LAYER_MAKO_render:"
            "VK_LAYER_FROG_gamescope_wsi_x86_64:"
            "VK_LAYER_MAKO_spatial_scaling:"
            "VK_LAYER_existing_one:VK_LAYER_existing_two",
        )

    def test_desktop_scaling_skips_gamescope_wsi_without_a_modal_path(self):
        with (
            tempfile.TemporaryDirectory() as compatibility_dir,
            tempfile.TemporaryDirectory() as scaling_dir,
        ):
            compatibility_path = Path(compatibility_dir)
            (
                compatibility_path /
                configuration_module.GAMESCOPE_WSI_MANIFEST_FILENAME_64
            ).write_text("{}", encoding="utf-8")
            self.service.gamescope_wsi_compatibility_dir = compatibility_path
            scaling_path = Path(scaling_dir)
            (
                scaling_path /
                configuration_module.SPATIAL_SCALING_JSON_FILENAME
            ).write_text("{}", encoding="utf-8")
            self.service.spatial_scaling_layer_dir = scaling_path
            config = ConfigurationManager.get_defaults()
            config["scaling_enabled"] = True
            values = self._evaluate(config=config)

        self.assertEqual(values["IMPLICIT"], "/private/mako/implicit_layer.d")
        self.assertEqual(values["DISABLE_GAMESCOPE"], "1")
        self.assertEqual(values["DISABLE_SCALING"], "1")
        self.assertEqual(values["SPLIT"], "")
        self.assertEqual(values["INSTANCE"], "")
        self.assertIn(
            "MAKO Decky: Gamescope WSI skipped: no active Gamescope session",
            self.last_stderr,
        )

    def test_nested_wayland_session_does_not_admit_gamescope_wsi(self):
        with tempfile.TemporaryDirectory() as compatibility_dir:
            compatibility_path = Path(compatibility_dir)
            (
                compatibility_path /
                configuration_module.GAMESCOPE_WSI_MANIFEST_FILENAME_64
            ).write_text("{}", encoding="utf-8")
            self.service.gamescope_wsi_compatibility_dir = compatibility_path
            config = ConfigurationManager.get_defaults()
            config["gamescope_wsi_compatibility"] = True
            values = self._evaluate(
                {
                    "GAMESCOPE_WAYLAND_DISPLAY": "gamescope-0",
                    "WAYLAND_DISPLAY": "wayland-0",
                },
                config,
            )

        self.assertEqual(values["DISABLE_GAMESCOPE"], "1")
        self.assertEqual(values["INSTANCE"], "")
        self.assertIn("no active Gamescope session", self.last_stderr)

    def test_matching_wayland_session_admits_gamescope_wsi(self):
        with tempfile.TemporaryDirectory() as compatibility_dir:
            compatibility_path = Path(compatibility_dir)
            (
                compatibility_path /
                configuration_module.GAMESCOPE_WSI_MANIFEST_FILENAME_64
            ).write_text("{}", encoding="utf-8")
            self.service.gamescope_wsi_compatibility_dir = compatibility_path
            config = ConfigurationManager.get_defaults()
            config["gamescope_wsi_compatibility"] = True
            values = self._evaluate(
                {
                    "GAMESCOPE_WAYLAND_DISPLAY": "gamescope-0",
                    "WAYLAND_DISPLAY": "gamescope-0",
                },
                config,
            )

        self.assertEqual(values["DISABLE_GAMESCOPE"], "")
        self.assertEqual(
            values["INSTANCE"],
            "VK_LAYER_MAKO_render:"
            "VK_LAYER_FROG_gamescope_wsi_x86_64",
        )
        self.assertEqual(self.last_stderr, "")

    def _evaluate_profile_selection(self, lines, extra_environment=None):
        script = "\n".join(lines + [
            'printf "PROFILE=%s\\n" "${MAKO_PROFILE:-}"',
            'printf "FALLBACK=%s\\n" "${MAKO_PROFILE_FALLBACK:-}"',
        ])
        result = subprocess.run(
            ["bash", "-c", script],
            check=True,
            capture_output=True,
            text=True,
            env={"PATH": os.environ.get("PATH", ""), **(extra_environment or {})},
        )
        return dict(line.split("=", 1) for line in result.stdout.splitlines())

    def test_caller_profile_wins_over_the_decky_selected_profile(self):
        lines = self.service._profile_selection_lines(
            "decky-mako",
            {"active_in": ""},
            automatic_matching_enabled=False,
        )

        values = self._evaluate_profile_selection(
            lines,
            {"MAKO_PROFILE": "dolphin-starfox"},
        )

        self.assertEqual(values["PROFILE"], "dolphin-starfox")
        self.assertEqual(values["FALLBACK"], "")

    def test_selected_profile_is_the_fallback_without_a_caller_override(self):
        lines = self.service._profile_selection_lines(
            "decky-mako",
            {"active_in": ""},
            automatic_matching_enabled=False,
        )

        values = self._evaluate_profile_selection(lines)

        self.assertEqual(values["PROFILE"], "")
        self.assertEqual(values["FALLBACK"], "decky-mako")

    def test_automatic_matching_retains_a_low_priority_default_fallback(self):
        lines = self.service._profile_selection_lines(
            "mako",
            {"active_in": "CoolGame.exe"},
            automatic_matching_enabled=True,
        )

        values = self._evaluate_profile_selection(lines)

        self.assertEqual(values["PROFILE"], "")
        self.assertEqual(values["FALLBACK"], "mako")

    def test_existing_additional_paths_are_excluded_on_host(self):
        values = self._evaluate(
            {"VK_ADD_IMPLICIT_LAYER_PATH": "/caller/one:/caller/two"},
            ConfigurationManager.get_defaults(),
        )
        self.assertEqual(values["ADD"], "")
        self.assertEqual(values["IMPLICIT"], "/private/mako/implicit_layer.d")

    def test_caller_override_path_is_replaced_on_host(self):
        values = self._evaluate(
            {
                "VK_IMPLICIT_LAYER_PATH": "/caller/override",
                "VK_ADD_IMPLICIT_LAYER_PATH": "/ignored/by/loader",
            },
            ConfigurationManager.get_defaults(),
        )
        self.assertEqual(values["IMPLICIT"], "/private/mako/implicit_layer.d")
        self.assertEqual(values["ADD"], "")

    def test_sdr_boundary_uses_deterministic_layer_discovery(self):
        values = self._evaluate(
            {
                "MAKO_DISABLE_HDR_EXPOSURE": "1",
                "VK_IMPLICIT_LAYER_PATH": "/caller/override",
                "VK_ADD_IMPLICIT_LAYER_PATH": "/caller/additional",
            },
            {"disable_hdr_exposure": True},
        )
        self.assertEqual(values["IMPLICIT"], "/private/mako/implicit_layer.d")
        self.assertEqual(values["ADD"], "")
        self.assertEqual(values["DISABLE_GAMESCOPE"], "1")
        self.assertEqual(values["ENABLE_GAMESCOPE"], "")
        self.assertEqual(values["DXVK_HDR"], "")

    def test_flatpak_uses_deterministic_extension_boundary(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            previous = configuration_module.FLATPAK_IMPLICIT_LAYER_DIR
            configuration_module.FLATPAK_IMPLICIT_LAYER_DIR = temp_dir
            try:
                values = self._evaluate(config=ConfigurationManager.get_defaults())
            finally:
                configuration_module.FLATPAK_IMPLICIT_LAYER_DIR = previous

        self.assertEqual(values["ADD"], "")
        self.assertEqual(values["IMPLICIT"], temp_dir)
        self.assertEqual(values["ENABLE"], "1")

    def test_flatpak_discards_caller_layer_paths(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            previous = configuration_module.FLATPAK_IMPLICIT_LAYER_DIR
            configuration_module.FLATPAK_IMPLICIT_LAYER_DIR = temp_dir
            try:
                values = self._evaluate(
                    {
                        "VK_IMPLICIT_LAYER_PATH": "/caller/override",
                        "VK_ADD_IMPLICIT_LAYER_PATH": "/caller/additional",
                    }
                )
            finally:
                configuration_module.FLATPAK_IMPLICIT_LAYER_DIR = previous

        self.assertEqual(values["ADD"], "")
        self.assertEqual(values["IMPLICIT"], temp_dir)

    def test_flatpak_does_not_admit_selected_host_external_layer(self):
        with tempfile.TemporaryDirectory() as flatpak_dir:
            with tempfile.TemporaryDirectory() as system_dir:
                self.service.mangohud_layer_dir = Path(system_dir)
                (
                    self.service.mangohud_layer_dir /
                    configuration_module.MANGOHUD_MANIFEST_FILENAME_64
                ).write_text("{}", encoding="utf-8")
                config = ConfigurationManager.get_defaults()
                config["external_vulkan_layer"] = "mangohud"
                with patch.object(
                    configuration_module,
                    "FLATPAK_IMPLICIT_LAYER_DIR",
                    flatpak_dir,
                ):
                    values = self._evaluate(config=config)

        self.assertEqual(values["IMPLICIT"], flatpak_dir)
        self.assertEqual(values["MANGOHUD"], "")
        self.assertEqual(values["VKBASALT"], "")

    def test_flatpak_admits_staged_gamescope_wsi_compatibility_mode(self):
        with tempfile.TemporaryDirectory() as flatpak_dir:
            with tempfile.TemporaryDirectory() as compatibility_dir:
                compatibility_path = Path(compatibility_dir)
                (
                    compatibility_path /
                    configuration_module.GAMESCOPE_WSI_MANIFEST_FILENAME_64
                ).write_text("{}", encoding="utf-8")
                self.service.gamescope_wsi_compatibility_dir = (
                    compatibility_path
                )
                config = ConfigurationManager.get_defaults()
                config["gamescope_wsi_compatibility"] = True
                with patch.object(
                    configuration_module,
                    "FLATPAK_IMPLICIT_LAYER_DIR",
                    flatpak_dir,
                ):
                    values = self._evaluate(
                        self.gamescope_environment,
                        config,
                    )

        self.assertEqual(
            values["IMPLICIT"],
            f"{flatpak_dir}:{compatibility_path}",
        )
        self.assertEqual(values["DISABLE_GAMESCOPE"], "")
        self.assertEqual(values["ENABLE_GAMESCOPE"], "")
        self.assertEqual(
            values["INSTANCE"],
            "VK_LAYER_MAKO_render:VK_LAYER_FROG_gamescope_wsi_x86_64",
        )

    def test_flatpak_scaling_uses_packaged_spatial_layer_after_staged_wsi(self):
        with tempfile.TemporaryDirectory() as flatpak_dir:
            with tempfile.TemporaryDirectory() as compatibility_dir:
                compatibility_path = Path(compatibility_dir)
                (
                    compatibility_path /
                    configuration_module.GAMESCOPE_WSI_MANIFEST_FILENAME_64
                ).write_text("{}", encoding="utf-8")
                self.service.gamescope_wsi_compatibility_dir = (
                    compatibility_path
                )
                (
                    Path(flatpak_dir) /
                    configuration_module.SPATIAL_SCALING_JSON_FILENAME
                ).write_text("{}", encoding="utf-8")
                config = ConfigurationManager.get_defaults()
                config["scaling_enabled"] = True
                with patch.object(
                    configuration_module,
                    "FLATPAK_IMPLICIT_LAYER_DIR",
                    flatpak_dir,
                ):
                    values = self._evaluate(
                        self.gamescope_environment,
                        config,
                    )

        self.assertEqual(
            values["IMPLICIT"],
            f"{flatpak_dir}:{compatibility_path}",
        )
        self.assertEqual(values["DISABLE_GAMESCOPE"], "")
        self.assertEqual(values["ENABLE_GAMESCOPE"], "")
        self.assertEqual(values["SPLIT"], "2")
        self.assertEqual(values["DISABLE_SCALING"], "")
        self.assertEqual(values["ENABLE_SCALING"], "")
        self.assertEqual(
            values["INSTANCE"],
            "VK_LAYER_MAKO_render:VK_LAYER_FROG_gamescope_wsi_x86_64:VK_LAYER_MAKO_spatial_scaling",
        )

    def test_direct_flatpak_shortcut_receives_ordered_chain_per_launch(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            flatpak_layer_dir = "/usr/lib/extensions/vulkan/"
            flatpak_layer_dir += "makorender/share/vulkan/implicit_layer.d"
            compatibility_path = root / "gamescope-wsi"
            compatibility_path.mkdir()
            (
                compatibility_path /
                configuration_module.GAMESCOPE_WSI_MANIFEST_FILENAME_64
            ).write_text("{}", encoding="utf-8")
            self.service.gamescope_wsi_compatibility_dir = compatibility_path
            self.service.config_dir = root / "config"
            self.service.config_file_path = self.service.config_dir / "conf.toml"

            config = ConfigurationManager.get_defaults()
            config["scaling_enabled"] = True
            with patch.object(
                configuration_module,
                "FLATPAK_IMPLICIT_LAYER_DIR",
                str(flatpak_layer_dir),
            ):
                wrapper = root / "mako-run"
                wrapper.write_text(
                    self.service._generate_script_content(config),
                    encoding="utf-8",
                )
            wrapper.chmod(0o755)

            captured = root / "flatpak-arguments"
            fake_flatpak = root / "flatpak"
            fake_flatpak.write_text(
                "#!/bin/bash\nprintf '%s\\n' \"$@\" > \"$MAKO_CAPTURE\"\n",
                encoding="utf-8",
            )
            fake_flatpak.chmod(0o755)

            subprocess.run(
                [
                    str(wrapper),
                    str(fake_flatpak),
                    "run",
                    "--branch=stable",
                    "org.DolphinEmu.dolphin-emu",
                    "--batch",
                ],
                check=True,
                env={
                    "PATH": os.environ.get("PATH", ""),
                    "MAKO_CAPTURE": str(captured),
                    **self.gamescope_environment,
                },
            )

            arguments = captured.read_text(encoding="utf-8").splitlines()
            self.assertEqual(arguments[0], "run")
            self.assertIn("--env=MAKO_SPLIT_LAYER_CHAIN=2", arguments)
            self.assertIn(
                "--env=VK_INSTANCE_LAYERS="
                "VK_LAYER_MAKO_render:"
                "VK_LAYER_FROG_gamescope_wsi_x86_64:"
                "VK_LAYER_MAKO_spatial_scaling",
                arguments,
            )
            self.assertIn(
                f"--env=VK_IMPLICIT_LAYER_PATH={flatpak_layer_dir}:"
                f"{compatibility_path}",
                arguments,
            )
            self.assertIn("--unset-env=DISABLE_GAMESCOPE_WSI", arguments)
            self.assertIn("--unset-env=ENABLE_MAKO", arguments)
            self.assertEqual(
                arguments[-3:],
                [
                    "--branch=stable",
                    "org.DolphinEmu.dolphin-emu",
                    "--batch",
                ],
            )

    def test_flatpak_sdr_boundary_keeps_gamescope_wsi_out_of_umu_chain(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            previous = configuration_module.FLATPAK_IMPLICIT_LAYER_DIR
            configuration_module.FLATPAK_IMPLICIT_LAYER_DIR = temp_dir
            try:
                values = self._evaluate(
                    {
                        "MAKO_DISABLE_HDR_EXPOSURE": "1",
                        "VK_IMPLICIT_LAYER_PATH": "/caller/override",
                        "VK_ADD_IMPLICIT_LAYER_PATH": "/caller/additional",
                        "ENABLE_GAMESCOPE_WSI": "1",
                    },
                    {"disable_hdr_exposure": True},
                )
            finally:
                configuration_module.FLATPAK_IMPLICIT_LAYER_DIR = previous

        self.assertEqual(values["IMPLICIT"], temp_dir)
        self.assertEqual(values["ADD"], "")
        self.assertEqual(values["ENABLE_GAMESCOPE"], "")
        self.assertEqual(values["DISABLE_GAMESCOPE"], "1")
        self.assertEqual(values["DXVK_HDR"], "")

    def test_hdr_recovery_profile_generates_wrapper_export(self):
        lines = get_script_generation_logic()({"disable_hdr_exposure": True})
        self.assertIn("export MAKO_DISABLE_HDR_EXPOSURE=1", lines)

    def test_full_wrapper_emits_forced_hdr_disable_once(self):
        script = self.service._generate_script_content(
            ConfigurationManager.get_defaults()
        )
        self.assertEqual(
            script.count("export MAKO_DISABLE_HDR_EXPOSURE=1"),
            1,
        )

    def test_required_wrapper_markers_are_unique(self):
        self.assertEqual(
            len(self.service._REQUIRED_WRAPPER_EXPORTS),
            len(set(self.service._REQUIRED_WRAPPER_EXPORTS)),
        )

    def test_freshly_generated_default_wrapper_is_current(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            self.service.mako_script_path = Path(temp_dir) / "wrapper"
            self.service.mako_script_path.write_text(
                self.service._generate_script_content(
                    ConfigurationManager.get_defaults()
                ),
                encoding="utf-8",
            )

            self.assertFalse(self.service.migrate_launch_script_if_needed())

    def test_hdr_is_blocked_by_default(self):
        self.assertTrue(CONFIG_SCHEMA["disable_hdr_exposure"].default)
        settings = self.service._wrapper_settings_defaults()
        self.assertTrue(settings["disable_hdr_exposure"])

    def test_saved_hdr_test_opt_in_is_overridden(self):
        settings = self.service._normalize_wrapper_settings({
            "disable_hdr_exposure": False,
        })
        self.assertTrue(settings["disable_hdr_exposure"])

    def test_released_v22_gamescope_selector_survives_wrapper_regeneration(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
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
            self.service.gamescope_wsi_compatibility_dir = (
                root / "gamescope_wsi_compatibility.d"
            )
            self.service.gamescope_wsi_compatibility_dir.mkdir(parents=True)
            (
                self.service.gamescope_wsi_compatibility_dir /
                configuration_module.GAMESCOPE_WSI_MANIFEST_FILENAME_64
            ).write_text("{}", encoding="utf-8")

            defaults = ConfigurationManager.get_defaults()
            profile_data = ProfileData(
                current_profile="mako",
                profiles={"mako": defaults},
                global_config={
                    "dll": defaults["dll"],
                    "allow_fp16": defaults["allow_fp16"],
                },
            )
            self.service._save_profile_data(profile_data)
            self.service.migrate_profile_metadata_if_needed()
            self.service.wrapper_profile_settings_path.write_text(
                json.dumps({
                    "version": 1,
                    "profiles": {
                        "mako": {
                            "external_vulkan_layer": "gamescope-wsi",
                        },
                    },
                }) + "\n",
                encoding="utf-8",
            )
            self.service.mako_script_path.parent.mkdir(parents=True)
            self.service.mako_script_path.write_text(
                "\n".join([
                    "#!/bin/bash",
                    "# mako-wrapper-format: 45",
                    'mako_wrapper_profile="${MAKO_PROFILE:-mako}"',
                    'case "$mako_wrapper_profile" in',
                    "    mako)",
                    "        export MAKO_EXTERNAL_VULKAN_LAYER=gamescope-wsi",
                    "        ;;",
                    "esac",
                    'exec "$@"',
                    "",
                ]),
                encoding="utf-8",
            )

            self.assertFalse(
                self.service.migrate_wrapper_profile_settings_if_needed()
            )
            with patch.object(
                    configuration_module,
                    "FLATPAK_IMPLICIT_LAYER_DIR",
                    str(root / "absent-flatpak-extension"),
            ):
                self.assertTrue(self.service.migrate_launch_script_if_needed())

            settings = self.service._read_wrapper_profile_settings()["mako"]
            self.assertTrue(settings["gamescope_wsi_compatibility"])
            self.assertEqual(settings["external_vulkan_layer"], "")

            generated_wrapper = self.service.mako_script_path.read_text(
                encoding="utf-8"
            )
            self.assertIn(self.service._WRAPPER_FORMAT_MARKER, generated_wrapper)
            self.assertNotIn("gamescope-wsi", generated_wrapper)

            probe = "\n".join([
                'printf "IMPLICIT=%s\\n" "${VK_IMPLICIT_LAYER_PATH:-}"',
                'printf "ENABLE=%s\\n" "${ENABLE_GAMESCOPE_WSI:-}"',
                'printf "DISABLE=%s\\n" "${DISABLE_GAMESCOPE_WSI:-}"',
                'printf "INSTANCE=%s\\n" "${VK_INSTANCE_LAYERS:-}"',
                'printf "SELECTOR=%s\\n" "${MAKO_EXTERNAL_VULKAN_LAYER:-}"',
            ])
            result = subprocess.run(
                [
                    str(self.service.mako_script_path),
                    "/bin/bash",
                    "-c",
                    probe,
                ],
                check=True,
                capture_output=True,
                text=True,
                env={
                    "PATH": os.environ.get("PATH", ""),
                    **self.gamescope_environment,
                },
            )
            values = dict(
                line.split("=", 1) for line in result.stdout.splitlines()
            )
            self.assertEqual(
                values["IMPLICIT"],
                f"{self.service.local_share_dir}:"
                f"{self.service.gamescope_wsi_compatibility_dir}",
            )
            self.assertEqual(values["ENABLE"], "")
            self.assertEqual(values["DISABLE"], "")
            self.assertEqual(
                values["INSTANCE"],
                "VK_LAYER_MAKO_render:"
                "VK_LAYER_FROG_gamescope_wsi_x86_64",
            )
            self.assertEqual(values["SELECTOR"], "")

    def test_explicit_hdr_test_opt_in_remains_blocked(self):
        lines = self.service._hdr_activation_lines({
            "disable_hdr_exposure": False,
        })
        self.assertEqual(lines, [
            "export MAKO_DISABLE_HDR_EXPOSURE=1",
            "unset DXVK_HDR",
        ])

        values = self._evaluate(
            {
                "VK_INSTANCE_LAYERS":
                    "VK_LAYER_existing_one:VK_LAYER_existing_two",
                "ENABLE_GAMESCOPE_WSI": "1",
                "ENABLE_MAKO": "1",
            },
            {"disable_hdr_exposure": False},
        )
        self.assertEqual(
            values["INSTANCE"],
            "VK_LAYER_existing_one:VK_LAYER_existing_two",
        )
        self.assertEqual(values["ENABLE"], "1")
        self.assertEqual(values["ENABLE_GAMESCOPE"], "")
        self.assertEqual(values["HDR_EXPOSURE_DISABLED"], "1")
        self.assertEqual(values["DXVK_HDR"], "")
        self.assertEqual(values["DISABLE_MAKO"], "")
        self.assertEqual(values["DISABLE_GAMESCOPE"], "1")

    def test_default_sdr_profile_never_exports_hdr_bootstrap(self):
        lines = self.service._hdr_activation_lines({
            "disable_hdr_exposure": True,
        })
        self.assertEqual(lines, [
            "export MAKO_DISABLE_HDR_EXPOSURE=1",
            "unset DXVK_HDR",
        ])
        values = self._evaluate(
            {"VK_INSTANCE_LAYERS": "VK_LAYER_existing"},
            {"disable_hdr_exposure": True},
        )
        self.assertEqual(values["INSTANCE"], "VK_LAYER_existing")
        self.assertEqual(values["HDR_EXPOSURE_DISABLED"], "1")
        self.assertEqual(values["DXVK_HDR"], "")
        self.assertEqual(values["ENABLE_GAMESCOPE"], "")
        self.assertEqual(values["DISABLE_MAKO"], "")
        self.assertEqual(values["DISABLE_GAMESCOPE"], "1")

    def test_full_layer_disable_keeps_hdr_exposure_blocked(self):
        lines = self.service._hdr_activation_lines({
            "disable_hdr_exposure": False,
            "disable_mako": True,
        })
        self.assertIn("unset DXVK_HDR", lines)
        self.assertNotIn("export DISABLE_GAMESCOPE_WSI=1", lines)
        self.assertNotIn("VK_INSTANCE_LAYERS", "\n".join(lines))

    def test_full_layer_disable_targets_mako_identity(self):
        lines = get_script_generation_logic()({"disable_mako": True})
        self.assertIn("export DISABLE_MAKO=1", lines)

    def test_mako_disable_export_enables_full_layer_toggle(self):
        values = get_script_parsing_logic()([
            "export ENABLE_MAKO=1",
        ])
        self.assertNotIn("disable_mako", values)

        values = get_script_parsing_logic()([
            "export DISABLE_MAKO=1",
        ])
        self.assertTrue(values["disable_mako"])

    def test_wrapper_never_exports_obsolete_wow64_workaround(self):
        self.assertNotIn("enable_wow64", ALL_FIELDS)
        lines = get_script_generation_logic()({"enable_wow64": True})
        self.assertNotIn("export PROTON_USE_WOW64=1", lines)

    def test_base_fps_cap_is_engine_owned(self):
        self.assertIn("base_fps_cap", ALL_FIELDS)
        self.assertNotIn("dxvk_frame_rate", ALL_FIELDS)
        lines = get_script_generation_logic()({"base_fps_cap": 60})
        self.assertNotIn("export DXVK_FRAME_RATE=60", lines)

    def test_adaptive_auto_cap_is_engine_owned_and_serialized(self):
        self.assertIn("adaptive_auto_base_fps_cap", ALL_FIELDS)
        config = ConfigurationManager.get_defaults()
        self.assertTrue(config["adaptive_auto_base_fps_cap"])
        config["adaptive"] = True
        config["adaptive_auto_base_fps_cap"] = True
        config["target_fps"] = 165
        toml = ConfigurationManager.generate_toml_content(config)
        self.assertIn("adaptive_auto_base_fps_cap = true", toml)
        self.assertIn("target_fps = 165", toml)
        self.assertNotIn("ADAPTIVE_AUTO_BASE_FPS_CAP", "\n".join(
            get_script_generation_logic()(config)
        ))

    def test_wrapper_never_exports_obsolete_recreation_request(self):
        lines = self.service._generate_layer_environment_lines()
        self.assertFalse(any(
            "MAKO_PRESENT_RECOVERY_RECREATE" in line for line in lines
        ))

    def test_published_wrapper_keeps_diagnostics_opt_in(self):
        lines = self.service._generate_layer_environment_lines()
        self.assertIn(
            'export MAKO_PRESENT_DIAGNOSTICS="${MAKO_PRESENT_DIAGNOSTICS:-0}"',
            lines,
        )
        self.assertEqual(
            self.service._diagnostics_default_marker(),
            "# development presentation diagnostics default: disabled",
        )

    def test_desktop_gamescope_wsi_skip_is_written_to_diagnostics_log(self):
        with (
            tempfile.TemporaryDirectory() as compatibility_dir,
            tempfile.TemporaryDirectory() as scaling_dir,
            tempfile.TemporaryDirectory() as diagnostics_dir,
        ):
            compatibility_path = Path(compatibility_dir)
            (
                compatibility_path /
                configuration_module.GAMESCOPE_WSI_MANIFEST_FILENAME_64
            ).write_text("{}", encoding="utf-8")
            self.service.gamescope_wsi_compatibility_dir = compatibility_path
            scaling_path = Path(scaling_dir)
            (
                scaling_path /
                configuration_module.SPATIAL_SCALING_JSON_FILENAME
            ).write_text("{}", encoding="utf-8")
            self.service.spatial_scaling_layer_dir = scaling_path
            log_path = Path(diagnostics_dir) / "present-diagnostics.log"
            config = ConfigurationManager.get_defaults()
            config["scaling_enabled"] = True
            script = "\n".join([
                *self.service._script_configuration_lines(config),
                *self.service._generate_layer_environment_lines(),
            ])

            result = subprocess.run(
                ["bash", "-c", script],
                check=False,
                capture_output=True,
                text=True,
                env={
                    "PATH": os.environ.get("PATH", ""),
                    "MAKO_PRESENT_DIAGNOSTICS": "1",
                    "MAKO_PRESENT_DIAGNOSTICS_LOG": str(log_path),
                },
            )
            log_contents = log_path.read_text(encoding="utf-8")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertIn(
            "MAKO Decky: Gamescope WSI skipped: no active Gamescope session",
            log_contents,
        )

    def test_enabled_diagnostics_retains_exactly_three_launch_sessions(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            log_path = Path(temporary_directory) / "present-diagnostics.log"
            script = "\n".join([
                *self.service._generate_layer_environment_lines(),
                'printf "%s\\n" "$MAKO_TEST_SESSION" >&2',
            ])

            for session in ("run-one", "run-two", "run-three", "run-four"):
                result = subprocess.run(
                    ["bash", "-c", script],
                    check=False,
                    capture_output=True,
                    text=True,
                    env={
                        **os.environ,
                        "MAKO_PRESENT_DIAGNOSTICS": "1",
                        "MAKO_PRESENT_DIAGNOSTICS_LOG": str(log_path),
                        "MAKO_TEST_SESSION": session,
                    },
                )
                self.assertEqual(result.returncode, 0, result.stderr)

            self.assertEqual(log_path.read_text(encoding="utf-8"), "run-four\n")
            self.assertEqual(
                Path(f"{log_path}.1").read_text(encoding="utf-8"),
                "run-three\n",
            )
            self.assertEqual(
                Path(f"{log_path}.2").read_text(encoding="utf-8"),
                "run-two\n",
            )
            self.assertEqual(
                sorted(path.name for path in log_path.parent.iterdir()),
                [
                    "present-diagnostics.log",
                    "present-diagnostics.log.1",
                    "present-diagnostics.log.2",
                ],
            )

    def test_disabled_diagnostics_preserves_existing_session_history(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            log_path = Path(temporary_directory) / "present-diagnostics.log"
            previous_path = Path(f"{log_path}.1")
            oldest_path = Path(f"{log_path}.2")
            log_path.write_text("latest\n", encoding="utf-8")
            previous_path.write_text("previous\n", encoding="utf-8")
            oldest_path.write_text("oldest\n", encoding="utf-8")
            script = "\n".join(
                self.service._generate_layer_environment_lines()
            )

            result = subprocess.run(
                ["bash", "-c", script],
                check=False,
                capture_output=True,
                text=True,
                env={
                    **os.environ,
                    "MAKO_PRESENT_DIAGNOSTICS": "0",
                    "MAKO_PRESENT_DIAGNOSTICS_LOG": str(log_path),
                },
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(log_path.read_text(encoding="utf-8"), "latest\n")
            self.assertEqual(
                previous_path.read_text(encoding="utf-8"),
                "previous\n",
            )
            self.assertEqual(
                oldest_path.read_text(encoding="utf-8"),
                "oldest\n",
            )

    def test_development_wrapper_keeps_diagnostics_opt_in(self):
        service = ConfigurationService(
            logger=_Logger(),
            development_build=True,
        )
        lines = service._generate_layer_environment_lines()
        self.assertIn(
            'export MAKO_PRESENT_DIAGNOSTICS="${MAKO_PRESENT_DIAGNOSTICS:-0}"',
            lines,
        )
        self.assertEqual(
            service._diagnostics_default_marker(),
            "# development presentation diagnostics default: disabled",
        )

    def test_unsupported_armada_host_bypasses_mako_and_wraps_once(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            device_env = temp_path / "device-env"
            game_launch = temp_path / "armada-game-launch"
            game = temp_path / "game"
            test_bin = temp_path / "test-bin"
            test_bin.mkdir()
            test_uname = test_bin / "uname"
            test_uname.write_text(
                "#!/bin/sh\nprintf 'x86_64\\n'\n",
                encoding="utf-8",
            )
            test_uname.chmod(0o755)
            test_environment = {
                **os.environ,
                "PATH": f"{test_bin}:{os.environ.get('PATH', '')}",
            }
            game_launch.write_text(
                "#!/bin/bash\nprintf 'armada\\n'\nexec \"$@\"\n",
                encoding="utf-8",
            )
            game_launch.chmod(0o755)
            game.write_text(
                "#!/bin/bash\nprintf 'game enable=%s disable=%s\\n' "
                '"${ENABLE_MAKO:-}" "${DISABLE_MAKO:-}"\n',
                encoding="utf-8",
            )
            game.chmod(0o755)

            with (
                patch.object(configuration_module, "ARMADA_DEVICE_ENV", device_env),
                patch.object(configuration_module, "ARMADA_GAME_LAUNCH", game_launch),
            ):
                lines = self.service._generate_host_compatibility_guard_lines()
            lines.extend([
                "export ENABLE_MAKO=1",
                'exec "$@"',
            ])

            direct = subprocess.run(
                ["bash", "-c", "\n".join(lines), "mako-test", str(game)],
                check=True,
                capture_output=True,
                text=True,
                env=test_environment,
            )
            self.assertEqual(direct.stdout, "game enable=1 disable=\n")

            device_env.write_text("", encoding="utf-8")
            wrapped = subprocess.run(
                ["bash", "-c", "\n".join(lines), "mako-test", str(game)],
                check=True,
                capture_output=True,
                text=True,
                env=test_environment,
            )
            self.assertEqual(
                wrapped.stdout,
                "armada\ngame enable= disable=1\n",
            )

            already_wrapped = subprocess.run(
                [
                    "bash", "-c", "\n".join(lines), "mako-test",
                    str(game_launch), str(game),
                ],
                check=True,
                capture_output=True,
                text=True,
                env=test_environment,
            )
            self.assertEqual(
                already_wrapped.stdout,
                "armada\ngame enable= disable=1\n",
            )

    def test_native_aarch64_host_bypasses_without_armada_marker(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            test_bin = temp_path / "test-bin"
            test_bin.mkdir()
            test_uname = test_bin / "uname"
            test_uname.write_text(
                "#!/bin/sh\nprintf 'aarch64\\n'\n",
                encoding="utf-8",
            )
            test_uname.chmod(0o755)
            game = temp_path / "game"
            game.write_text(
                "#!/bin/sh\nprintf 'enable=%s disable=%s\\n' "
                '"${ENABLE_MAKO:-}" "${DISABLE_MAKO:-}"\n',
                encoding="utf-8",
            )
            game.chmod(0o755)

            with (
                patch.object(
                    configuration_module,
                    "ARMADA_DEVICE_ENV",
                    temp_path / "missing-device-env",
                ),
                patch.object(
                    configuration_module,
                    "ARMADA_GAME_LAUNCH",
                    temp_path / "missing-armada-game-launch",
                ),
            ):
                lines = self.service._generate_host_compatibility_guard_lines()
            lines.extend(["export ENABLE_MAKO=1", 'exec "$@"'])

            result = subprocess.run(
                ["bash", "-c", "\n".join(lines), "mako-test", str(game)],
                check=True,
                capture_output=True,
                text=True,
                env={
                    **os.environ,
                    "PATH": f"{test_bin}:{os.environ.get('PATH', '')}",
                },
            )

            self.assertEqual(result.stdout, "enable= disable=1\n")

    def test_generated_wrapper_places_host_guard_before_mako_exports(self):
        script = self.service._generate_script_content(
            ConfigurationManager.get_defaults()
        )

        self.assertLess(
            script.index(self.service._HOST_COMPATIBILITY_MARKER),
            script.index("export ENABLE_MAKO=1"),
        )

    def test_diagnostics_default_marker_change_regenerates_wrapper(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            self.service.mako_script_path = Path(temp_dir) / "wrapper"
            self.service.mako_script_path.write_text(
                self.service._generate_script_content(
                    ConfigurationManager.get_defaults()
                ).replace(
                    self.service._diagnostics_default_marker(),
                    "# development presentation diagnostics default: enabled",
                ),
                encoding="utf-8",
            )
            self.service._get_profile_data = lambda: {}
            self.service.update_mako_script_from_profile_data = (
                lambda _profile_data: {"success": True}
            )

            self.assertTrue(self.service.migrate_launch_script_if_needed())

    def test_any_non_current_wrapper_format_is_regenerated(self):
        current_script = self.service._generate_script_content(
            ConfigurationManager.get_defaults()
        )
        for stale_version in (1, 41, 999):
            with self.subTest(stale_version=stale_version):
                with tempfile.TemporaryDirectory() as temp_dir:
                    self.service.mako_script_path = Path(temp_dir) / "wrapper"
                    self.service.mako_script_path.write_text(
                        current_script.replace(
                            self.service._WRAPPER_FORMAT_MARKER,
                            f"# mako-wrapper-format: {stale_version}",
                        ),
                        encoding="utf-8",
                    )
                    self.service._get_profile_data = lambda: {}
                    self.service.update_mako_script_from_profile_data = (
                        lambda _profile_data: {"success": True}
                    )

                    self.assertTrue(
                        self.service.migrate_launch_script_if_needed()
                    )

    def test_obsolete_wow64_profile_setting_is_discarded(self):
        settings = self.service._normalize_wrapper_settings({
            "enable_wow64": True,
            "unknown_launcher_option": "unsafe",
            "disable_hdr_exposure": True,
        })
        self.assertNotIn("enable_wow64", settings)
        self.assertNotIn("unknown_launcher_option", settings)
        self.assertTrue(settings["disable_hdr_exposure"])

    def test_unknown_wrapper_settings_version_falls_back_safely(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            self.service.wrapper_profile_settings_path = (
                Path(temp_dir) / "wrapper-settings.json"
            )
            self.service.wrapper_profile_settings_path.write_text(
                json.dumps({
                    "version": 99,
                    "profiles": {
                        "mako": {"disable_mako": True},
                    },
                }),
                encoding="utf-8",
            )

            self.assertEqual(self.service._read_wrapper_profile_settings(), {})

    def test_current_wrapper_with_any_obsolete_export_is_regenerated(self):
        current_script = self.service._generate_script_content(
            ConfigurationManager.get_defaults()
        )
        for obsolete_export in self.service._OBSOLETE_WRAPPER_EXPORTS:
            with self.subTest(obsolete_export=obsolete_export):
                with tempfile.TemporaryDirectory() as temp_dir:
                    self.service.mako_script_path = Path(temp_dir) / "wrapper"
                    self.service.mako_script_path.write_text(
                        f"{current_script}\nexport {obsolete_export}=1\n",
                        encoding="utf-8",
                    )
                    self.service._get_profile_data = lambda: {}
                    self.service.update_mako_script_from_profile_data = (
                        lambda _profile_data: {"success": True}
                    )

                    self.assertTrue(
                        self.service.migrate_launch_script_if_needed()
                    )

    def test_legacy_dxvk_cap_migrates_into_engine_profile(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            self.service.config_dir = temp_path
            self.service.config_file_path = temp_path / "conf.toml"
            self.service.wrapper_profile_settings_path = temp_path / "wrapper.json"
            self.service.mako_script_path = temp_path / "wrapper"
            self.service.config_file_path.write_text(
                "\n".join([
                    "version = 2",
                    '# decky-current-profile = "decky-mako"',
                    "[global]",
                    "allow_fp16 = true",
                    "[[profile]]",
                    'name = "decky-mako"',
                    "multiplier = 2",
                    "",
                ]),
                encoding="utf-8",
            )
            self.service.wrapper_profile_settings_path.write_text(
                json.dumps({
                    "version": 1,
                    "profiles": {
                        "decky-mako": {
                            "dxvk_frame_rate": 60,
                            "disable_hdr_exposure": True,
                        },
                    },
                }),
                encoding="utf-8",
            )
            self.service.mako_script_path.write_text(
                "export DXVK_FRAME_RATE=60\n",
                encoding="utf-8",
            )

            self.assertTrue(
                self.service.migrate_legacy_base_fps_caps_if_needed()
            )
            profile_data = self.service._get_profile_data()
            self.assertEqual(
                profile_data["profiles"]["decky-mako"]["base_fps_cap"],
                60,
            )
            self.assertNotIn(
                "DXVK_FRAME_RATE",
                self.service.mako_script_path.read_text(encoding="utf-8"),
            )


if __name__ == "__main__":
    unittest.main()
