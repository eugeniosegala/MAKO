import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";
import type { ProfileResult } from "../../src/api/makoApi";

const decky = vi.hoisted(() => ({
  router: {
    MainRunningApp: undefined as
      { appid: number; display_name: string } | undefined,
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

  test("initializes the editor from the saved stream selection while idle", async () => {
    const loadProfileConfig = vi.fn(async () => undefined);
    const syncCurrentProfile = vi.fn().mockResolvedValue({
      success: true,
      game_running: false,
      profile_name: "Streaming",
    });
    const { result } = renderHook(() =>
      useProfileSession({
        isInstalled: true,
        loadProfileConfig,
        syncCurrentProfile,
      }),
    );
    await act(async () => Promise.resolve());
    expect(result.current.editingProfile).toBe("Streaming");
    expect(loadProfileConfig).toHaveBeenLastCalledWith("Streaming");
  });

  test("a delayed initial idle poll cannot undo a manual selection", async () => {
    let finishSync!: (value: ProfileResult) => void;
    const syncCurrentProfile = vi.fn(
      () =>
        new Promise<ProfileResult>((resolve) => {
          finishSync = resolve;
        }),
    );
    const loadProfileConfig = vi.fn(async () => undefined);
    const { result } = renderHook(() =>
      useProfileSession({
        isInstalled: true,
        syncCurrentProfile,
        loadProfileConfig,
      }),
    );
    act(() => result.current.selectEditingProfile("new-choice"));
    loadProfileConfig.mockClear();
    await act(async () => {
      finishSync({
        success: true,
        message: "",
        error: null,
        game_running: false,
        profile_name: "old-choice",
      });
    });
    expect(result.current.editingProfile).toBe("new-choice");
    expect(loadProfileConfig).not.toHaveBeenCalled();
  });

  test("reloads a live game's settings after AC/battery changes", async () => {
    decky.router.MainRunningApp = { appid: 42, display_name: "Game" };
    const loadProfileConfig = vi.fn(async () => undefined);
    const syncCurrentProfile = vi.fn().mockResolvedValue({
      success: true,
      game_running: true,
      profile_name: "game",
      power_source: "handheld",
    });
    renderHook(() =>
      useProfileSession({
        isInstalled: true,
        loadProfileConfig,
        syncCurrentProfile,
      }),
    );
    await act(async () => {
      await Promise.resolve();
    });
    loadProfileConfig.mockClear();
    syncCurrentProfile.mockResolvedValue({
      success: true,
      game_running: true,
      profile_name: "game",
      power_source: "",
    });
    await act(async () => {
      await vi.advanceTimersByTimeAsync(2000);
    });
    expect(loadProfileConfig).not.toHaveBeenCalled();
    syncCurrentProfile.mockResolvedValue({
      success: true,
      game_running: true,
      profile_name: "game",
      power_source: "docked",
    });
    await act(async () => {
      await vi.advanceTimersByTimeAsync(2000);
    });
    expect(loadProfileConfig).toHaveBeenCalledOnce();
    expect(loadProfileConfig).toHaveBeenCalledWith("game");
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

  test("a delayed stream poll cannot undo a newly created profile selection", async () => {
    const loadProfileConfig = vi.fn(async () => undefined);
    const stream = {
      success: true,
      message: "",
      error: null,
      game_running: true,
      remote_play_running: true,
      profile_name: "old",
      power_source: "handheld",
    };
    const syncCurrentProfile = vi.fn().mockResolvedValue(stream);
    const { result } = renderHook(() =>
      useProfileSession({
        isInstalled: true,
        loadProfileConfig,
        syncCurrentProfile,
      }),
    );
    await act(async () => {});
    let finish!: (value: ProfileResult) => void;
    syncCurrentProfile.mockReturnValueOnce(
      new Promise<ProfileResult>((resolve) => {
        finish = resolve;
      }),
    );
    await act(async () => {
      await vi.advanceTimersByTimeAsync(2000);
    });
    act(() => result.current.selectEditingProfile("Future streams"));
    loadProfileConfig.mockClear();
    await act(async () => {
      finish(stream);
    });
    expect(result.current.editingProfile).toBe("Future streams");
    expect(loadProfileConfig).not.toHaveBeenCalled();
    syncCurrentProfile.mockResolvedValue({
      ...stream,
      profile_name: "Future streams",
      power_source: "docked",
    });
    await act(async () => {
      await vi.advanceTimersByTimeAsync(2000);
    });
    expect(result.current.editingProfile).toBe("Future streams");
    expect(loadProfileConfig).toHaveBeenCalledWith("Future streams");
  });
});

test("native streams without an AppID follow the selected profile and refresh its power set", async () => {
  vi.useFakeTimers();
  decky.router.MainRunningApp = undefined;
  const loadProfileConfig = vi.fn(async () => undefined);
  const syncCurrentProfile = vi.fn().mockResolvedValue({
    success: true,
    game_running: true,
    remote_play_running: true,
    profile_name: "stream-quality",
    power_source: "handheld",
  });
  const { result, unmount } = renderHook(() =>
    useProfileSession({
      isInstalled: true,
      loadProfileConfig,
      syncCurrentProfile,
    }),
  );
  await act(async () => Promise.resolve());
  expect(result.current.editingProfile).toBe("stream-quality");
  expect(result.current.remotePlayRunning).toBe(true);
  expect(result.current.mainRunningApp).toBeUndefined();
  loadProfileConfig.mockClear();
  syncCurrentProfile.mockResolvedValue({
    success: true,
    game_running: true,
    remote_play_running: true,
    profile_name: "stream-quality",
    power_source: "docked",
  });
  await act(async () => vi.advanceTimersByTimeAsync(2000));
  expect(loadProfileConfig).toHaveBeenCalledWith("stream-quality");
  syncCurrentProfile.mockResolvedValue({
    success: true,
    game_running: false,
    remote_play_running: false,
    profile_name: "stream-quality",
    power_source: "docked",
  });
  await act(async () => vi.advanceTimersByTimeAsync(2000));
  expect(result.current.remotePlayRunning).toBe(false);
  expect(result.current.editingProfile).toBe("stream-quality");
  unmount();
  vi.useRealTimers();
});
