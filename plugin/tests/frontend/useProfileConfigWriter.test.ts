import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, describe, expect, test, vi } from "vitest";
import { useProfileConfigWriter } from "../../src/hooks/useProfileConfigWriter";

describe("profile configuration writer", () => {
  afterEach(cleanup);

  test("keeps a deferred writer bound to its original profile", async () => {
    let currentProfile = "first-game";
    const getEditingProfile = () => currentProfile;
    const updateProfileConfigFields = vi.fn().mockResolvedValue({
      success: true,
      config: null,
      message: "updated",
      error: null,
    });
    const loadProfileConfig = vi.fn(async () => undefined);
    const { result, rerender } = renderHook(
      ({ editingProfile }) =>
        useProfileConfigWriter({
          editingProfile,
          getEditingProfile,
          updateProfileConfigFields,
          loadProfileConfig,
        }),
      { initialProps: { editingProfile: currentProfile } },
    );
    const deferredWriter = result.current.saveConfigField;

    currentProfile = "second-game";
    rerender({ editingProfile: currentProfile });
    await act(async () => deferredWriter("target_fps", 90));

    expect(updateProfileConfigFields).toHaveBeenCalledWith("first-game", {
      target_fps: 90,
    });
    expect(loadProfileConfig).not.toHaveBeenCalled();
  });

  test("reloads only when the patched profile is still being edited", async () => {
    const updateProfileConfigFields = vi.fn().mockResolvedValue({
      success: true,
      config: null,
      message: "updated",
      error: null,
    });
    const loadProfileConfig = vi.fn(async () => undefined);
    const { result } = renderHook(() =>
      useProfileConfigWriter({
        editingProfile: "mako",
        getEditingProfile: () => "mako",
        updateProfileConfigFields,
        loadProfileConfig,
      }),
    );

    await act(async () =>
      result.current.saveConfigChanges({
        performance_mode: true,
        flow_scale: 0.75,
      }),
    );

    expect(updateProfileConfigFields).toHaveBeenCalledWith("mako", {
      performance_mode: true,
      flow_scale: 0.75,
    });
    expect(loadProfileConfig).toHaveBeenCalledWith("mako");
  });
});
