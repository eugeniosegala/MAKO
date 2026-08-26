import { act, cleanup, renderHook, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";
import { getDefaults, type ConfigurationData } from "../../src/config/configSchema";

const mocks = vi.hoisted(() => ({
  checkMakoInstalled: vi.fn(),
  checkLosslessScalingDll: vi.fn(),
  getMakoConfig: vi.fn(),
  getProfileConfig: vi.fn(),
  updateMakoConfigFromObject: vi.fn(),
  showErrorToast: vi.fn()
}));

vi.mock("../../src/api/makoApi", () => ({
  checkMakoInstalled: mocks.checkMakoInstalled,
  checkLosslessScalingDll: mocks.checkLosslessScalingDll,
  getMakoConfig: mocks.getMakoConfig,
  getProfileConfig: mocks.getProfileConfig,
  updateMakoConfigFromObject: mocks.updateMakoConfigFromObject
}));
vi.mock("../../src/utils/toastUtils", () => ({
  showErrorToast: mocks.showErrorToast,
  ToastMessages: {
    CONFIG_UPDATE_ERROR: { title: "Update Failed", body: "Failed to update configuration" }
  }
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback
}));

import { useInstallationStatus, useMakoConfig } from "../../src/hooks/useMakoHooks";

describe("native host installation boundary", () => {
  beforeEach(() => vi.clearAllMocks());
  afterEach(cleanup);

  test("disables installation only after an explicit unsupported-host result", async () => {
    mocks.checkMakoInstalled.mockResolvedValue({
      installed: false,
      host_architecture: "aarch64",
      host_architecture_supported: false,
      engine_update_required: false,
      error: "MAKO Renderer is disabled on this host"
    });

    const { result } = renderHook(() => useInstallationStatus());

    await waitFor(() => expect(result.current.hostArchitectureSupported).toBe(false));
    expect(result.current.installationStatus).toBe(
      "MAKO Renderer is disabled on this host"
    );
  });

  test("does not treat a transient backend failure as unsupported hardware", async () => {
    mocks.checkMakoInstalled.mockRejectedValue(new Error("Decky reloading"));

    const { result } = renderHook(() => useInstallationStatus());

    await waitFor(() => expect(mocks.checkMakoInstalled).toHaveBeenCalledOnce());
    expect(result.current.hostArchitectureSupported).toBe(true);
    expect(result.current.installationStatus).toBe("MAKO Renderer not installed");
  });
});

describe("MAKO configuration persistence", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    mocks.getMakoConfig.mockResolvedValue({ success: true, config: getDefaults() });
  });
  afterEach(cleanup);

  test("keeps a newer profile load when an older request completes late", async () => {
    let finishInitialLoad!: (value: { success: boolean; config: ConfigurationData }) => void;
    mocks.getMakoConfig.mockReturnValue(new Promise((resolve) => {
      finishInitialLoad = resolve;
    }));
    mocks.getProfileConfig.mockResolvedValue({
      success: true,
      config: { ...getDefaults(), multiplier: 4, target_fps: 120 }
    });
    const { result } = renderHook(() => useMakoConfig());

    await act(() => result.current.loadMakoConfig("game-profile"));
    expect(result.current.config.multiplier).toBe(4);

    await act(async () => {
      finishInitialLoad({
        success: true,
        config: { ...getDefaults(), multiplier: 2, target_fps: 60 }
      });
      await Promise.resolve();
    });

    expect(result.current.config.multiplier).toBe(4);
    expect(result.current.config.target_fps).toBe(120);
  });

  test("fills missing defaults before writing and commits state only after success", async () => {
    mocks.updateMakoConfigFromObject.mockResolvedValue({ success: true });
    const { result } = renderHook(() => useMakoConfig());
    await waitFor(() => expect(mocks.getMakoConfig).toHaveBeenCalledOnce());
    const partial = { multiplier: 3, adaptive: true } as ConfigurationData;

    await act(() => result.current.updateConfig(partial));

    expect(mocks.updateMakoConfigFromObject).toHaveBeenCalledWith({
      ...getDefaults(),
      multiplier: 3,
      adaptive: true
    });
    expect(result.current.config.multiplier).toBe(3);
    expect(result.current.config.disable_hdr_exposure).toBe(true);
    expect(result.current.config.external_vulkan_layer).toBe("");

    mocks.updateMakoConfigFromObject.mockResolvedValue({ success: false, error: "write failed" });
    await act(() => result.current.updateConfig({ ...result.current.config, multiplier: 4 }));

    expect(result.current.config.multiplier).toBe(3);
    expect(mocks.showErrorToast).toHaveBeenCalledWith("Update Failed", "write failed");
  });

  test("keeps external tools off when an older backend omits the selector", async () => {
    mocks.getMakoConfig.mockResolvedValue({
      success: true,
      config: { multiplier: 3 } as ConfigurationData
    });

    const { result } = renderHook(() => useMakoConfig());

    await waitFor(() => expect(result.current.config.multiplier).toBe(3));
    expect(result.current.config.external_vulkan_layer).toBe("");
  });

  test("keeps scaling inert when an older backend omits its fields", async () => {
    mocks.getMakoConfig.mockResolvedValue({
      success: true,
      config: {
        multiplier: 3,
        frame_generation_enabled: true,
      } as ConfigurationData
    });

    const { result } = renderHook(() => useMakoConfig());

    await waitFor(() => expect(result.current.config.multiplier).toBe(3));
    expect(result.current.config.scaling_enabled).toBe(false);
    expect(result.current.config.scaling_factor).toBe(1.5);
    expect(result.current.config.scaling_sharpness).toBe(0.5);
    expect(result.current.config.frame_generation_enabled).toBe(true);
  });
});
