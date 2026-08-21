import { describe, expect, test } from "vitest";
import { getDefaults } from "../../src/config/configSchema";
import {
  adaptiveModeChanges,
  fractionalAdaptivePresetChanges,
  isFractionalAdaptivePresetEnabled
} from "../../src/config/fractionalAdaptivePreset";

describe("fractional Adaptive preset", () => {
  test("selects the default Steady Base Cap when Adaptive is enabled", () => {
    expect(adaptiveModeChanges(true)).toEqual({
      adaptive: true,
      adaptive_auto_base_fps_cap: true
    });
    expect(adaptiveModeChanges(false)).toEqual({
      adaptive: false
    });
  });

  test("new profiles start with Steady 2x", () => {
    const config = getDefaults();

    expect(config.adaptive_auto_base_fps_cap).toBe(true);
    expect(isFractionalAdaptivePresetEnabled({
      ...config,
      adaptive: true
    })).toBe(false);
  });

  test("derives its state from the three compatible settings", () => {
    const enabledConfig = {
      ...getDefaults(),
      ...fractionalAdaptivePresetChanges(true)
    };

    expect(isFractionalAdaptivePresetEnabled(enabledConfig)).toBe(true);
    expect(isFractionalAdaptivePresetEnabled({
      ...enabledConfig,
      frame_generation_enabled: false
    })).toBe(false);
    expect(isFractionalAdaptivePresetEnabled({
      ...enabledConfig,
      adaptive: false
    })).toBe(false);
    expect(isFractionalAdaptivePresetEnabled({
      ...enabledConfig,
      adaptive_auto_base_fps_cap: true
    })).toBe(false);
    expect(isFractionalAdaptivePresetEnabled({
      ...enabledConfig,
      target_fps: 120
    })).toBe(true);
  });

  test("enables the fractional setup atomically and selects Steady 2x when disabled", () => {
    expect(fractionalAdaptivePresetChanges(true)).toEqual({
      frame_generation_enabled: true,
      adaptive: true,
      adaptive_auto_base_fps_cap: false
    });
    expect(fractionalAdaptivePresetChanges(false)).toEqual({
      adaptive_auto_base_fps_cap: true
    });
  });
});
