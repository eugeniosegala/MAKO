import { beforeEach, describe, expect, test, vi } from "vitest";

const { toast } = vi.hoisted(() => ({ toast: vi.fn() }));
vi.mock("@decky/api", () => ({ callable: vi.fn(), toaster: { toast } }));

import type {
  RuntimeContextState,
  RuntimeStatusResult,
  RuntimeWineCompatibilityFix,
} from "../../src/api/makoApi";
import { notifyWineCompatibilityRestart } from "../../src/hooks/useMakoHooks";
import {
  runtimeScalingUiState,
  wineCompatibilityFixMessage,
} from "../../src/utils/runtimeScalingUtils";

const offscreenContext = (
  pid: number,
  fix: RuntimeWineCompatibilityFix | null,
): RuntimeContextState =>
  ({
    pid,
    process_start_ticks: pid * 10,
    context: 1,
    role: "frame-generation",
    frame_generation_active: false,
    frame_generation_unavailable: fix
      ? { reason: "offscreen-x11-window", wine_compatibility_fix: fix }
      : null,
    pending: {
      frame_generation_private: false,
      spatial_private: false,
      swapchain_recreation: false,
      process_restart: false,
    },
    requested: { name: "Half-Life-2", frame_generation_enabled: true },
    applied: { name: "Half-Life-2", frame_generation_enabled: true },
    spatial_scaling: { active: false },
  }) as unknown as RuntimeContextState;

const status = (contexts: RuntimeContextState[]): RuntimeStatusResult => ({
  success: true,
  phase: "active",
  contexts,
  message: "",
  error: null,
});

describe("Wine compatibility fix status", () => {
  beforeEach(() => {
    toast.mockClear();
  });

  test("runtime state carries the Wine fix state of the generating context", () => {
    expect(
      runtimeScalingUiState(status([offscreenContext(1, "restart-required")]), "Half-Life-2")
        .wineCompatibilityFix,
    ).toBe("restart-required");
    expect(
      runtimeScalingUiState(status([offscreenContext(1, null)]), "Half-Life-2")
        .wineCompatibilityFix,
    ).toBeNull();
  });

  test("every state explains why Frame Generation is off", () => {
    expect(wineCompatibilityFixMessage(null)).toBeNull();
    const messages = (
      [
        "restart-required",
        "ineffective",
        "disabled",
        "not-eligible",
        "unneeded",
        "manual",
      ] as RuntimeWineCompatibilityFix[]
    ).map(wineCompatibilityFixMessage);
    expect(messages.every((message) => message && message.length > 0)).toBe(true);
    expect(messages[0]).toContain("restart");
    expect(messages[1]).toContain("Proton 10");
    expect(messages[2]).toContain("Proton Offscreen Window Fix");
    expect(messages[5]).toContain("WINE_DISABLE_VK_CHILD_WINDOW_RENDERING_HACK=1");
  });

  test("a restart notice appears once per game process", () => {
    notifyWineCompatibilityRestart(status([offscreenContext(7, "restart-required")]));
    notifyWineCompatibilityRestart(status([offscreenContext(7, "restart-required")]));
    expect(toast).toHaveBeenCalledTimes(1);
    expect(toast.mock.calls[0][0].body).toContain("Half-Life-2");

    notifyWineCompatibilityRestart(status([offscreenContext(8, "restart-required")]));
    expect(toast).toHaveBeenCalledTimes(2);
  });

  test("other states and ordinary contexts never show the restart notice", () => {
    notifyWineCompatibilityRestart(
      status([
        offscreenContext(20, "ineffective"),
        offscreenContext(21, "not-eligible"),
        offscreenContext(22, "disabled"),
        offscreenContext(23, null),
        offscreenContext(24, "manual"),
      ]),
    );
    expect(toast).not.toHaveBeenCalled();
  });
});
