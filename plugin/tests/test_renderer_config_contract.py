"""Keep Decky's profile allowlist aligned with Renderer TOML fields."""

from pathlib import Path
import json
import re
import unittest

from shared_config import (
    ADAPTIVE_MAX_MULTIPLIER_MAX,
    ADAPTIVE_MAX_MULTIPLIER_MIN,
    ADAPTIVE_MINIMUM_BASE_FPS,
    BASE_FPS_CAP_MAX,
    BASE_FPS_CAP_MIN,
    CONFIG_SCHEMA_DEF,
    GPU_HEADROOM_PERCENT_VALUES,
    DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_MAX,
    DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_MIN,
    DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_VALUES,
    FIXED_MULTIPLIER_UI_MAX,
    FIXED_MULTIPLIER_UI_MIN,
    FLOW_SCALE_MAX,
    FLOW_SCALE_MIN,
    FRAME_GENERATION_REFRESH_THRESHOLD_MAX,
    FRAME_GENERATION_REFRESH_THRESHOLD_MIN,
    SCALING_FACTOR_MAX,
    SCALING_FACTOR_MIN,
    SCALING_METHOD_MAKO,
    SCALING_SHARPNESS_MAX,
    SCALING_SHARPNESS_MIN,
    TARGET_FPS_MAX,
    TARGET_FPS_MIN,
    ULTRA_PERFORMANCE_FLOW_SCALE,
    ULTRA_PERFORMANCE_MAKO_FG_FLOW_SCALE,
)
from py_modules.mako_plugin.config_schema import CONFIG_FORMAT_VERSION
from py_modules.mako_plugin.launcher_exclusions_generated import EXCLUDED_WINDOWS_LAUNCHERS
from py_modules.mako_plugin.constants import (
    CONFIG_DIR,
    CONFIG_FILENAME,
    COMPETING_LSFG_DISABLE_ENVS,
    DXVK_HDR_ENV,
    GAMESCOPE_WSI_DISABLE_ENV,
    GAMESCOPE_WSI_ENABLE_ENV,
    HDR_EXPOSURE_DISABLE_ENV,
    MAKO_CONFIG_ENV,
    MAKO_LAYER_DISABLE_ENV,
    MAKO_LAYER_ENABLE_ENV,
    MAKO_PROFILE_ENV,
    MAKO_PROFILE_FALLBACK_ENV,
    MANGOHUD_CONFIG_FILENAME,
    MANGOHUD_CONFIG_FILE_ENV,
    PRESENT_ACQUIRE_TIMEOUT_ENV,
    PRESENT_DIAGNOSTICS_ENV,
    PRESENT_DIAGNOSTICS_LOG_ENV,
    PRESENT_DIAGNOSTICS_LOG_FILENAME,
    PRESENT_DIAGNOSTICS_RETAINED_SESSION_COUNT,
    VK_ADD_IMPLICIT_LAYER_PATH_ENV,
    VK_IMPLICIT_LAYER_PATH_ENV,
)


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RENDERER_CONFIG_SOURCE = (
    REPOSITORY_ROOT / "engine/mako-common/src/configuration/config.cpp"
)


class RendererConfigContractTests(unittest.TestCase):
    def test_gpu_headroom_presets_match_native_renderer(self):
        source = (REPOSITORY_ROOT / "engine/mako-common/include/mako-common/configuration/config.hpp").read_text()
        presets = re.search(r"gpuHeadroomPercentPresets\{([^}]+)\}", source)
        self.assertIsNotNone(presets)
        self.assertEqual(tuple(int(value.strip()) for value in presets[1].split(",")), GPU_HEADROOM_PERCENT_VALUES)
        self.assertEqual(CONFIG_SCHEMA_DEF["gpu_headroom_percent"]["default"], 0)

    def test_launcher_exclusions_match_renderer_activation_guard(self):
        source = (REPOSITORY_ROOT /
                  "engine/mako-common/src/configuration/launcher_exclusions_generated.hpp").read_text()
        declaration = re.search(
            r"excludedWindowsLauncherExecutables\s*\{(.*?)\n    \};",
            source, re.DOTALL,
        )
        self.assertIsNotNone(declaration)
        names = re.findall(r'std::string_view\{"([^"\n]+)"\}', declaration[1])
        entries = json.loads((REPOSITORY_ROOT /
                              "engine/mako-common/launcher_exclusions.json").read_text())
        expected = {name.lower() for entry in entries for name in entry["executables"]}
        self.assertEqual(set(names), expected)
        self.assertEqual(EXCLUDED_WINDOWS_LAUNCHERS, expected)

    def test_decky_toml_fields_match_renderer_parser_and_writer(self):
        source = RENDERER_CONFIG_SOURCE.read_text(encoding="utf-8")
        decky_fields = {
            name
            for name, field in CONFIG_SCHEMA_DEF.items()
            if field["location"] in {"global", "toml", "profile"}
        }
        parser_fields = set(
            re.findall(r'tbl\s*\[\s*"([^"]+)"\s*\]', source)
        )
        writer_fields = set(
            re.findall(
                r'(?:global|profile)\.insert\(\s*"([^"]+)"',
                source,
            )
        )

        # Renderer owns the structural profile name; Decky owns it separately
        # from the configurable field allowlist.
        renderer_profile_fields = parser_fields - {"name"}
        renderer_written_fields = writer_fields - {"name"}

        self.assertEqual(renderer_profile_fields, decky_fields)
        self.assertEqual(renderer_written_fields, decky_fields)

    def test_decky_ranges_and_defaults_fit_renderer_acceptance(self):
        source = (
            REPOSITORY_ROOT
            / "engine/mako-common/include/mako-common/configuration/config.hpp"
        ).read_text(encoding="utf-8")

        def renderer_constant(name: str) -> float:
            match = re.search(
                rf"static constexpr [^;=]+\s+{name}\s*=\s*([0-9.]+)F?;",
                source,
            )
            self.assertIsNotNone(match, f"missing Renderer constant {name}")
            return float(match.group(1))

        ranges = (
            (
                BASE_FPS_CAP_MIN,
                BASE_FPS_CAP_MAX,
                CONFIG_SCHEMA_DEF["base_fps_cap"]["default"],
                renderer_constant("minimumBaseFpsCap"),
                renderer_constant("maximumBaseFpsCap"),
            ),
            (
                TARGET_FPS_MIN,
                TARGET_FPS_MAX,
                CONFIG_SCHEMA_DEF["target_fps"]["default"],
                renderer_constant("minimumTargetFps"),
                renderer_constant("maximumTargetFps"),
            ),
            (
                FRAME_GENERATION_REFRESH_THRESHOLD_MIN,
                FRAME_GENERATION_REFRESH_THRESHOLD_MAX,
                CONFIG_SCHEMA_DEF["frame_generation_refresh_threshold"]["default"],
                renderer_constant("minimumFrameGenerationRefreshThreshold"),
                renderer_constant("maximumFrameGenerationRefreshThreshold"),
            ),
            (
                ADAPTIVE_MAX_MULTIPLIER_MIN,
                ADAPTIVE_MAX_MULTIPLIER_MAX,
                CONFIG_SCHEMA_DEF["adaptive_max_multiplier"]["default"],
                renderer_constant("minimumAdaptiveMaxMultiplier"),
                renderer_constant("maximumAdaptiveMaxMultiplier"),
            ),
            (
                DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_MIN,
                DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_MAX,
                CONFIG_SCHEMA_DEF[
                    "dynamic_cadence_probe_interval_seconds"
                ]["default"],
                renderer_constant(
                    "minimumDynamicCadenceProbeIntervalSeconds"
                ),
                renderer_constant(
                    "maximumDynamicCadenceProbeIntervalSeconds"
                ),
            ),
            (
                FLOW_SCALE_MIN,
                FLOW_SCALE_MAX,
                CONFIG_SCHEMA_DEF["flow_scale"]["default"],
                renderer_constant("minimumFlowScale"),
                renderer_constant("maximumFlowScale"),
            ),
            (
                SCALING_FACTOR_MIN,
                SCALING_FACTOR_MAX,
                CONFIG_SCHEMA_DEF["scaling_factor"]["default"],
                renderer_constant("minimumScalingFactor"),
                renderer_constant("maximumScalingFactor"),
            ),
            (
                SCALING_SHARPNESS_MIN,
                SCALING_SHARPNESS_MAX,
                CONFIG_SCHEMA_DEF["scaling_sharpness"]["default"],
                renderer_constant("minimumScalingSharpness"),
                renderer_constant("maximumScalingSharpness"),
            ),
        )
        for (
            decky_minimum,
            decky_maximum,
            default,
            renderer_minimum,
            renderer_maximum,
        ) in ranges:
            self.assertGreaterEqual(decky_minimum, renderer_minimum)
            self.assertLessEqual(decky_maximum, renderer_maximum)
            self.assertGreaterEqual(default, renderer_minimum)
            self.assertLessEqual(default, renderer_maximum)

        self.assertGreaterEqual(
            FIXED_MULTIPLIER_UI_MIN,
            renderer_constant("minimumMultiplier"),
        )
        self.assertLessEqual(
            FIXED_MULTIPLIER_UI_MAX,
            renderer_constant("maximumMultiplier"),
        )
        self.assertEqual(
            CONFIG_SCHEMA_DEF[
                "dynamic_cadence_probe_interval_seconds"
            ]["default"],
            renderer_constant("dynamicCadenceProbeIntervalSeconds"),
        )
        self.assertEqual(
            ULTRA_PERFORMANCE_FLOW_SCALE,
            renderer_constant("ultraPerformanceFlowScale"),
        )
        self.assertEqual(
            ULTRA_PERFORMANCE_MAKO_FG_FLOW_SCALE,
            renderer_constant("ultraPerformanceMakoFgFlowScale"),
        )
        self.assertEqual(
            CONFIG_SCHEMA_DEF["flow_scale"]["default"],
            renderer_constant("flowScale"),
        )
        self.assertEqual(
            CONFIG_SCHEMA_DEF["scaling_factor"]["default"],
            renderer_constant("scalingFactor"),
        )
        self.assertEqual(
            CONFIG_SCHEMA_DEF["scaling_sharpness"]["default"],
            renderer_constant("scalingSharpness"),
        )
        self.assertFalse(CONFIG_SCHEMA_DEF["scaling_enabled"]["default"])
        self.assertIn(
            "static constexpr bool scalingEnabled = false;",
            source,
        )
        self.assertEqual(
            CONFIG_SCHEMA_DEF["scaling_method"]["default"],
            SCALING_METHOD_MAKO,
        )
        self.assertIn(
            "static constexpr ScalingMethod scalingMethod = ScalingMethod::Mako;",
            source,
        )
        self.assertEqual(
            CONFIG_SCHEMA_DEF["frame_generation_model"]["default"],
            "mako-fg",
        )
        self.assertIn(
            "static constexpr FrameGenerationModel frameGenerationModel = FrameGenerationModel::MakoFg;",
            source,
        )

        preset_match = re.search(
            r"dynamicCadenceProbeIntervalPresetsSeconds\s*\{([^}]*)\}",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(
            preset_match,
            "missing Renderer cadence probe interval presets",
        )
        renderer_presets = tuple(
            float(value)
            for value in re.findall(r"([0-9.]+)F", preset_match.group(1))
        )
        self.assertEqual(
            DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_VALUES,
            renderer_presets,
        )

    def test_config_format_version_matches_renderer(self):
        source = (
            REPOSITORY_ROOT
            / "engine/mako-common/include/mako-common/configuration/config.hpp"
        ).read_text(encoding="utf-8")
        match = re.search(
            r"static constexpr int64_t\s+formatVersion\s*=\s*([0-9]+);",
            source,
        )
        self.assertIsNotNone(match)
        self.assertEqual(CONFIG_FORMAT_VERSION, int(match.group(1)))

    def test_pacing_value_matches_renderer_parser_and_writer(self):
        source = RENDERER_CONFIG_SOURCE.read_text(encoding="utf-8")
        pacing = CONFIG_SCHEMA_DEF["pacing"]["default"]

        self.assertEqual(pacing, "none")
        self.assertIn(f'if (str == "{pacing}")', source)
        self.assertIn(f'profile.insert("pacing", "{pacing}")', source)

    def test_renderer_ui_exposes_profile_controls(self):
        ui_source = (
            REPOSITORY_ROOT / "engine/mako-ui/rsc/UI.qml"
        ).read_text(encoding="utf-8")
        backend_source = (
            REPOSITORY_ROOT / "engine/mako-ui/src/backend.hpp"
        ).read_text(encoding="utf-8")

        for field, property_type in (
            ("scaling_enabled", "bool"),
            ("scaling_factor", "float"),
            ("scaling_sharpness", "float"),
            ("frame_generation_refresh_threshold", "uint"),
            ("dynamic_cadence_recovery", "bool"),
            ("dynamic_cadence_probe_interval_seconds", "double"),
            ("ultra_performance", "bool"),
        ):
            with self.subTest(field=field):
                self.assertIn(f"backend.{field}", ui_source)
                self.assertIn(
                    f"Q_PROPERTY({property_type} {field}", backend_source
                )

        self.assertNotIn("backend.pacing_mode", ui_source)
        self.assertNotIn("Q_PROPERTY(int pacing_mode", backend_source)

    def test_renderer_ui_exposes_safe_standalone_launch_controls(self):
        ui_source = (
            REPOSITORY_ROOT / "engine/mako-ui/rsc/UI.qml"
        ).read_text(encoding="utf-8")
        backend_source = (
            REPOSITORY_ROOT / "engine/mako-ui/src/backend.hpp"
        ).read_text(encoding="utf-8")

        for field in (
            "enable_zink",
            "force_alsa_audio",
            "enable_vkbasalt",
            "enable_mangohud",
            "mangohud_available",
            "mangohud_checking",
            "mangohud_count_real_frames",
        ):
            with self.subTest(field=field):
                self.assertIn(f"backend.{field}", ui_source)
                self.assertIn(f"Q_PROPERTY(bool {field}", backend_source)

        for field, property_type in (
            ("vkbasalt_sharpening", "QString"),
            ("vkbasalt_sharpness", "float"),
            ("vkbasalt_dls_denoise", "float"),
            ("vkbasalt_antialiasing", "QString"),
            ("vkbasalt_shader", "QString"),
        ):
            with self.subTest(field=field):
                self.assertIn(f"backend.{field}", ui_source)
                self.assertIn(
                    f"Q_PROPERTY({property_type} {field}", backend_source
                )

        for unsupported_layer_control in (
            "disable_steamdeck_mode",
            "external_vulkan_layer",
            "gamescope_wsi",
        ):
            with self.subTest(control=unsupported_layer_control):
                self.assertNotIn(
                    f"backend.{unsupported_layer_control}",
                    ui_source.lower(),
                )

    def test_native_mangohud_uses_the_shared_profile_and_layout_contract(self):
        backend = (REPOSITORY_ROOT / "engine/mako-ui/src/backend.cpp").read_text(encoding="utf-8")
        launcher = (REPOSITORY_ROOT / "engine/scripts/mako-launch").read_text(encoding="utf-8")
        self.assertIn('QStringLiteral("external_vulkan_layer")', backend)
        self.assertIn('QStringLiteral("mangohud")', backend)
        self.assertIn('QStringLiteral("MANGOHUD=1")', backend)
        self.assertIn('"--check-mangohud"', backend)
        self.assertIn("mako-render/" + MANGOHUD_CONFIG_FILENAME, launcher)
        self.assertIn(f'export {MANGOHUD_CONFIG_FILE_ENV}="$mako_mangohud_config"', launcher)
        self.assertIn('layer.get("enable_environment") != {"MANGOHUD": "1"}', launcher)
        self.assertIn('layer.get("disable_environment") != {"DISABLE_MANGOHUD": "1"}', launcher)
        # Qt stores Decky's per-profile frame-count field and hands the same
        # choice to the standalone launcher through its copied launch option.
        self.assertIn("mangohud_count_real_frames", CONFIG_SCHEMA_DEF)
        self.assertIn('QStringLiteral("mangohud_count_real_frames")', backend)
        self.assertIn('QStringLiteral("MAKO_MANGOHUD_REAL_FRAMES=1")', backend)
        self.assertIn('mako_mangohud_real_frames="${MAKO_MANGOHUD_REAL_FRAMES:-0}"', launcher)

    def test_adaptive_minimum_base_fps_matches_renderer_policy(self):
        source = (
            REPOSITORY_ROOT
            / "engine/mako-render/src/adaptive_policy_limits.hpp"
        ).read_text(encoding="utf-8")
        match = re.search(
            r"adaptiveMinimumBaseFps\s*=\s*([0-9.]+);",
            source,
        )
        self.assertIsNotNone(match)
        self.assertEqual(ADAPTIVE_MINIMUM_BASE_FPS, float(match.group(1)))

    def test_launcher_environment_names_match_renderer_consumers(self):
        consumers = {
            MAKO_CONFIG_ENV: "engine/mako-common/src/configuration/config.cpp",
            MAKO_PROFILE_ENV: "engine/mako-common/src/configuration/detection.cpp",
            MAKO_PROFILE_FALLBACK_ENV: (
                "engine/mako-common/src/configuration/detection.cpp"
            ),
            PRESENT_ACQUIRE_TIMEOUT_ENV: "engine/mako-render/src/swapchain/create.cpp",
            PRESENT_DIAGNOSTICS_ENV: (
                "engine/mako-render/src/present_diagnostics.cpp"
            ),
            PRESENT_DIAGNOSTICS_LOG_ENV: "scripts/mako-diagnostics",
            HDR_EXPOSURE_DISABLE_ENV: "engine/mako-render/src/instance.cpp",
            DXVK_HDR_ENV: "engine/mako-render/src/instance.cpp",
            MAKO_LAYER_DISABLE_ENV: "engine/mako-render/src/instance.cpp",
            GAMESCOPE_WSI_DISABLE_ENV: "engine/mako-render/src/instance.cpp",
            GAMESCOPE_WSI_ENABLE_ENV: (
                "engine/mako-render/src/gamescope_hdr_feedback.cpp"
            ),
        }
        for environment_name, relative_source in consumers.items():
            with self.subTest(environment_name=environment_name):
                source = (REPOSITORY_ROOT / relative_source).read_text(
                    encoding="utf-8"
                )
                self.assertIn(environment_name, source)

    def test_diagnostic_and_config_paths_match_root_tools(self):
        diagnostic_relative_path = (
            f"{CONFIG_DIR}/{PRESENT_DIAGNOSTICS_LOG_FILENAME}"
        )
        config_relative_path = f"{CONFIG_DIR}/{CONFIG_FILENAME}"
        collector = (REPOSITORY_ROOT / "scripts/mako-diagnostics").read_text(
            encoding="utf-8"
        )
        trace_capture = (REPOSITORY_ROOT / "scripts/capture-trace.sh").read_text(
            encoding="utf-8"
        )

        self.assertIn(diagnostic_relative_path, collector)
        self.assertIn(diagnostic_relative_path, trace_capture)
        self.assertIn(config_relative_path, trace_capture)
        self.assertIn(
            f"retained_session_count={PRESENT_DIAGNOSTICS_RETAINED_SESSION_COUNT}",
            collector,
        )

        renderer_config = RENDERER_CONFIG_SOURCE.read_text(encoding="utf-8")
        config_directory = Path(CONFIG_DIR)
        renderer_suffix = (
            f'/ "{config_directory.name}" / "{CONFIG_FILENAME}"'
        )
        self.assertEqual(renderer_config.count(renderer_suffix), 2)
        self.assertRegex(
            renderer_config,
            rf'/\s*"{re.escape(config_directory.parent.name)}"\s*'
            rf'/\s*"{re.escape(config_directory.name)}"\s*'
            rf'/\s*"{re.escape(CONFIG_FILENAME)}"',
        )
        self.assertIn(
            f'"/etc/{config_directory.name}/{CONFIG_FILENAME}"',
            renderer_config,
        )

    def test_standalone_launcher_shares_core_environment_ids(self):
        launcher = (
            REPOSITORY_ROOT / "engine/scripts/mako-launch"
        ).read_text(encoding="utf-8")
        shared_identifiers = (
            *COMPETING_LSFG_DISABLE_ENVS,
            VK_IMPLICIT_LAYER_PATH_ENV,
            VK_ADD_IMPLICIT_LAYER_PATH_ENV,
            GAMESCOPE_WSI_DISABLE_ENV,
            GAMESCOPE_WSI_ENABLE_ENV,
            HDR_EXPOSURE_DISABLE_ENV,
            DXVK_HDR_ENV,
            MAKO_LAYER_ENABLE_ENV,
        )
        for environment_name in shared_identifiers:
            with self.subTest(environment_name=environment_name):
                self.assertIn(environment_name, launcher)


if __name__ == "__main__":
    unittest.main()
