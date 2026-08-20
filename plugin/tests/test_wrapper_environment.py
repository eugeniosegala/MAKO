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
from py_modules.mako_plugin.config_schema import CONFIG_SCHEMA  # noqa: E402
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

    def _evaluate(self, extra_environment=None, config=None):
        lines = []
        if config is not None:
            lines.extend(self.service._hdr_activation_lines(config))
        lines.extend(self.service._generate_layer_environment_lines())
        script = "\n".join(lines + [
            'printf "ADD=%s\\n" "${VK_ADD_IMPLICIT_LAYER_PATH:-}"',
            'printf "IMPLICIT=%s\\n" "${VK_IMPLICIT_LAYER_PATH:-}"',
            'printf "ENABLE=%s\\n" "${ENABLE_MAKO:-}"',
            'printf "DISABLE_LSFG=%s\\n" "${DISABLE_LSFG:-}"',
            'printf "DISABLE_LSFGVK=%s\\n" "${DISABLE_LSFGVK:-}"',
            'printf "DISABLE_MAKO=%s\\n" "${DISABLE_MAKO:-}"',
            'printf "DISABLE_GAMESCOPE=%s\\n" "${DISABLE_GAMESCOPE_WSI:-}"',
            'printf "ENABLE_GAMESCOPE=%s\\n" "${ENABLE_GAMESCOPE_WSI:-}"',
            'printf "HDR_EXPOSURE_DISABLED=%s\\n" "${MAKO_DISABLE_HDR_EXPOSURE:-}"',
            'printf "DXVK_HDR=%s\\n" "${DXVK_HDR:-}"',
            'printf "INSTANCE=%s\\n" "${VK_INSTANCE_LAYERS:-}"',
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
        return dict(line.split("=", 1) for line in result.stdout.splitlines())

    def test_host_uses_deterministic_private_layer_boundary(self):
        values = self._evaluate(config=ConfigurationManager.get_defaults())
        self.assertEqual(values["ADD"], "")
        self.assertEqual(values["IMPLICIT"], "/private/mako/implicit_layer.d")
        self.assertEqual(values["ENABLE"], "1")
        self.assertEqual(values["DISABLE_LSFG"], "1")
        self.assertEqual(values["DISABLE_LSFGVK"], "1")
        self.assertEqual(values["DISABLE_GAMESCOPE"], "1")
        self.assertEqual(values["ENABLE_GAMESCOPE"], "")
        self.assertEqual(values["DXVK_HDR"], "")
        self.assertEqual(values["INSTANCE"], "")

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
        self.assertFalse(config["adaptive_auto_base_fps_cap"])
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
            )
            self.assertEqual(direct.stdout, "game enable=1 disable=\n")

            device_env.write_text("", encoding="utf-8")
            wrapped = subprocess.run(
                ["bash", "-c", "\n".join(lines), "mako-test", str(game)],
                check=True,
                capture_output=True,
                text=True,
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
            )
            self.assertEqual(
                already_wrapped.stdout,
                "armada\ngame enable= disable=1\n",
            )

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
                "\n".join([
                    self.service._WRAPPER_FORMAT_MARKER,
                    "# development presentation diagnostics default: enabled",
                    *self.service._REQUIRED_WRAPPER_EXPORTS,
                ]),
                encoding="utf-8",
            )
            self.service._get_profile_data = lambda: {}
            self.service.update_mako_script_from_profile_data = (
                lambda _profile_data: {"success": True}
            )

            self.assertTrue(self.service.migrate_launch_script_if_needed())

    def test_format_30_wrapper_is_regenerated(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            self.service.mako_script_path = Path(temp_dir) / "wrapper"
            self.service.mako_script_path.write_text(
                "\n".join([
                    "# mako-wrapper-format: 30",
                    self.service._diagnostics_default_marker(),
                    *self.service._REQUIRED_WRAPPER_EXPORTS,
                ]),
                encoding="utf-8",
            )
            self.service._get_profile_data = lambda: {}
            self.service.update_mako_script_from_profile_data = (
                lambda _profile_data: {"success": True}
            )

            self.assertTrue(self.service.migrate_launch_script_if_needed())

    def test_format_35_wrapper_is_regenerated(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            self.service.mako_script_path = Path(temp_dir) / "wrapper"
            self.service.mako_script_path.write_text(
                "\n".join([
                    "# mako-wrapper-format: 35",
                    self.service._diagnostics_default_marker(),
                    *self.service._REQUIRED_WRAPPER_EXPORTS,
                ]),
                encoding="utf-8",
            )
            self.service._get_profile_data = lambda: {}
            self.service.update_mako_script_from_profile_data = (
                lambda _profile_data: {"success": True}
            )

            self.assertTrue(self.service.migrate_launch_script_if_needed())

    def test_format_36_wrapper_is_regenerated(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            self.service.mako_script_path = Path(temp_dir) / "wrapper"
            self.service.mako_script_path.write_text(
                "\n".join([
                    "# mako-wrapper-format: 36",
                    self.service._diagnostics_default_marker(),
                    *self.service._REQUIRED_WRAPPER_EXPORTS,
                ]),
                encoding="utf-8",
            )
            self.service._get_profile_data = lambda: {}
            self.service.update_mako_script_from_profile_data = (
                lambda _profile_data: {"success": True}
            )

            self.assertTrue(self.service.migrate_launch_script_if_needed())

    def test_format_37_wrapper_is_regenerated(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            self.service.mako_script_path = Path(temp_dir) / "wrapper"
            self.service.mako_script_path.write_text(
                "\n".join([
                    "# mako-wrapper-format: 37",
                    self.service._diagnostics_default_marker(),
                    *self.service._REQUIRED_WRAPPER_EXPORTS,
                ]),
                encoding="utf-8",
            )
            self.service._get_profile_data = lambda: {}
            self.service.update_mako_script_from_profile_data = (
                lambda _profile_data: {"success": True}
            )

            self.assertTrue(self.service.migrate_launch_script_if_needed())

    def test_format_38_wrapper_is_regenerated(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            self.service.mako_script_path = Path(temp_dir) / "wrapper"
            self.service.mako_script_path.write_text(
                "\n".join([
                    "# mako-wrapper-format: 38",
                    self.service._diagnostics_default_marker(),
                    *self.service._REQUIRED_WRAPPER_EXPORTS,
                ]),
                encoding="utf-8",
            )
            self.service._get_profile_data = lambda: {}
            self.service.update_mako_script_from_profile_data = (
                lambda _profile_data: {"success": True}
            )

            self.assertTrue(self.service.migrate_launch_script_if_needed())

    def test_format_39_wrapper_is_regenerated(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            self.service.mako_script_path = Path(temp_dir) / "wrapper"
            self.service.mako_script_path.write_text(
                "\n".join([
                    "# mako-wrapper-format: 39",
                    self.service._diagnostics_default_marker(),
                    *self.service._REQUIRED_WRAPPER_EXPORTS,
                ]),
                encoding="utf-8",
            )
            self.service._get_profile_data = lambda: {}
            self.service.update_mako_script_from_profile_data = (
                lambda _profile_data: {"success": True}
            )

            self.assertTrue(self.service.migrate_launch_script_if_needed())

    def test_format_40_wrapper_is_regenerated(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            self.service.mako_script_path = Path(temp_dir) / "wrapper"
            self.service.mako_script_path.write_text(
                "\n".join([
                    "# mako-wrapper-format: 40",
                    self.service._diagnostics_default_marker(),
                    *self.service._REQUIRED_WRAPPER_EXPORTS,
                ]),
                encoding="utf-8",
            )
            self.service._get_profile_data = lambda: {}
            self.service.update_mako_script_from_profile_data = (
                lambda _profile_data: {"success": True}
            )

            self.assertTrue(self.service.migrate_launch_script_if_needed())

    def test_format_41_wrapper_is_regenerated_for_host_guard(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            self.service.mako_script_path = Path(temp_dir) / "wrapper"
            self.service.mako_script_path.write_text(
                "\n".join([
                    "# mako-wrapper-format: 41",
                    self.service._diagnostics_default_marker(),
                    *self.service._REQUIRED_WRAPPER_EXPORTS,
                ]),
                encoding="utf-8",
            )
            self.service._get_profile_data = lambda: {}
            self.service.update_mako_script_from_profile_data = (
                lambda _profile_data: {"success": True}
            )

            self.assertTrue(self.service.migrate_launch_script_if_needed())

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

    def test_current_marker_with_obsolete_wow64_export_is_regenerated(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            self.service.mako_script_path = Path(temp_dir) / "wrapper"
            self.service.mako_script_path.write_text(
                "\n".join([
                    self.service._WRAPPER_FORMAT_MARKER,
                    *self.service._REQUIRED_WRAPPER_EXPORTS,
                    "export PROTON_USE_WOW64=1",
                ]),
                encoding="utf-8",
            )
            self.service._get_profile_data = lambda: {}
            self.service.update_mako_script_from_profile_data = (
                lambda _profile_data: {"success": True}
            )

            self.assertTrue(self.service.migrate_launch_script_if_needed())

    def test_current_marker_with_obsolete_recreation_export_is_regenerated(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            self.service.mako_script_path = Path(temp_dir) / "wrapper"
            self.service.mako_script_path.write_text(
                "\n".join([
                    self.service._WRAPPER_FORMAT_MARKER,
                    *self.service._REQUIRED_WRAPPER_EXPORTS,
                    "export MAKO_PRESENT_RECOVERY_RECREATE=1",
                ]),
                encoding="utf-8",
            )
            self.service._get_profile_data = lambda: {}
            self.service.update_mako_script_from_profile_data = (
                lambda _profile_data: {"success": True}
            )

            self.assertTrue(self.service.migrate_launch_script_if_needed())

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
