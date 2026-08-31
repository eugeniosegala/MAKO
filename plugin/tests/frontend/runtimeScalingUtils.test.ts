import { describe, expect, test } from "vitest";
import type {
  RuntimeContextState,
  RuntimeStatusResult,
} from "../../src/api/makoApi";
import { scalingInactiveReason } from "../../src/utils/runtimeScalingUtils";

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
});
