import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";

const decky = vi.hoisted(() => ({
  router: {
    MainRunningApp: undefined as
      | { appid: number; display_name: string }
      | undefined,
  },
}));

vi.mock("@decky/ui", () => ({
  Router: decky.router,
}));

import { useProfileSession } from "../../src/hooks/useProfileSession";

describe("profile runtime session", () => {
  beforeEach(() => {
    vi.useFakeTimers();
    decky.router.MainRunningApp = undefined;
  });

  afterEach(() => {
    cleanup();
    vi.useRealTimers();
  });

  test("follows a running game, resets once on exit, then preserves offline editing", async () => {
    const loadProfileConfig = vi.fn(async () => undefined);
    const syncCurrentProfile = vi.fn().mockResolvedValue({
      success: true,
      game_running: false,
    });
    const { result } = renderHook(() =>
      useProfileSession({
        isInstalled: true,
        loadProfileConfig,
        syncCurrentProfile,
      }),
    );

    await act(async () => Promise.resolve());
    expect(loadProfileConfig).toHaveBeenCalledWith("mako");
    expect(syncCurrentProfile).toHaveBeenCalledWith(undefined);
    expect(loadProfileConfig.mock.invocationCallOrder[0]).toBeLessThan(
      syncCurrentProfile.mock.invocationCallOrder[0],
    );

    act(() => result.current.selectEditingProfile("offline-profile"));
    loadProfileConfig.mockClear();
    decky.router.MainRunningApp = { appid: 123, display_name: "Test Game" };
    syncCurrentProfile.mockResolvedValueOnce({
      success: true,
      game_running: true,
      profile_name: "game-123",
    });

    await act(async () => {
      await vi.advanceTimersByTimeAsync(2000);
    });
    expect(syncCurrentProfile).toHaveBeenLastCalledWith("123");
    expect(result.current.mainRunningApp?.display_name).toBe("Test Game");
    expect(result.current.editingProfile).toBe("game-123");
    expect(loadProfileConfig).toHaveBeenLastCalledWith("game-123");

    decky.router.MainRunningApp = undefined;
    syncCurrentProfile.mockResolvedValueOnce({
      success: true,
      game_running: false,
    });
    await act(async () => {
      await vi.advanceTimersByTimeAsync(2000);
    });
    expect(result.current.mainRunningApp).toBeUndefined();
    expect(result.current.editingProfile).toBe("mako");
    expect(loadProfileConfig).toHaveBeenLastCalledWith("mako");

    act(() => result.current.selectEditingProfile("offline-profile"));
    await act(async () => {
      await vi.advanceTimersByTimeAsync(2000);
    });
    expect(result.current.editingProfile).toBe("offline-profile");
  });

  test("prevents overlapping profile synchronisation polls", async () => {
    let finishFirstSync!: (value: {
      success: boolean;
      game_running: boolean;
    }) => void;
    const firstSync = new Promise<{
      success: boolean;
      game_running: boolean;
    }>((resolve) => {
      finishFirstSync = resolve;
    });
    const loadProfileConfig = vi.fn(async () => undefined);
    const syncCurrentProfile = vi
      .fn()
      .mockReturnValueOnce(firstSync)
      .mockResolvedValue({ success: true, game_running: false });

    renderHook(() =>
      useProfileSession({
        isInstalled: false,
        loadProfileConfig,
        syncCurrentProfile,
      }),
    );
    expect(syncCurrentProfile).toHaveBeenCalledOnce();

    await act(async () => {
      await vi.advanceTimersByTimeAsync(6000);
    });
    expect(syncCurrentProfile).toHaveBeenCalledOnce();

    await act(async () => {
      finishFirstSync({ success: true, game_running: false });
      await firstSync;
    });
    await act(async () => {
      await vi.advanceTimersByTimeAsync(2000);
    });
    expect(syncCurrentProfile).toHaveBeenCalledTimes(2);
  });

  test("loads the currently selected editor profile when installation appears", async () => {
    const loadProfileConfig = vi.fn(async () => undefined);
    const syncCurrentProfile = vi.fn().mockResolvedValue({
      success: true,
      game_running: false,
    });
    const { result, rerender } = renderHook(
      ({ isInstalled }) =>
        useProfileSession({
          isInstalled,
          loadProfileConfig,
          syncCurrentProfile,
        }),
      { initialProps: { isInstalled: false } },
    );
    await act(async () => Promise.resolve());
    loadProfileConfig.mockClear();

    act(() => result.current.selectEditingProfile("offline-profile"));
    rerender({ isInstalled: true });

    expect(loadProfileConfig).toHaveBeenCalledOnce();
    expect(loadProfileConfig).toHaveBeenCalledWith("offline-profile");
  });
});
