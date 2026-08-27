import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";
import { getDefaults } from "../../src/config/configSchema";
import {
  PROFILE_CONFIG_SAVE_DELAY_MS,
  useProfileConfigWriter,
} from "../../src/hooks/useProfileConfigWriter";

describe("profile configuration writer", () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });

  afterEach(() => {
    cleanup();
    vi.useRealTimers();
  });

  test("coalesces a control burst into one latest-value profile patch", async () => {
    const canonicalConfig = {
      ...getDefaults(),
      scaling_enabled: true,
      scaling_sharpness: 0.73,
    };
    const updateProfileConfigFields = vi.fn().mockResolvedValue({
      success: true,
      config: canonicalConfig,
      message: "updated",
      error: null,
    });
    const applyConfigPatch = vi.fn();
    const replaceConfig = vi.fn();
    const loadProfileConfig = vi.fn(async () => undefined);
    const { result } = renderHook(() =>
      useProfileConfigWriter({
        editingProfile: "game",
        getEditingProfile: () => "game",
        updateProfileConfigFields,
        loadProfileConfig,
        applyConfigPatch,
        replaceConfig,
      }),
    );

    act(() => {
      void result.current.saveConfigField("scaling_enabled", true);
      for (let step = 51; step <= 73; step += 1) {
        void result.current.saveConfigField("scaling_sharpness", step / 100);
      }
    });

    expect(applyConfigPatch).toHaveBeenCalledTimes(24);
    expect(updateProfileConfigFields).not.toHaveBeenCalled();

    await act(async () => {
      await vi.advanceTimersByTimeAsync(PROFILE_CONFIG_SAVE_DELAY_MS - 1);
    });
    expect(updateProfileConfigFields).not.toHaveBeenCalled();

    await act(async () => {
      await vi.advanceTimersByTimeAsync(1);
    });

    expect(updateProfileConfigFields).toHaveBeenCalledOnce();
    expect(updateProfileConfigFields).toHaveBeenCalledWith("game", {
      scaling_enabled: true,
      scaling_sharpness: 0.73,
    });
    expect(replaceConfig).toHaveBeenCalledWith(canonicalConfig);
    expect(loadProfileConfig).not.toHaveBeenCalled();
  });

  test("allows only one backend write in flight", async () => {
    let resolveFirstWrite: ((value: unknown) => void) | undefined;
    const firstWrite = new Promise((resolve) => {
      resolveFirstWrite = resolve;
    });
    const updateProfileConfigFields = vi
      .fn()
      .mockReturnValueOnce(firstWrite)
      .mockResolvedValue({
        success: true,
        config: { ...getDefaults(), flow_scale: 0.8 },
        message: "updated",
        error: null,
      });
    const { result } = renderHook(() =>
      useProfileConfigWriter({
        editingProfile: "game",
        getEditingProfile: () => "game",
        updateProfileConfigFields,
        loadProfileConfig: vi.fn(async () => undefined),
        applyConfigPatch: vi.fn(),
        replaceConfig: vi.fn(),
      }),
    );

    act(() => void result.current.saveConfigField("flow_scale", 0.7));
    await act(async () => {
      await vi.advanceTimersByTimeAsync(PROFILE_CONFIG_SAVE_DELAY_MS);
    });
    expect(updateProfileConfigFields).toHaveBeenCalledOnce();

    act(() => {
      for (let step = 71; step <= 80; step += 1) {
        void result.current.saveConfigField("flow_scale", step / 100);
      }
    });
    await act(async () => {
      await vi.advanceTimersByTimeAsync(PROFILE_CONFIG_SAVE_DELAY_MS);
    });
    expect(updateProfileConfigFields).toHaveBeenCalledOnce();

    await act(async () => {
      resolveFirstWrite?.({
        success: true,
        config: { ...getDefaults(), flow_scale: 0.7 },
        message: "updated",
        error: null,
      });
      await Promise.resolve();
    });
    await act(async () => {
      await vi.advanceTimersByTimeAsync(PROFILE_CONFIG_SAVE_DELAY_MS);
    });

    expect(updateProfileConfigFields).toHaveBeenCalledTimes(2);
    expect(updateProfileConfigFields).toHaveBeenLastCalledWith("game", {
      flow_scale: 0.8,
    });
  });

  test("keeps a queued writer bound to its original profile", async () => {
    let currentProfile = "first-game";
    const getEditingProfile = () => currentProfile;
    const updateProfileConfigFields = vi.fn().mockResolvedValue({
      success: true,
      config: { ...getDefaults(), target_fps: 90 },
      message: "updated",
      error: null,
    });
    const replaceConfig = vi.fn();
    const { result, rerender } = renderHook(
      ({ editingProfile }) =>
        useProfileConfigWriter({
          editingProfile,
          getEditingProfile,
          updateProfileConfigFields,
          loadProfileConfig: vi.fn(async () => undefined),
          applyConfigPatch: vi.fn(),
          replaceConfig,
        }),
      { initialProps: { editingProfile: currentProfile } },
    );
    const queuedWriter = result.current.saveConfigField;

    act(() => void queuedWriter("target_fps", 90));
    currentProfile = "second-game";
    rerender({ editingProfile: currentProfile });
    await act(async () => {
      await vi.advanceTimersByTimeAsync(PROFILE_CONFIG_SAVE_DELAY_MS);
    });

    expect(updateProfileConfigFields).toHaveBeenCalledWith("first-game", {
      target_fps: 90,
    });
    expect(replaceConfig).not.toHaveBeenCalled();
  });

  test("flushes the final queued patch when Decky unmounts the panel", async () => {
    const updateProfileConfigFields = vi.fn().mockResolvedValue({
      success: true,
      config: { ...getDefaults(), scaling_sharpness: 0.9 },
      message: "updated",
      error: null,
    });
    const { result, unmount } = renderHook(() =>
      useProfileConfigWriter({
        editingProfile: "game",
        getEditingProfile: () => "game",
        updateProfileConfigFields,
        loadProfileConfig: vi.fn(async () => undefined),
        applyConfigPatch: vi.fn(),
        replaceConfig: vi.fn(),
      }),
    );

    act(() => void result.current.saveConfigField("scaling_sharpness", 0.9));
    unmount();
    await act(async () => Promise.resolve());

    expect(updateProfileConfigFields).toHaveBeenCalledOnce();
    expect(updateProfileConfigFields).toHaveBeenCalledWith("game", {
      scaling_sharpness: 0.9,
    });
    vi.advanceTimersByTime(PROFILE_CONFIG_SAVE_DELAY_MS);
    expect(updateProfileConfigFields).toHaveBeenCalledOnce();
  });

  test("uses the update response and reads the profile only for recovery", async () => {
    const loadProfileConfig = vi.fn(async () => undefined);
    const replaceConfig = vi.fn();
    const updateProfileConfigFields = vi
      .fn()
      .mockResolvedValueOnce({
        success: true,
        config: { ...getDefaults(), performance_mode: true },
        message: "updated",
        error: null,
      })
      .mockResolvedValueOnce({
        success: false,
        config: null,
        message: "failed",
        error: "simulated failure",
      });
    const { result } = renderHook(() =>
      useProfileConfigWriter({
        editingProfile: "mako",
        getEditingProfile: () => "mako",
        updateProfileConfigFields,
        loadProfileConfig,
        applyConfigPatch: vi.fn(),
        replaceConfig,
      }),
    );

    act(() => void result.current.saveConfigField("performance_mode", true));
    await act(async () => {
      await vi.advanceTimersByTimeAsync(PROFILE_CONFIG_SAVE_DELAY_MS);
    });
    expect(replaceConfig).toHaveBeenCalledOnce();
    expect(loadProfileConfig).not.toHaveBeenCalled();

    act(() => void result.current.saveConfigField("performance_mode", false));
    await act(async () => {
      await vi.advanceTimersByTimeAsync(PROFILE_CONFIG_SAVE_DELAY_MS);
    });
    expect(loadProfileConfig).toHaveBeenCalledOnce();
    expect(loadProfileConfig).toHaveBeenCalledWith("mako");
  });
});
