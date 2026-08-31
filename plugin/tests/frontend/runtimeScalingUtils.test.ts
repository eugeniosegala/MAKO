import { describe, expect, test } from "vitest";
import type {
  RuntimeContextState,
  RuntimeStatusResult,
} from "../../src/api/makoApi";
import {
  runtimeScalingUiState,
  scalingInactiveReason,
} from "../../src/utils/runtimeScalingUtils";

const context = {
  requested: {
    name: "game",
    scaling_enabled: true,
    scaling_method: "ls1",
  },
  applied: { name: "game" },
  spatial_scaling: {
    active: false,
    activation_supported: false,
    inactive_reason: "gamescope-wsi-surface-unproven",
    source_width: 960,
    source_height: 540,
    gamescope_target_width: 1280,
    gamescope_target_height: 800,
    non_supersampling_factor_ceiling: 4 / 3,
  },
} as RuntimeContextState;

const status = (contexts: RuntimeContextState[]): RuntimeStatusResult => ({
  success: true,
  phase: "active",
  contexts,
  message: "",
  error: null,
});

describe("runtime scaling availability", () => {
  test("exposes an unproven Gamescope WSI surface for the active profile", () => {
    expect(scalingInactiveReason(status([context]), "game")).toBe(
      "gamescope-wsi-surface-unproven",
    );
  });

  test("does not warn for another profile or native resolution", () => {
    expect(scalingInactiveReason(status([context]), "other")).toBeNull();
    expect(
      scalingInactiveReason(
        status([
          {
            ...context,
            requested: { ...context.requested, scaling_method: "native" },
          },
        ]),
        "game",
      ),
    ).toBeNull();
  });

  test("uses the safest renderer-proven live factor ceiling", () => {
    const second = {
      ...context,
      context: 2,
      spatial_scaling: {
        ...context.spatial_scaling,
        non_supersampling_factor_ceiling: 1.5,
      },
    } as RuntimeContextState;
    expect(runtimeScalingUiState(status([second, context]), "game"))
      .toEqual({
        inactiveReason: "gamescope-wsi-surface-unproven",
        nonSupersamplingFactorCeiling: 4 / 3,
      });
  });
});
