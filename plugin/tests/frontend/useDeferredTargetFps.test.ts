import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";
import { TARGET_FPS } from "../../src/config/generatedConfigSchema";
import { useDeferredTargetFps } from "../../src/hooks/useDeferredTargetFps";

describe("deferred target FPS persistence", () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });

  afterEach(() => {
    cleanup();
    vi.useRealTimers();
  });

  test("coalesces rapid slider changes into one latest-value write", async () => {
    const saveConfigurationField = vi.fn(async () => undefined);
    const { result } = renderHook(() =>
      useDeferredTargetFps(60, saveConfigurationField),
    );

    act(() => {
      result.current.changeTargetFps(75);
      result.current.changeTargetFps(89);
      result.current.changeTargetFps(90);
    });

    expect(result.current.targetFps).toBe(90);
    expect(saveConfigurationField).not.toHaveBeenCalled();

    await act(async () => {
      await vi.advanceTimersByTimeAsync(249);
    });
    expect(saveConfigurationField).not.toHaveBeenCalled();

    await act(async () => {
      await vi.advanceTimersByTimeAsync(1);
    });
    expect(saveConfigurationField).toHaveBeenCalledOnce();
    expect(saveConfigurationField).toHaveBeenCalledWith(TARGET_FPS, 90);
  });

  test("flushes the pending value with its original writer on unmount", () => {
    const firstWriter = vi.fn(async () => undefined);
    const replacementWriter = vi.fn(async () => undefined);
    const { result, rerender, unmount } = renderHook(
      ({ writer }) => useDeferredTargetFps(60, writer),
      { initialProps: { writer: firstWriter } },
    );

    act(() => result.current.changeTargetFps(90));
    rerender({ writer: replacementWriter });
    unmount();

    expect(firstWriter).toHaveBeenCalledOnce();
    expect(firstWriter).toHaveBeenCalledWith(TARGET_FPS, 90);
    expect(replacementWriter).not.toHaveBeenCalled();

    vi.advanceTimersByTime(250);
    expect(firstWriter).toHaveBeenCalledOnce();
  });

  test("accepts backend updates only while no slider write is pending", async () => {
    const saveConfigurationField = vi.fn(async () => undefined);
    const { result, rerender } = renderHook(
      ({ configuredTargetFps }) =>
        useDeferredTargetFps(configuredTargetFps, saveConfigurationField),
      { initialProps: { configuredTargetFps: 60 } },
    );

    rerender({ configuredTargetFps: 72 });
    expect(result.current.targetFps).toBe(72);

    act(() => result.current.changeTargetFps(90));
    rerender({ configuredTargetFps: 75 });
    expect(result.current.targetFps).toBe(90);

    await act(async () => {
      await vi.advanceTimersByTimeAsync(250);
    });
    rerender({ configuredTargetFps: 76 });
    expect(result.current.targetFps).toBe(76);
  });
});
