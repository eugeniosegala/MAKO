import { act, cleanup, renderHook, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";

const mocks = vi.hoisted(() => ({
  captureGameProfile: vi.fn(),
  createProfile: vi.fn(),
  deleteProfile: vi.fn(),
  getProfiles: vi.fn(),
  renameProfile: vi.fn(),
  setCurrentProfile: vi.fn(),
  showErrorToast: vi.fn(),
  showSuccessToast: vi.fn(),
}));

vi.mock("../../src/api/makoApi", () => ({
  captureGameProfile: mocks.captureGameProfile,
  createProfile: mocks.createProfile,
  deleteProfile: mocks.deleteProfile,
  getProfiles: mocks.getProfiles,
  renameProfile: mocks.renameProfile,
  setCurrentProfile: mocks.setCurrentProfile,
}));
vi.mock("../../src/utils/toastUtils", () => ({
  showErrorToast: mocks.showErrorToast,
  showSuccessToast: mocks.showSuccessToast,
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));

import { useProfileEditorModel } from "../../src/hooks/useProfileEditorModel";

const defaultProfile = {
  profile_name: "mako",
  display_name: "Default",
  kind: "default",
  processes: [],
};
const gameProfile = {
  profile_name: "game-123",
  display_name: "Test Game",
  kind: "game",
  steam_app_id: "123",
  processes: ["test-game.exe"],
};

describe("profile editor model", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    mocks.getProfiles.mockResolvedValue({
      success: true,
      profiles: ["mako", "game-123"],
      current_profile: "mako",
      profile_details: [defaultProfile, gameProfile],
    });
  });

  afterEach(cleanup);

  test("switches only the offline editor and never overrides the runtime profile", async () => {
    const onProfileChange = vi.fn(async () => undefined);
    const { result } = renderHook(() =>
      useProfileEditorModel({ onProfileChange }),
    );
    await waitFor(() => expect(result.current.profileOptions).toHaveLength(2));

    await act(() => result.current.switchProfile("game-123"));

    expect(result.current.selectedProfile).toBe("game-123");
    expect(onProfileChange).toHaveBeenCalledWith("game-123");
    expect(mocks.setCurrentProfile).not.toHaveBeenCalled();
    expect(mocks.captureGameProfile).not.toHaveBeenCalled();
  });

  test("refreshes a newly created process profile without changing the offline selection", async () => {
    const { result, rerender } = renderHook(
      ({ profileRevision }) =>
        useProfileEditorModel({ editingProfile: "mako", profileRevision }),
      { initialProps: { profileRevision: 0 } },
    );
    await waitFor(() => expect(result.current.profileOptions).toHaveLength(2));
    mocks.getProfiles.mockResolvedValue({
      success: true,
      profiles: ["mako", "game-123", "new-profile"],
      current_profile: "mako",
      profile_details: [
        defaultProfile,
        gameProfile,
        {
          profile_name: "new-profile",
          display_name: "New Profile",
          kind: "process",
          processes: ["streaming_client"],
        },
      ],
    });
    rerender({ profileRevision: 1 });
    await waitFor(() => expect(result.current.profileOptions).toHaveLength(3));
    expect(result.current.selectedProfile).toBe("mako");
    expect(mocks.setCurrentProfile).not.toHaveBeenCalled();
  });

  test("rejects a profile that is absent from the refreshed editor list", async () => {
    const onProfileChange = vi.fn(async () => undefined);
    const { result } = renderHook(() =>
      useProfileEditorModel({ onProfileChange }),
    );
    await waitFor(() => expect(result.current.profileOptions).toHaveLength(2));

    await act(() => result.current.switchProfile("missing-profile"));

    expect(result.current.selectedProfile).toBe("mako");
    expect(onProfileChange).not.toHaveBeenCalled();
    expect(mocks.showErrorToast).toHaveBeenCalledWith(
      "Failed to switch profile",
      "Error: Profile 'missing-profile' does not exist",
    );
  });

  test("creates an ordinary profile from the selection and selects the result", async () => {
    const onProfileChange = vi.fn(async () => undefined);
    const { result } = renderHook(() =>
      useProfileEditorModel({ onProfileChange }),
    );
    await waitFor(() => expect(result.current.profileOptions).toHaveLength(2));
    mocks.createProfile.mockResolvedValue({
      success: true,
      profile_name: "Streaming",
    });
    mocks.getProfiles.mockResolvedValue({
      success: true,
      profiles: ["mako", "game-123", "Streaming"],
      current_profile: "mako",
    });
    await act(() => result.current.createSelectedProfile("Streaming"));
    expect(mocks.createProfile).toHaveBeenCalledWith("Streaming", "mako");
    expect(onProfileChange).toHaveBeenCalledWith("Streaming");
    expect(result.current.selectedProfile).toBe("Streaming");
    expect(mocks.captureGameProfile).not.toHaveBeenCalled();
  });

  test.each(["create", "rename", "delete", "capture"])(
    "%s waits for pending settings before mutating profiles",
    async (operation) => {
      let finishSave!: () => void;
      const onBeforeProfileMutation = vi.fn(
        () =>
          new Promise<void>((resolve) => {
            finishSave = resolve;
          }),
      );
      const { result } = renderHook(() =>
        useProfileEditorModel({
          editingProfile: "game-123",
          onBeforeProfileMutation,
          mainRunningApp: { appid: 123, display_name: "Test Game" } as never,
        }),
      );
      await waitFor(() =>
        expect(result.current.profileOptions).toHaveLength(2),
      );
      const mutation =
        operation === "create"
          ? mocks.createProfile
          : operation === "rename"
            ? mocks.renameProfile
            : operation === "delete"
              ? mocks.deleteProfile
              : mocks.captureGameProfile;
      mutation.mockResolvedValue({
        success: true,
        profile_name: "game-123",
        current_profile: "mako",
      });
      let pending!: Promise<void>;
      act(() => {
        pending =
          operation === "create"
            ? result.current.createSelectedProfile("copy")
            : operation === "rename"
              ? result.current.renameSelectedProfile("renamed")
              : operation === "delete"
                ? result.current.deleteSelectedProfile()
                : result.current.saveRunningGame();
      });
      expect(result.current.isLoading).toBe(true);
      expect(mutation).not.toHaveBeenCalled();
      await act(async () => {
        finishSave();
        await pending;
      });
      expect(mutation).toHaveBeenCalledOnce();
    },
  );

  test("a failed preparation prevents profile creation", async () => {
    const { result } = renderHook(() =>
      useProfileEditorModel({
        onBeforeProfileMutation: async () => {
          throw new Error("save failed");
        },
      }),
    );
    await waitFor(() => expect(result.current.profileOptions).toHaveLength(2));
    await act(() => result.current.createSelectedProfile("copy"));
    expect(mocks.createProfile).not.toHaveBeenCalled();
    expect(result.current.selectedProfile).toBe("mako");
  });

  test("a created profile is not shown as selected when persistence fails", async () => {
    const { result } = renderHook(() =>
      useProfileEditorModel({
        onProfileChange: async () => {
          throw new Error("selection failed");
        },
      }),
    );
    await waitFor(() => expect(result.current.profileOptions).toHaveLength(2));
    mocks.createProfile.mockResolvedValue({
      success: true,
      profile_name: "Streaming",
    });
    mocks.getProfiles.mockResolvedValue({
      success: true,
      profiles: ["mako", "Streaming"],
      current_profile: "mako",
    });
    await act(() => result.current.createSelectedProfile("Streaming"));
    expect(result.current.selectedProfile).toBe("mako");
    expect(result.current.profileOptions).toHaveLength(2);
    expect(mocks.showErrorToast).toHaveBeenCalledWith(
      "Could not create profile",
      "Error: selection failed",
    );
  });

  test("captures a running game from the selected editor profile and refreshes details", async () => {
    const capturedProfile = {
      ...gameProfile,
      profile_name: "captured-123",
    };
    mocks.getProfiles
      .mockResolvedValueOnce({
        success: true,
        profiles: ["mako"],
        current_profile: "mako",
        profile_details: [defaultProfile],
      })
      .mockResolvedValueOnce({
        success: true,
        profiles: ["mako", "captured-123"],
        current_profile: "mako",
        profile_details: [defaultProfile, capturedProfile],
      });
    mocks.captureGameProfile.mockResolvedValue({
      success: true,
      profile_name: "captured-123",
      profile: capturedProfile,
    });
    const onProfileChange = vi.fn(async () => undefined);
    const mainRunningApp = { appid: 123, display_name: "Test Game" } as never;
    const { result } = renderHook(() =>
      useProfileEditorModel({ mainRunningApp, onProfileChange }),
    );
    await waitFor(() => expect(mocks.getProfiles).toHaveBeenCalledOnce());

    await act(() => result.current.saveRunningGame());

    expect(mocks.captureGameProfile).toHaveBeenCalledWith(
      "123",
      "Test Game",
      "mako",
    );
    expect(mocks.getProfiles).toHaveBeenCalledTimes(2);
    expect(result.current.selectedProfile).toBe("captured-123");
    expect(result.current.runningProfile?.profile_name).toBe("captured-123");
    expect(onProfileChange).toHaveBeenCalledWith("captured-123");
    expect(mocks.showSuccessToast).toHaveBeenCalledWith(
      "Game profile saved",
      "Test Game: test-game.exe",
    );
    expect(mocks.captureGameProfile.mock.invocationCallOrder[0]).toBeLessThan(
      mocks.showSuccessToast.mock.invocationCallOrder[0],
    );
    expect(mocks.showSuccessToast.mock.invocationCallOrder[0]).toBeLessThan(
      mocks.getProfiles.mock.invocationCallOrder[1],
    );
    expect(mocks.getProfiles.mock.invocationCallOrder[1]).toBeLessThan(
      onProfileChange.mock.invocationCallOrder[0],
    );
  });

  test("preserves rename and delete transaction ordering", async () => {
    const renamedProfile = {
      ...gameProfile,
      profile_name: "renamed-123",
      display_name: "Friendly Name",
    };
    mocks.getProfiles
      .mockResolvedValueOnce({
        success: true,
        profiles: ["mako", "game-123"],
        current_profile: "mako",
        profile_details: [defaultProfile, gameProfile],
      })
      .mockResolvedValueOnce({
        success: true,
        profiles: ["mako", "renamed-123"],
        current_profile: "mako",
        profile_details: [defaultProfile, renamedProfile],
      })
      .mockResolvedValueOnce({
        success: true,
        profiles: ["mako"],
        current_profile: "mako",
        profile_details: [defaultProfile],
      });
    mocks.renameProfile.mockResolvedValue({
      success: true,
      profile_name: "renamed-123",
    });
    mocks.deleteProfile.mockResolvedValue({
      success: true,
      current_profile: "mako",
    });
    const onProfileChange = vi.fn(async () => undefined);
    const { result } = renderHook(() =>
      useProfileEditorModel({
        editingProfile: "game-123",
        onProfileChange,
      }),
    );
    await waitFor(() =>
      expect(result.current.selectedProfile).toBe("game-123"),
    );

    await act(() => result.current.renameSelectedProfile("Friendly Name"));
    expect(mocks.renameProfile).toHaveBeenCalledWith(
      "game-123",
      "Friendly Name",
    );
    expect(result.current.selectedProfile).toBe("renamed-123");
    expect(onProfileChange).toHaveBeenLastCalledWith("renamed-123");
    expect(mocks.showSuccessToast).toHaveBeenCalledWith(
      "Profile renamed",
      "Friendly Name",
    );
    expect(mocks.renameProfile.mock.invocationCallOrder[0]).toBeLessThan(
      mocks.getProfiles.mock.invocationCallOrder[1],
    );
    expect(mocks.getProfiles.mock.invocationCallOrder[1]).toBeLessThan(
      onProfileChange.mock.invocationCallOrder[0],
    );
    expect(onProfileChange.mock.invocationCallOrder[0]).toBeLessThan(
      mocks.showSuccessToast.mock.invocationCallOrder[0],
    );

    await act(() => result.current.deleteSelectedProfile());
    expect(mocks.deleteProfile).toHaveBeenCalledWith("renamed-123");
    expect(result.current.selectedProfile).toBe("mako");
    expect(onProfileChange).toHaveBeenLastCalledWith("mako");
    expect(mocks.showSuccessToast).toHaveBeenCalledWith(
      "Profile deleted",
      "Friendly Name",
    );
    expect(mocks.deleteProfile.mock.invocationCallOrder[0]).toBeLessThan(
      mocks.getProfiles.mock.invocationCallOrder[2],
    );
    expect(mocks.getProfiles.mock.invocationCallOrder[2]).toBeLessThan(
      onProfileChange.mock.invocationCallOrder[1],
    );
    expect(onProfileChange.mock.invocationCallOrder[1]).toBeLessThan(
      mocks.showSuccessToast.mock.invocationCallOrder[1],
    );
  });
});
