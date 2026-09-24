import { describe, expect, test } from "vitest";
import {
  ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_AUTO,
  ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_HIGH,
  ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_LOW,
  ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_MEDIUM,
  ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_VERY_HIGH,
  getDefaults,
} from "../../src/config/configSchema";
import {
  adaptiveModeChanges,
  baseFpsCapChanges,
  dynamicCadenceRecoveryChanges,
  fractionalAdaptivePresetChanges,
  fractionalRealFramePriorityCap,
  fractionalRealFramePriorityChanges,
  isFractionalAdaptivePresetEnabled,
  steadyBaseCapChanges,
} from "../../src/config/fractionalAdaptivePreset";

function applyChanges(
  config: ReturnType<typeof getDefaults>,
  changes: Partial<ReturnType<typeof getDefaults>>,
) {
  return { ...config, ...changes };
}

describe("fractional Adaptive preset", () => {
  test("changes only the generation mode so Adaptive settings remain selected", () => {
    expect(adaptiveModeChanges(true)).toEqual({
      adaptive: true,
    });
    expect(adaptiveModeChanges(false)).toEqual({
      adaptive: false,
    });
  });

  test("new profiles start with Steady 2x", () => {
    const config = getDefaults();

    expect(config.adaptive_auto_base_fps_cap).toBe(true);
    expect(config.adaptive_fractional_real_frame_priority).toBe(
      ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_AUTO,
    );
    expect(
      isFractionalAdaptivePresetEnabled({
        ...config,
        adaptive: true,
      }),
    ).toBe(false);

    const {
      adaptive_auto_base_fps_cap: _missingSteadyCap,
      ...legacyPartialConfig
    } = {
      ...config,
      adaptive: true,
    };
    expect(
      isFractionalAdaptivePresetEnabled(
        legacyPartialConfig as ReturnType<typeof getDefaults>,
      ),
    ).toBe(false);
  });

  test("derives its state from Adaptive and the Steady Base Cap", () => {
    const enabledConfig = {
      ...getDefaults(),
      ...fractionalAdaptivePresetChanges(true),
    };

    expect(isFractionalAdaptivePresetEnabled(enabledConfig)).toBe(true);
    expect(
      isFractionalAdaptivePresetEnabled({
        ...enabledConfig,
        frame_generation_enabled: false,
      }),
    ).toBe(true);
    expect(
      isFractionalAdaptivePresetEnabled({
        ...enabledConfig,
        adaptive: false,
      }),
    ).toBe(false);
    expect(
      isFractionalAdaptivePresetEnabled({
        ...enabledConfig,
        adaptive_auto_base_fps_cap: true,
      }),
    ).toBe(false);
    expect(
      isFractionalAdaptivePresetEnabled({
        ...enabledConfig,
        dynamic_cadence_recovery: true,
      }),
    ).toBe(true);
    expect(
      isFractionalAdaptivePresetEnabled({
        ...enabledConfig,
        target_fps: 120,
      }),
    ).toBe(true);
  });

  test("enables the fractional setup atomically and selects Steady 2x when disabled", () => {
    expect(fractionalAdaptivePresetChanges(true)).toEqual({
      adaptive: true,
      adaptive_auto_base_fps_cap: false,
      dynamic_cadence_recovery: false,
    });
    expect(fractionalAdaptivePresetChanges(false)).toEqual({
      adaptive_auto_base_fps_cap: true,
      dynamic_cadence_recovery: false,
    });
  });

  test("keeps the latest Recovery-driven preset when returning to Adaptive", () => {
    let config = {
      ...getDefaults(),
      adaptive: false,
      adaptive_auto_base_fps_cap: true,
      base_fps_cap: 30,
    };

    config = applyChanges(config, dynamicCadenceRecoveryChanges(true));
    expect(config).toMatchObject({
      adaptive: false,
      adaptive_auto_base_fps_cap: false,
      base_fps_cap: 0,
      dynamic_cadence_recovery: true,
    });

    config = applyChanges(config, adaptiveModeChanges(true));
    expect(isFractionalAdaptivePresetEnabled(config)).toBe(true);
    expect(config.dynamic_cadence_recovery).toBe(true);
  });

  test("keeps later cap changes authoritative when returning to Adaptive", () => {
    let config = {
      ...getDefaults(),
      adaptive: false,
      adaptive_auto_base_fps_cap: false,
      base_fps_cap: 0,
      dynamic_cadence_recovery: true,
    };

    config = applyChanges(config, baseFpsCapChanges(40));
    config = applyChanges(config, adaptiveModeChanges(true));
    expect(config).toMatchObject({
      adaptive: true,
      adaptive_auto_base_fps_cap: false,
      base_fps_cap: 40,
      dynamic_cadence_recovery: false,
    });
    expect(isFractionalAdaptivePresetEnabled(config)).toBe(true);

    config = applyChanges(config, steadyBaseCapChanges(true));
    expect(isFractionalAdaptivePresetEnabled(config)).toBe(false);
    expect(config.dynamic_cadence_recovery).toBe(false);
  });

  test("encodes every cap and Recovery interaction as an atomic patch", () => {
    expect(dynamicCadenceRecoveryChanges(true)).toEqual({
      dynamic_cadence_recovery: true,
      adaptive_auto_base_fps_cap: false,
      adaptive_fractional_real_frame_priority:
        ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_AUTO,
      base_fps_cap: 0,
    });
    expect(dynamicCadenceRecoveryChanges(false)).toEqual({
      dynamic_cadence_recovery: false,
    });
    expect(baseFpsCapChanges(30)).toEqual({
      base_fps_cap: 30,
      adaptive_fractional_real_frame_priority:
        ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_AUTO,
      dynamic_cadence_recovery: false,
    });
    expect(baseFpsCapChanges(0)).toEqual({
      base_fps_cap: 0,
      adaptive_fractional_real_frame_priority:
        ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_AUTO,
      dynamic_cadence_recovery: false,
    });
    expect(steadyBaseCapChanges(true)).toEqual({
      adaptive_auto_base_fps_cap: true,
      dynamic_cadence_recovery: false,
    });
    expect(steadyBaseCapChanges(false)).toEqual({
      adaptive_auto_base_fps_cap: false,
      dynamic_cadence_recovery: false,
    });
  });

  test("maps explicit priorities to stable real/generated ratios", () => {
    expect(
      fractionalRealFramePriorityCap(
        120,
        ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_AUTO,
      ),
    ).toBeUndefined();
    expect(
      fractionalRealFramePriorityCap(
        120,
        ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_LOW,
      ),
    ).toBe(72);
    expect(
      fractionalRealFramePriorityCap(
        120,
        ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_MEDIUM,
      ),
    ).toBe(80);
    expect(
      fractionalRealFramePriorityCap(
        120,
        ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_HIGH,
      ),
    ).toBe(90);
    expect(
      fractionalRealFramePriorityCap(
        120,
        ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_VERY_HIGH,
      ),
    ).toBe(96);
    expect(
      fractionalRealFramePriorityChanges(
        ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_HIGH,
      ),
    ).toEqual({
      adaptive_fractional_real_frame_priority:
        ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_HIGH,
      dynamic_cadence_recovery: false,
    });
  });
});
