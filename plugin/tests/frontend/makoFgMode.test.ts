import { describe, expect, test } from "vitest";

import { getDefaults } from "../../src/config/configSchema";
import {
  effectiveMakoFgMode,
  makoFgModeChoices,
  makoFgModeLabel,
} from "../../src/config/makoFgMode";

describe("MAKO FG Mode", () => {
  test("defaults to Balanced and offers every mode", () => {
    expect(effectiveMakoFgMode(getDefaults())).toBe("balanced");
    expect(makoFgModeChoices(getDefaults())).toEqual([
      "quality",
      "balanced",
      "performance",
      "extra-performance",
    ]);
  });

  test("Ultra Performance uses at least Performance", () => {
    const ultra = { ...getDefaults(), ultra_performance: true };
    expect(makoFgModeChoices(ultra)).toEqual([
      "performance",
      "extra-performance",
    ]);
    expect(effectiveMakoFgMode({ ...ultra, mako_fg_mode: "quality" })).toBe(
      "performance",
    );
    expect(
      effectiveMakoFgMode({ ...ultra, mako_fg_mode: "extra-performance" }),
    ).toBe("extra-performance");
  });

  test("an unknown saved value shows Balanced", () => {
    expect(
      effectiveMakoFgMode({ ...getDefaults(), mako_fg_mode: "fast" }),
    ).toBe("balanced");
  });

  test("labels name each mode", () => {
    expect(makoFgModeLabel("extra-performance")).toBe("Extra Performance");
  });
});
