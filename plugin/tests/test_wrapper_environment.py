"""Deterministic tests for the generated Vulkan-layer search environment."""

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
from py_modules.mako_plugin.configuration import ConfigurationService  # noqa: E402
from py_modules.mako_plugin.config_schema import CONFIG_SCHEMA  # noqa: E402
from py_modules.mako_plugin.config_schema_generated import (  # noqa: E402
    ALL_FIELDS,
    get_script_generation_logic,
    get_script_parsing_logic,
)


class WrapperEnvironmentTests(unittest.TestCase):
    def setUp(self):
        self.service = ConfigurationService(logger=_Logger())
        self.service.local_share_dir = Path("/private/mako/implicit_layer.d")

    def _evaluate(self, extra_environment=None, config=None):
        lines = []
        if config is not None:
            lines.extend(self.service._experimental_hdr_activation_lines(config))
        lines.extend(self.service._generate_layer_environment_lines())
        script = "\n".join(lines + [
            'printf "ADD=%s\\n" "${VK_ADD_IMPLICIT_LAYER_PATH:-}"',
            'printf "IMPLICIT=%s\\n" "${VK_IMPLICIT_LAYER_PATH:-}"',
            'printf "ENABLE=%s\\n" "${ENABLE_MAKO:-}"',
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

    def test_host_uses_registered_gated_manifest_without_overriding_discovery(self):
        values = self._evaluate()
        self.assertEqual(values["ADD"], "")
        self.assertEqual(values["IMPLICIT"], "")
        self.assertEqual(values["ENABLE"], "1")
        self.assertEqual(values["INSTANCE"], "")

    def test_existing_instance_layer_order_is_preserved(self):
        values = self._evaluate({
            "VK_INSTANCE_LAYERS": "VK_LAYER_existing_one:VK_LAYER_existing_two",
        })
        self.assertEqual(
            values["INSTANCE"],
            "VK_LAYER_existing_one:VK_LAYER_existing_two",
        )

    def test_caller_requested_experimental_instance_layer_is_untouched(self):
        values = self._evaluate({
            "VK_INSTANCE_LAYERS":
                "VK_LAYER_existing:VK_LAYER_MAKO_render",
        })
        self.assertEqual(
            values["INSTANCE"],
            "VK_LAYER_existing:VK_LAYER_MAKO_render",
        )

    def test_existing_additional_paths_are_untouched_on_host(self):
        values = self._evaluate({
            "VK_ADD_IMPLICIT_LAYER_PATH": "/caller/one:/caller/two",
        })
        self.assertEqual(
            values["ADD"],
            "/caller/one:/caller/two",
        )

    def test_caller_override_path_is_untouched_on_host(self):
        values = self._evaluate({
            "VK_IMPLICIT_LAYER_PATH": "/caller/override",
            "VK_ADD_IMPLICIT_LAYER_PATH": "/ignored/by/loader",
        })
        self.assertEqual(values["IMPLICIT"], "/caller/override")
        self.assertEqual(values["ADD"], "/ignored/by/loader")

    def test_hdr_recovery_uses_legacy_isolation(self):
        values = self._evaluate({
            "MAKO_DISABLE_HDR_EXPOSURE": "1",
            "VK_IMPLICIT_LAYER_PATH": "/caller/override",
            "VK_ADD_IMPLICIT_LAYER_PATH": "/caller/additional",
        })
        self.assertEqual(values["IMPLICIT"], "/private/mako/implicit_layer.d")
        self.assertEqual(values["ADD"], "")

    def test_flatpak_adds_wrapper_scoped_extension_without_hiding_other_layers(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            previous = configuration_module.FLATPAK_IMPLICIT_LAYER_DIR
            configuration_module.FLATPAK_IMPLICIT_LAYER_DIR = temp_dir
            try:
                values = self._evaluate()
            finally:
                configuration_module.FLATPAK_IMPLICIT_LAYER_DIR = previous

        self.assertEqual(values["ADD"], temp_dir)
        self.assertEqual(values["IMPLICIT"], "")
        self.assertEqual(values["ENABLE"], "1")

    def test_flatpak_sdr_boundary_uses_explicit_umu_manifest_path(self):
        with tempfile.TemporaryDirectory() as temp_dir, tempfile.TemporaryDirectory() as gamescope_dir:
            previous = configuration_module.FLATPAK_IMPLICIT_LAYER_DIR
            previous_gamescope = configuration_module.FLATPAK_GAMESCOPE_IMPLICIT_LAYER_DIR
            configuration_module.FLATPAK_IMPLICIT_LAYER_DIR = temp_dir
            configuration_module.FLATPAK_GAMESCOPE_IMPLICIT_LAYER_DIR = gamescope_dir
            try:
                values = self._evaluate({
                    "MAKO_DISABLE_HDR_EXPOSURE": "1",
                    "VK_IMPLICIT_LAYER_PATH": "/caller/override",
                    "VK_ADD_IMPLICIT_LAYER_PATH": "/caller/additional",
                    "ENABLE_GAMESCOPE_WSI": "1",
                })
            finally:
                configuration_module.FLATPAK_IMPLICIT_LAYER_DIR = previous
                configuration_module.FLATPAK_GAMESCOPE_IMPLICIT_LAYER_DIR = previous_gamescope

        self.assertEqual(values["IMPLICIT"], f"{gamescope_dir}:{temp_dir}")
        self.assertEqual(values["ADD"], "")
        self.assertEqual(values["ENABLE_GAMESCOPE"], "1")

    def test_hdr_recovery_profile_generates_wrapper_export(self):
        lines = get_script_generation_logic()({"disable_hdr_exposure": True})
        self.assertIn("export MAKO_DISABLE_HDR_EXPOSURE=1", lines)

    def test_experimental_hdr_is_blocked_by_default(self):
        self.assertTrue(CONFIG_SCHEMA["disable_hdr_exposure"].default)
        settings = self.service._wrapper_settings_defaults()
        self.assertTrue(settings["disable_hdr_exposure"])

    def test_saved_hdr_test_opt_in_is_overridden(self):
        settings = self.service._normalize_wrapper_settings({
            "disable_hdr_exposure": False,
        })
        self.assertTrue(settings["disable_hdr_exposure"])

    def test_explicit_hdr_test_opt_in_remains_blocked(self):
        lines = self.service._experimental_hdr_activation_lines({
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
        self.assertEqual(values["ENABLE_GAMESCOPE"], "1")
        self.assertEqual(values["HDR_EXPOSURE_DISABLED"], "1")
        self.assertEqual(values["DXVK_HDR"], "")
        self.assertEqual(values["DISABLE_MAKO"], "")
        self.assertEqual(values["DISABLE_GAMESCOPE"], "")

    def test_default_sdr_profile_never_exports_hdr_bootstrap(self):
        lines = self.service._experimental_hdr_activation_lines({
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
        self.assertEqual(values["DISABLE_GAMESCOPE"], "")

    def test_full_layer_disable_keeps_hdr_exposure_blocked(self):
        lines = self.service._experimental_hdr_activation_lines({
            "disable_hdr_exposure": False,
            "disable_mako": True,
        })
        self.assertIn("unset DXVK_HDR", lines)
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

    def test_wrapper_never_exports_obsolete_recreation_request(self):
        lines = self.service._generate_layer_environment_lines()
        self.assertFalse(any(
            "MAKO_PRESENT_RECOVERY_RECREATE" in line for line in lines
        ))

    def test_obsolete_wow64_profile_setting_is_discarded(self):
        settings = self.service._normalize_wrapper_settings({
            "enable_wow64": True,
            "disable_hdr_exposure": True,
        })
        self.assertNotIn("enable_wow64", settings)
        self.assertTrue(settings["disable_hdr_exposure"])

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


if __name__ == "__main__":
    unittest.main()
