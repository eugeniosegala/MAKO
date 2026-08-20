import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";

const mocks = vi.hoisted(() => ({
  installMako: vi.fn(),
  uninstallMako: vi.fn(),
  showInstallErrorToast: vi.fn(),
  showUninstallSuccessToast: vi.fn(),
  showUninstallErrorToast: vi.fn()
}));

vi.mock("../../src/api/makoApi", () => ({
  installMako: mocks.installMako,
  uninstallMako: mocks.uninstallMako
}));
vi.mock("../../src/utils/toastUtils", () => ({
  showInstallErrorToast: mocks.showInstallErrorToast,
  showUninstallSuccessToast: mocks.showUninstallSuccessToast,
  showUninstallErrorToast: mocks.showUninstallErrorToast
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback
}));

import { useInstallationActions } from "../../src/hooks/useInstallationActions";

describe("MAKO Renderer installation actions", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    vi.useFakeTimers();
  });
  afterEach(() => {
    vi.useRealTimers();
    cleanup();
  });

  test("keeps completion visible for three seconds before showing the installed UI", async () => {
    let finishInstall!: (value: { success: boolean; message: string }) => void;
    const installResult = new Promise<{ success: boolean; message: string }>((resolve) => {
      finishInstall = resolve;
    });
    mocks.installMako.mockReturnValue(installResult);
    const setInstalled = vi.fn();
    const setStatus = vi.fn();
    const reloadConfig = vi.fn().mockResolvedValue(undefined);
    const { result } = renderHook(() => useInstallationActions());

    let operation!: Promise<void>;
    act(() => {
      operation = result.current.handleInstall(setInstalled, setStatus, reloadConfig, "update");
    });

    expect(result.current.isInstalling).toBe(true);
    expect(setStatus).toHaveBeenLastCalledWith("Updating MAKO Renderer...");

    await act(async () => {
      finishInstall({ success: true, message: "Renderer updated" });
      await Promise.resolve();
    });

    expect(setStatus).toHaveBeenLastCalledWith("Renderer updated");
    expect(reloadConfig).toHaveBeenCalledOnce();
    expect(result.current.isInstallCompletionVisible).toBe(true);
    expect(setInstalled).not.toHaveBeenCalled();
    expect(result.current.isInstalling).toBe(true);

    await act(async () => {
      await vi.advanceTimersByTimeAsync(2999);
    });
    expect(result.current.isInstallCompletionVisible).toBe(true);
    expect(setInstalled).not.toHaveBeenCalled();

    await act(async () => {
      await vi.advanceTimersByTimeAsync(1);
      await operation;
    });
    expect(result.current.isInstallCompletionVisible).toBe(false);
    expect(setInstalled).toHaveBeenCalledWith(true);
    expect(result.current.isInstalling).toBe(false);
  });

  test("does not report an installation when the backend rejects it", async () => {
    mocks.installMako.mockResolvedValue({ success: false, error: "checksum mismatch" });
    const setInstalled = vi.fn();
    const setStatus = vi.fn();
    const { result } = renderHook(() => useInstallationActions());

    await act(() => result.current.handleInstall(setInstalled, setStatus));

    expect(setInstalled).not.toHaveBeenCalled();
    expect(setStatus).toHaveBeenLastCalledWith("Installation failed: checksum mismatch");
    expect(mocks.showInstallErrorToast).toHaveBeenCalledWith("checksum mismatch");
    expect(result.current.isInstallCompletionVisible).toBe(false);
  });
});
