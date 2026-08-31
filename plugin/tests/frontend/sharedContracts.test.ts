import { describe, expect, test } from "vitest";
import {
  ADAPTIVE_MAX_MULTIPLIER_MAX,
  ADAPTIVE_MAX_MULTIPLIER_MIN,
  ADAPTIVE_MINIMUM_BASE_FPS,
  BASE_FPS_CAP_MAX,
  BASE_FPS_CAP_MIN,
  BASE_FPS_CAP_UI_MAX,
  DEFAULT_PROFILE_NAME as generatedDefaultProfileName,
  DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_VALUES,
  EXTERNAL_VULKAN_LAYER_VALUES,
  FIXED_MULTIPLIER_MIN,
  FIXED_MULTIPLIER_UI_MAX,
  FIXED_MULTIPLIER_UI_MIN,
  FLOW_SCALE_MAX,
  FLOW_SCALE_MIN,
  FRAME_GENERATION_REFRESH_THRESHOLD_MAX,
  FRAME_GENERATION_REFRESH_THRESHOLD_MIN,
  FRAME_GENERATION_REFRESH_THRESHOLD_PRESET,
  FRAME_GENERATION_REFRESH_THRESHOLD_UI_MIN,
  MAKO_WRAPPER_RELATIVE_PATH,
  PER_GAME_WRAPPER_FLATPAK_APP_IDS,
  PROFILE_KIND_VALUES,
  SCALING_FACTOR_MAX,
  SCALING_FACTOR_MIN,
  SCALING_SHARPNESS_MAX,
  SCALING_SHARPNESS_MIN,
  SUPPORTED_FLATPAK_RUNTIMES,
  TARGET_FPS_MAX,
  TARGET_FPS_MIN,
  getDefaults,
} from "../../src/config/generatedConfigSchema";
import {
  DEFAULT_PROFILE_NAME,
  SUPPORTED_FLATPAK_RUNTIMES as exportedFlatpakRuntimes,
} from "../../src/config/configSchema";
import { DEFAULT_MAKO_WRAPPER_PATH } from "../../src/config/runtimePaths";

describe("generated cross-language contracts", () => {
  test("preserves the persisted default profile identifier", () => {
    expect(DEFAULT_PROFILE_NAME).toBe(generatedDefaultProfileName);
    expect(DEFAULT_PROFILE_NAME).toBe("mako");
  });

  test("builds the pre-RPC wrapper fallback from the shared relative path", () => {
    expect(MAKO_WRAPPER_RELATIVE_PATH).toBe(".local/bin/mako-run");
    expect(DEFAULT_MAKO_WRAPPER_PATH).toBe(
      `/home/deck/${MAKO_WRAPPER_RELATIVE_PATH}`,
    );
  });

  test("preserves persisted profile category values", () => {
    expect([...PROFILE_KIND_VALUES]).toEqual([
      "default",
      "game",
      "process",
      "manual",
    ]);
  });

  test("preserves ordered Flatpak versions and public RPC status fields", () => {
    expect(exportedFlatpakRuntimes).toBe(SUPPORTED_FLATPAK_RUNTIMES);
    expect(SUPPORTED_FLATPAK_RUNTIMES).toEqual([
      {
        version: "23.08",
        statusField: "installed_23_08",
        i18nKey: "FLATPAK_RUNTIME_VERSION",
      },
      {
        version: "24.08",
        statusField: "installed_24_08",
        i18nKey: "FLATPAK_RUNTIME_VERSION",
      },
      {
        version: "25.08",
        statusField: "installed_25_08",
        i18nKey: "FLATPAK_RUNTIME_VERSION",
      },
    ]);
  });

  test("shares Flatpak apps that require per-game wrapper setup", () => {
    expect([...PER_GAME_WRAPPER_FLATPAK_APP_IDS]).toEqual([
      "com.heroicgameslauncher.hgl",
    ]);
  });

  test("preserves validation limits, narrower UI limits, and external-layer values", () => {
    expect({
      baseFpsCap: [BASE_FPS_CAP_MIN, BASE_FPS_CAP_MAX],
      baseFpsCapUiMax: BASE_FPS_CAP_UI_MAX,
      targetFps: [TARGET_FPS_MIN, TARGET_FPS_MAX],
      adaptiveMaxMultiplier: [
        ADAPTIVE_MAX_MULTIPLIER_MIN,
        ADAPTIVE_MAX_MULTIPLIER_MAX,
      ],
      adaptiveMinimumBaseFps: ADAPTIVE_MINIMUM_BASE_FPS,
      dynamicCadenceProbeIntervals: [
        ...DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_VALUES,
      ],
      flowScale: [FLOW_SCALE_MIN, FLOW_SCALE_MAX],
      scalingFactor: [SCALING_FACTOR_MIN, SCALING_FACTOR_MAX],
      scalingSharpness: [SCALING_SHARPNESS_MIN, SCALING_SHARPNESS_MAX],
      refreshThreshold: [
        FRAME_GENERATION_REFRESH_THRESHOLD_MIN,
        FRAME_GENERATION_REFRESH_THRESHOLD_MAX,
      ],
      refreshThresholdUiMin: FRAME_GENERATION_REFRESH_THRESHOLD_UI_MIN,
      refreshThresholdPreset: FRAME_GENERATION_REFRESH_THRESHOLD_PRESET,
      fixedMultiplierMin: FIXED_MULTIPLIER_MIN,
      fixedMultiplierUi: [FIXED_MULTIPLIER_UI_MIN, FIXED_MULTIPLIER_UI_MAX],
      externalVulkanLayers: [...EXTERNAL_VULKAN_LAYER_VALUES],
    }).toEqual({
      baseFpsCap: [0, 240],
      baseFpsCapUiMax: 120,
      targetFps: [30, 240],
      adaptiveMaxMultiplier: [2, 5],
      adaptiveMinimumBaseFps: 10,
      dynamicCadenceProbeIntervals: [
        0.1,
        0.2,
        0.25,
        0.5,
        0.75,
        1,
        1.5,
        2,
        3,
      ],
      flowScale: [0.25, 1],
      scalingFactor: [1, 2],
      scalingSharpness: [0, 1],
      refreshThreshold: [0, 240],
      refreshThresholdUiMin: 30,
      refreshThresholdPreset: 60,
      fixedMultiplierMin: 2,
      fixedMultiplierUi: [2, 5],
      externalVulkanLayers: ["", "mangohud", "vkbasalt"],
    });
  });

  test("keeps spatial scaling inert by default while retaining its tuning values", () => {
    const defaults = getDefaults();

    expect({
      enabled: defaults.scaling_enabled,
      method: defaults.scaling_method,
      factor: defaults.scaling_factor,
      sharpness: defaults.scaling_sharpness,
    }).toEqual({
      enabled: false,
      method: "ls1",
      factor: 1.8,
      sharpness: 0.8,
    });
  });
});
