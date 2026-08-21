import { act, cleanup, renderHook, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";

const mocks = vi.hoisted(() => ({
  getProfiles: vi.fn(),
  deleteProfile: vi.fn(),
  renameProfile: vi.fn(),
  setCurrentProfile: vi.fn(),
  syncCurrentProfile: vi.fn(),
  updateProfileConfig: vi.fn(),
  updateProfileConfigFields: vi.fn(),
  showSuccessToast: vi.fn(),
  showErrorToast: vi.fn(),
}));

vi.mock("@decky/api", () => ({ callable: vi.fn() }));
vi.mock("../../src/api/makoApi", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../../src/api/makoApi")>();
  return {
    ...actual,
    getProfiles: mocks.getProfiles,
    deleteProfile: mocks.deleteProfile,
    renameProfile: mocks.renameProfile,
    setCurrentProfile: mocks.setCurrentProfile,
    syncCurrentProfile: mocks.syncCurrentProfile,
    updateProfileConfig: mocks.updateProfileConfig,
    updateProfileConfigFields: mocks.updateProfileConfigFields,
  };
});
vi.mock("../../src/utils/toastUtils", () => ({
  showSuccessToast: mocks.showSuccessToast,
  showErrorToast: mocks.showErrorToast,
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));

import { useProfileManagement } from "../../src/hooks/useProfileManagement";

describe("game profile selection", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    mocks.getProfiles.mockResolvedValue({
      success: true,
      profiles: ["mako", "elden-ring"],
      current_profile: "mako",
    });
  });
  afterEach(cleanup);

  test("changes the selected profile only after the backend accepts it", async () => {
    mocks.setCurrentProfile.mockResolvedValue({
      success: false,
      error: "game still running",
    });
    const { result } = renderHook(() => useProfileManagement());
    await waitFor(() =>
      expect(result.current.profiles).toEqual(["mako", "elden-ring"]),
    );

    await act(() => result.current.setCurrentProfile("elden-ring"));
    expect(result.current.currentProfile).toBe("mako");
    expect(mocks.showErrorToast).toHaveBeenCalledWith(
      "Failed to switch profile",
      "game still running",
    );

    mocks.setCurrentProfile.mockResolvedValue({
      success: true,
      profile_name: "elden-ring",
    });
    await act(() => result.current.setCurrentProfile("elden-ring"));
    expect(result.current.currentProfile).toBe("elden-ring");
    expect(mocks.showSuccessToast).toHaveBeenCalledWith(
      "Profile switched",
      "Switched to profile: elden-ring",
    );
  });

  test("never sends a request that deletes the default profile", async () => {
    const { result } = renderHook(() => useProfileManagement());
    await waitFor(() => expect(result.current.profiles).toContain("mako"));

    let response!: Awaited<ReturnType<typeof result.current.deleteProfile>>;
    await act(async () => {
      response = await result.current.deleteProfile("mako");
    });

    expect(response.success).toBe(false);
    expect(mocks.deleteProfile).not.toHaveBeenCalled();
    expect(mocks.showErrorToast).toHaveBeenCalledWith(
      "Cannot delete default profile",
      "The default profile cannot be deleted",
    );
  });
});
