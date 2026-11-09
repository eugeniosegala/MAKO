import { describe, expect, test } from "vitest";

import { getDefaults } from "../../src/config/configSchema";
import {
  frameGenerationModelChoice,
  frameGenerationModelChoiceChanges,
  frameGenerationModelChoices,
  frameGenerationModelShortLabel,
} from "../../src/config/frameGenerationModelChoice";

const lsfg = { ...getDefaults(), frame_generation_model: "lossless-scaling" };

describe("Frame Generation model picker", () => {
  test("derives one choice from the model and the effective lighter LSFG variant", () => {
    expect(frameGenerationModelChoice(getDefaults())).toBe("mako-fg");
    expect(
      frameGenerationModelChoice({ ...getDefaults(), performance_mode: true }),
    ).toBe("mako-fg");
    expect(frameGenerationModelChoice(lsfg)).toBe("lsfg-quality");
    expect(frameGenerationModelChoice({ ...lsfg, performance_mode: true })).toBe(
      "lsfg-performance",
    );
    expect(frameGenerationModelChoice({ ...lsfg, ultra_performance: true })).toBe(
      "lsfg-performance",
    );
  });

  test("hides LSFG Quality while Ultra Performance forces the lighter model", () => {
    expect(frameGenerationModelChoices(lsfg)).toEqual([
      "mako-fg",
      "lsfg-quality",
      "lsfg-performance",
    ]);
    expect(frameGenerationModelChoices({ ...lsfg, ultra_performance: true })).toEqual([
      "mako-fg",
      "lsfg-performance",
    ]);
  });

  test("shortens LSFG names for the live status", () => {
    expect(frameGenerationModelShortLabel("mako-fg")).toBe("MAKO FG");
    expect(frameGenerationModelShortLabel("lsfg-quality")).toBe("LSFG Q");
    expect(frameGenerationModelShortLabel("lsfg-performance")).toBe("LSFG P");
  });

  test("writes LSFG variants atomically and keeps them saved behind MAKO FG", () => {
    expect(frameGenerationModelChoiceChanges("mako-fg")).toEqual({
      frame_generation_model: "mako-fg",
    });
    expect(frameGenerationModelChoiceChanges("lsfg-quality")).toEqual({
      frame_generation_model: "lossless-scaling",
      performance_mode: false,
    });
    expect(frameGenerationModelChoiceChanges("lsfg-performance")).toEqual({
      frame_generation_model: "lossless-scaling",
      performance_mode: true,
    });
  });
});
