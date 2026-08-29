import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";

const mocks = vi.hoisted(() => ({
  getRuntimeStatus: vi.fn(),
}));

vi.mock("../../src/api/makoApi", () => ({
  checkMakoInstalled: vi.fn(),
  checkLosslessScalingDll: vi.fn(),
  getMakoConfig: vi.fn(),
  getProfileConfig: vi.fn(),
  getRuntimeStatus: mocks.getRuntimeStatus,
  updateMakoConfigFromObject: vi.fn(),
  configFailureResult: (error: string) => ({ success: false, error }),
}));
vi.mock("../../src/utils/toastUtils", () => ({
  showErrorToast: vi.fn(),
  ToastMessages: {
    CONFIG_UPDATE_ERROR: { title: "Update Failed", body: "Update failed" },
  },
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));

import { useRuntimeStatus } from "../../src/hooks/useMakoHooks";

describe("Renderer runtime status polling", () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.clearAllMocks();
    mocks.getRuntimeStatus.mockResolvedValue({
      success: true,
      phase: "active",
      contexts: [],
      message: "active",
      error: null,
    });
  });

  afterEach(() => {
    cleanup();
    vi.useRealTimers();
  });

  test("polls the selected profile without overlapping requests", async () => {
    let finishFirst!: (value: object) => void;
    mocks.getRuntimeStatus.mockReturnValueOnce(
      new Promise((resolve) => {
        finishFirst = resolve;
      }),
    );
    const { result } = renderHook(() =>
      useRuntimeStatus(true, "game-profile"),
    );

    expect(mocks.getRuntimeStatus).toHaveBeenCalledWith("game-profile");
    await act(async () => {
      await vi.advanceTimersByTimeAsync(1500);
    });
    expect(mocks.getRuntimeStatus).toHaveBeenCalledOnce();

    await act(async () => {
      finishFirst({
        success: true,
        phase: "draining",
        contexts: [],
        message: "applying",
        error: null,
      });
      await Promise.resolve();
    });
    expect(result.current?.phase).toBe("draining");

    await act(async () => {
      await vi.advanceTimersByTimeAsync(750);
    });
    expect(mocks.getRuntimeStatus).toHaveBeenCalledTimes(2);
  });

  test("clears status and stops polling when no game is active", async () => {
    const { result, rerender } = renderHook(
      ({ enabled }) => useRuntimeStatus(enabled, "game-profile"),
      { initialProps: { enabled: true } },
    );
    await act(async () => Promise.resolve());
    expect(result.current?.phase).toBe("active");

    rerender({ enabled: false });
    expect(result.current).toBeNull();
    mocks.getRuntimeStatus.mockClear();
    await act(async () => {
      await vi.advanceTimersByTimeAsync(1500);
    });
    expect(mocks.getRuntimeStatus).not.toHaveBeenCalled();
  });
});
