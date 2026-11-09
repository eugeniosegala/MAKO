import { describe, expect, test } from "vitest";
import type { RuntimeContextState, RuntimeStatusResult } from "../../src/api/makoApi";
import {
  frameGenerationUnavailableMessage,
  runtimeScalingUiState,
} from "../../src/utils/runtimeScalingUtils";

const context = (profile: string, offscreen: boolean): RuntimeContextState =>
  ({
    role: "frame-generation",
    frame_generation_active: false,
    frame_generation_unavailable: offscreen
      ? { reason: "offscreen-x11-window" }
      : null,
    pending: {
      frame_generation_private: false,
      spatial_private: false,
      swapchain_recreation: false,
      process_restart: false,
    },
    requested: { name: profile, frame_generation_enabled: true },
    applied: { name: profile, frame_generation_enabled: true },
    spatial_scaling: { active: false },
  }) as unknown as RuntimeContextState;

const status = (contexts: RuntimeContextState[]): RuntimeStatusResult => ({
  success: true,
  phase: "active",
  contexts,
  message: "",
  error: null,
});

describe("offscreen Frame Generation status", () => {
  test("the selected profile reports unavailable presentation without a restart request", () => {
    const result = runtimeScalingUiState(
      status([context("Other", false), context("Game", true)]), "Game",
    );
    expect(result.frameGenerationUnavailableReason).toBe("offscreen-x11-window");
    expect(result.frameGenerationPending).toBe(false);
    expect(result.frameGenerationActive).toBe(false);
    const message = frameGenerationUnavailableMessage(result.frameGenerationUnavailableReason);
    expect(message).toContain("generated frames cannot reach the screen");
    expect(message).not.toContain("restart");
  });

  test("an unrelated offscreen game does not affect direct presentation", () => {
    const result = runtimeScalingUiState(
      status([context("Other", true), context("Game", false)]), "Game",
    );
    expect(result.frameGenerationUnavailableReason).toBeNull();
    expect(frameGenerationUnavailableMessage(result.frameGenerationUnavailableReason)).toBeNull();
    expect(frameGenerationUnavailableMessage("unknown-reason")).toBeNull();
  });
});
