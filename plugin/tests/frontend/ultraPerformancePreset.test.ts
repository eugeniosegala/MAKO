import { describe, expect, test } from "vitest";

import { ultraPerformanceChanges } from "../../src/config/ultraPerformancePreset";

describe("Ultra Performance preset", () => {
  test("applies its forced restart-only values atomically", () => {
    expect(ultraPerformanceChanges(true)).toEqual({
      ultra_performance: true,
      flow_scale: 0.8,
      performance_mode: true,
      allow_fp16: true,
    });
  });

  test("restores Decky's canonical defaults when disabled", () => {
    expect(ultraPerformanceChanges(false)).toEqual({
      ultra_performance: false,
      flow_scale: 0.9,
      performance_mode: false,
      allow_fp16: true,
    });
  });
});
