"""Keep Decky's profile allowlist aligned with Renderer TOML fields."""

from pathlib import Path
import re
import unittest

from shared_config import (
    ADAPTIVE_MAX_MULTIPLIER_MAX,
    ADAPTIVE_MAX_MULTIPLIER_MIN,
    ADAPTIVE_MINIMUM_BASE_FPS,
    BASE_FPS_CAP_MAX,
    BASE_FPS_CAP_MIN,
    CONFIG_SCHEMA_DEF,
    DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_MAX,
    DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_MIN,
    FIXED_MULTIPLIER_UI_MIN,
    FLOW_SCALE_MAX,
    FLOW_SCALE_MIN,
    FRAME_GENERATION_REFRESH_THRESHOLD_MAX,
    FRAME_GENERATION_REFRESH_THRESHOLD_MIN,
    TARGET_FPS_MAX,
    TARGET_FPS_MIN,
)
from py_modules.mako_plugin.config_schema import CONFIG_FORMAT_VERSION
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
    PRESENT_ACQUIRE_TIMEOUT_ENV,
    PRESENT_DIAGNOSTICS_ENV,
    PRESENT_DIAGNOSTICS_LOG_ENV,
    PRESENT_DIAGNOSTICS_LOG_FILENAME,
    VK_ADD_IMPLICIT_LAYER_PATH_ENV,
    VK_IMPLICIT_LAYER_PATH_ENV,
)


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RENDERER_CONFIG_SOURCE = (
    REPOSITORY_ROOT / "engine/mako-common/src/configuration/config.cpp"
)


class RendererConfigContractTests(unittest.TestCase):
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
        self.assertEqual(
            CONFIG_SCHEMA_DEF[
                "dynamic_cadence_probe_interval_seconds"
            ]["default"],
            renderer_constant("dynamicCadenceProbeIntervalSeconds"),
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
            PRESENT_ACQUIRE_TIMEOUT_ENV: "engine/mako-render/src/swapchain.cpp",
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
