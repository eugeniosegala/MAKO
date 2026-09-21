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
  role: "frame-generation",
  frame_generation_active: true,
  pending: {
    frame_generation_private: false,
    spatial_private: false,
    swapchain_recreation: false,
    process_restart: false,
  },
  requested: {
    name: "game",
    scaling_enabled: true,
    scaling_method: "ls1",
    scaling_factor: 2,
  },
  applied: {
    name: "game",
    frame_generation_enabled: true,
    adaptive: true,
    target_fps: 120,
    adaptive_max_multiplier: 3,
    multiplier: 2,
    scaling_enabled: true,
  },
  spatial_scaling: {
    active: false,
    activation_supported: false,
    inactive_reason: "gamescope-wsi-surface-unproven",
    constraint_reason: null,
    source_width: 960,
    source_height: 540,
    presentation_width: 1280,
    presentation_height: 720,
    gamescope_target_width: 1280,
    gamescope_target_height: 800,
    requested_method: "ls1",
    active_method: "native",
    effective_factor: 4 / 3,
    pipeline: "inactive",
    supersampling_active: false,
    fallback_reason: null,
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
    expect(
      runtimeScalingUiState(status([second, context]), "game"),
    ).toMatchObject({
      hasContext: true,
      frameGenerationActive: true,
      frameGenerationMode: "adaptive",
      frameGenerationAdaptiveStyle: "fractional",
      frameGenerationTargetFps: 120,
      frameGenerationMultiplier: 3,
      scalingActivationSupported: false,
      inactiveReason: "gamescope-wsi-surface-unproven",
      nonSupersamplingFactorCeiling: 4 / 3,
    });
  });

  test("uses the active scaling owner for authoritative extents and method", () => {
    const scalingContext = {
      ...context,
      role: "spatial-scaling",
      context: 3,
      frame_generation_active: false,
      spatial_scaling: {
        ...context.spatial_scaling,
        active: true,
        activation_supported: true,
        inactive_reason: null,
        constraint_reason: "variable-surface-memory-budget",
        presentation_width: 1440,
        presentation_height: 810,
        requested_method: "ls1",
        active_method: "mako",
        effective_factor: 1.5,
        pipeline: "pre-frame-generation",
        supersampling_active: true,
        fallback_reason: "translator unavailable",
      },
    } as RuntimeContextState;

    expect(
      runtimeScalingUiState(status([scalingContext, context]), "game"),
    ).toMatchObject({
      scalingActive: true,
      scalingActivationSupported: true,
      sourceWidth: 960,
      sourceHeight: 540,
      presentationWidth: 1440,
      presentationHeight: 810,
      requestedMethod: "ls1",
      activeMethod: "mako",
      effectiveFactor: 1.5,
      requestedFactor: 2,
      constraintReason: "variable-surface-memory-budget",
      pipeline: "pre-frame-generation",
      supersamplingActive: true,
      fallbackReason: "translator unavailable",
    });
  });

  test("surfaces an inactive memory limit without treating the surface as unsupported", () => {
    const memoryLimitedContext = {
      ...context,
      spatial_scaling: {
        ...context.spatial_scaling,
        activation_supported: true,
        inactive_reason: "variable-surface-memory-budget",
      },
    } as RuntimeContextState;

    expect(
      scalingInactiveReason(status([memoryLimitedContext]), "game"),
    ).toBeNull();
    expect(
      runtimeScalingUiState(status([memoryLimitedContext]), "game"),
    ).toMatchObject({
      scalingActive: false,
      scalingActivationSupported: true,
      inactiveReason: "variable-surface-memory-budget",
    });

    const allocationFreeLowerContext = {
      ...context,
      role: "spatial-scaling",
      context: 4,
      frame_generation_active: false,
      requested: {
        ...context.requested,
        scaling_enabled: false,
        scaling_method: "native",
      },
      applied: {
        ...context.applied,
        scaling_enabled: false,
      },
      spatial_scaling: {
        ...context.spatial_scaling,
        active: false,
        activation_supported: true,
        inactive_reason: null,
      },
    } as RuntimeContextState;
    expect(
      runtimeScalingUiState(
        status([allocationFreeLowerContext, memoryLimitedContext]),
        "game",
      ),
    ).toMatchObject({
      scalingActive: false,
      scalingEnabled: true,
      scalingActivationSupported: true,
      requestedFactor: 2,
      inactiveReason: "variable-surface-memory-budget",
    });
  });

  test("reports Steady Adaptive from the applied automatic base cap", () => {
    expect(
      runtimeScalingUiState(
        status([
          {
            ...context,
            applied: {
              ...context.applied,
              adaptive_auto_base_fps_cap: true,
            },
          },
        ]),
        "game",
      ),
    ).toMatchObject({ frameGenerationAdaptiveStyle: "steady" });
  });
});
