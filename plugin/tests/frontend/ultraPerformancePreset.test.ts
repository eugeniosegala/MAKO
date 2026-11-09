import { describe, expect, test } from "vitest";

import { getDefaults } from "../../src/config/configSchema";
import {
  ultraPerformanceChanges,
  ultraPerformanceFlowScale,
} from "../../src/config/ultraPerformancePreset";

const lsfg = { ...getDefaults(), frame_generation_model: "lossless-scaling" };

describe("Ultra Performance preset", () => {
  test("locks each model at its own Flow Scale", () => {
    expect(ultraPerformanceFlowScale(getDefaults())).toBe(0.6);
    expect(ultraPerformanceFlowScale(lsfg)).toBe(0.7);
  });

  test("applies its forced restart-only values atomically", () => {
    expect(ultraPerformanceChanges(true, lsfg)).toEqual({
      ultra_performance: true,
      flow_scale: 0.7,
      performance_mode: true,
    });
    expect(ultraPerformanceChanges(true, getDefaults()).flow_scale).toBe(0.6);
  });

  test("restores Decky's canonical defaults when disabled", () => {
    expect(ultraPerformanceChanges(false, getDefaults())).toEqual({
      ultra_performance: false,
      flow_scale: 0.8,
      performance_mode: false,
    });
  });
});
