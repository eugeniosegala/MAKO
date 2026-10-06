import { act, cleanup, renderHook, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";
import {
  getDefaults,
  type ConfigurationData,
} from "../../src/config/configSchema";

const mocks = vi.hoisted(() => ({
  checkMakoInstalled: vi.fn(),
  checkLosslessScalingDll: vi.fn(),
  getMakoConfig: vi.fn(),
  getProfileConfig: vi.fn(),
  updateMakoConfigFromObject: vi.fn(),
  showErrorToast: vi.fn(),
}));

vi.mock("../../src/api/makoApi", () => ({
  checkMakoInstalled: mocks.checkMakoInstalled,
  checkLosslessScalingDll: mocks.checkLosslessScalingDll,
  getMakoConfig: mocks.getMakoConfig,
  getProfileConfig: mocks.getProfileConfig,
  updateMakoConfigFromObject: mocks.updateMakoConfigFromObject,
}));
vi.mock("../../src/utils/toastUtils", () => ({
  showErrorToast: mocks.showErrorToast,
  ToastMessages: {
    CONFIG_UPDATE_ERROR: {
      title: "Update Failed",
      body: "Failed to update configuration",
    },
  },
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));

import {
  useInstallationStatus,
  useDllDetection,
  useMakoConfig,
} from "../../src/hooks/useMakoHooks";

describe("native host installation boundary", () => {
  beforeEach(() => vi.clearAllMocks());
  afterEach(cleanup);

  test("disables installation only after an explicit unsupported-host result", async () => {
    mocks.checkMakoInstalled.mockResolvedValue({
      installed: false,
      host_architecture: "aarch64",
      host_architecture_supported: false,
      engine_update_required: false,
      error: "MAKO Renderer is disabled on this host",
    });

    const { result } = renderHook(() => useInstallationStatus());

    await waitFor(() =>
      expect(result.current.hostArchitectureSupported).toBe(false),
    );
    expect(result.current.installationStatus).toBe(
      "MAKO Renderer is disabled on this host",
    );
  });

  test("does not treat a transient backend failure as unsupported hardware", async () => {
    mocks.checkMakoInstalled.mockRejectedValue(new Error("Decky reloading"));

    const { result } = renderHook(() => useInstallationStatus());

    await waitFor(() =>
      expect(result.current.installationStatus).toBe(
        "MAKO Renderer not installed",
      ),
    );
    expect(mocks.checkMakoInstalled).toHaveBeenCalledOnce();
    expect(result.current.hostArchitectureSupported).toBe(true);
    expect(result.current.installationStatus).toBe(
      "MAKO Renderer not installed",
    );
  });
});

describe("Lossless Scaling availability", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    vi.useFakeTimers();
  });
  afterEach(() => {
    cleanup();
    vi.useRealTimers();
  });

  test("checks the saved path, refreshes after install, and ignores stale profile results", async () => {
    let finishOld!: (value: { detected: boolean; error: null }) => void;
    mocks.checkLosslessScalingDll.mockReturnValueOnce(
      new Promise((resolve) => {
        finishOld = resolve;
      }),
    );
    mocks.checkLosslessScalingDll.mockResolvedValue({
      detected: false,
      error: null,
    });
    const { result, rerender } = renderHook((dll) => useDllDetection(dll), {
      initialProps: "/old.dll",
    });
    expect(result.current.dllMissing).toBe(false);
    rerender("/new.dll");
    await act(async () => {});
    expect(mocks.checkLosslessScalingDll).toHaveBeenLastCalledWith("/new.dll");
    expect(result.current.dllMissing).toBe(true);
    expect(result.current.dllDetectionStatus).toContain(
      "Lossless Scaling not found",
    );
    await act(async () => finishOld({ detected: true, error: null }));
    expect(result.current.dllMissing).toBe(true);
    mocks.checkLosslessScalingDll.mockResolvedValue({
      detected: true,
      error: null,
    });
    await act(() => vi.advanceTimersByTimeAsync(30000));
    expect(result.current.dllMissing).toBe(false);
    expect(result.current.dllDetected).toBe(true);
  });

  test("RPC and discovery failures stay unknown instead of claiming a missing installation", async () => {
    mocks.checkLosslessScalingDll.mockRejectedValue(new Error("offline"));
    const { result } = renderHook(() => useDllDetection());
    await act(async () => {});
    expect(result.current.dllMissing).toBe(false);
    expect(result.current.dllDetectionStatus).toContain("Unable to check");
    mocks.checkLosslessScalingDll.mockResolvedValue({
      detected: false,
      error: "permission denied",
    });
    await act(() => vi.advanceTimersByTimeAsync(30000));
    expect(result.current.dllMissing).toBe(false);
  });
});

describe("MAKO configuration persistence", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    mocks.getMakoConfig.mockResolvedValue({
      success: true,
      config: getDefaults(),
    });
  });
  afterEach(cleanup);

  test("loads custom catalogs per profile and clears them for older backends", async () => {
    const { result } = renderHook(() => useMakoConfig());
    await waitFor(() => expect(result.current.isConfigLoading).toBe(false));
    const catalog = [
      { id: "custom/Tone_A", name: "Tone_A", path: "/tmp/tone.fx" },
    ];
    mocks.getProfileConfig.mockResolvedValueOnce({
      success: true,
      config: getDefaults(),
      custom_shader_effects: catalog,
    });
    await act(() => result.current.loadMakoConfig("game"));
    expect(result.current.customShaderEffects).toEqual(catalog);
    mocks.getProfileConfig.mockResolvedValueOnce({
      success: true,
      config: getDefaults(),
    });
    await act(() => result.current.loadMakoConfig("other"));
    expect(result.current.customShaderEffects).toEqual([]);
  });

  test("locks editing during power-mode loads and clears metadata on failure", async () => {
    const { result } = renderHook(() => useMakoConfig());
    await waitFor(() => expect(result.current.isConfigLoading).toBe(false));
    mocks.getProfileConfig.mockResolvedValue({
      success: true,
      config: { ...getDefaults(), target_fps: 60 },
      separate_power_modes: true,
      power_mode: "handheld",
      power_source: "docked",
    });
    await act(() => result.current.loadMakoConfig("game", "handheld"));
    expect(result.current.powerMode).toBe("handheld");
    expect(result.current.separatePowerModes).toBe(true);
    expect(result.current.powerSource).toBe("docked");
    expect(result.current.canEditConfig()).toBe(true);

    let finishLoad!: (value: { success: boolean; error: string }) => void;
    mocks.getProfileConfig.mockReturnValue(
      new Promise((resolve) => {
        finishLoad = resolve;
      }),
    );
    let loading!: Promise<void>;
    act(() => {
      loading = result.current.loadMakoConfig("game", "docked");
    });
    expect(result.current.isConfigLoading).toBe(true);
    expect(result.current.canEditConfig()).toBe(false);
    await act(async () => {
      finishLoad({ success: false, error: "read failed" });
      await loading;
    });
    expect(result.current.isConfigLoading).toBe(false);
    expect(result.current.canEditConfig()).toBe(true);
    expect(result.current.powerMode).toBe("shared");
    expect(result.current.getEditingPowerMode()).toBe("shared");
    expect(result.current.separatePowerModes).toBe(false);
    expect(result.current.powerSource).toBe("");
  });

  test("keeps a newer profile load when an older request completes late", async () => {
    let finishInitialLoad!: (value: {
      success: boolean;
      config: ConfigurationData;
      custom_shader_effects?: { id: string; name: string; path: string }[];
    }) => void;
    mocks.getMakoConfig.mockReturnValue(
      new Promise((resolve) => {
        finishInitialLoad = resolve;
      }),
    );
    mocks.getProfileConfig.mockResolvedValue({
      success: true,
      config: { ...getDefaults(), multiplier: 4, target_fps: 120 },
      vkbasalt_config_path: "/home/deck/.config/mako-render/vkbasalt/abc.conf",
      custom_shader_effects: [
        { id: "custom/Game", name: "Game", path: "/tmp/game.fx" },
      ],
    });
    const { result } = renderHook(() => useMakoConfig());

    await act(() => result.current.loadMakoConfig("game-profile"));
    expect(result.current.config.multiplier).toBe(4);
    expect(result.current.vkBasaltConfigPath).toBe(
      "/home/deck/.config/mako-render/vkbasalt/abc.conf",
    );

    await act(async () => {
      finishInitialLoad({
        success: true,
        config: { ...getDefaults(), multiplier: 2, target_fps: 60 },
        custom_shader_effects: [
          { id: "custom/Stale", name: "Stale", path: "/tmp/stale.fx" },
        ],
      });
      await Promise.resolve();
    });

    expect(result.current.config.multiplier).toBe(4);
    expect(result.current.config.target_fps).toBe(120);
    expect(
      result.current.customShaderEffects.map((effect) => effect.id),
    ).toEqual(["custom/Game"]);
    expect(result.current.vkBasaltConfigPath).toBe(
      "/home/deck/.config/mako-render/vkbasalt/abc.conf",
    );
  });

  test("locks before draining saves and skips superseded loads waiting on the queue", async () => {
    const { result } = renderHook(() => useMakoConfig());
    await waitFor(() => expect(result.current.isConfigLoading).toBe(false));
    let finishFlush!: () => void;
    const flushing = new Promise<void>((resolve) => {
      finishFlush = resolve;
    });
    const beforeLoad = vi.fn(() => flushing);
    mocks.getProfileConfig.mockResolvedValue({
      success: true,
      config: { ...getDefaults(), vkbasalt_sharpness: 0.73 },
      power_mode: "docked",
    });
    let oldLoad!: Promise<void>;
    let newLoad!: Promise<void>;
    act(() => {
      oldLoad = result.current.loadMakoConfig("game", "handheld", beforeLoad);
      newLoad = result.current.loadMakoConfig("game", "docked", beforeLoad);
    });
    expect(result.current.canEditConfig()).toBe(false);
    expect(mocks.getProfileConfig).not.toHaveBeenCalled();
    await act(async () => {
      finishFlush();
      await Promise.all([oldLoad, newLoad]);
    });
    expect(mocks.getProfileConfig).toHaveBeenCalledExactlyOnceWith(
      "game",
      "docked",
    );
    expect(result.current.config.vkbasalt_sharpness).toBe(0.73);
    expect(result.current.canEditConfig()).toBe(true);
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
      adaptive: true,
    });
    expect(result.current.config.multiplier).toBe(3);
    expect(result.current.config.disable_hdr_exposure).toBe(true);
    expect(result.current.config.external_vulkan_layer).toBe("");

    mocks.updateMakoConfigFromObject.mockResolvedValue({
      success: false,
      error: "write failed",
    });
    await act(() =>
      result.current.updateConfig({ ...result.current.config, multiplier: 4 }),
    );

    expect(result.current.config.multiplier).toBe(3);
    expect(mocks.showErrorToast).toHaveBeenCalledWith(
      "Update Failed",
      "write failed",
    );
  });

  test("keeps external tools off when an older backend omits the selector", async () => {
    mocks.getMakoConfig.mockResolvedValue({
      success: true,
      config: { multiplier: 3 } as ConfigurationData,
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
      } as ConfigurationData,
    });

    const { result } = renderHook(() => useMakoConfig());

    await waitFor(() => expect(result.current.config.multiplier).toBe(3));
    expect(result.current.config.scaling_enabled).toBe(false);
    expect(result.current.config.scaling_factor).toBe(1.5);
    expect(result.current.config.scaling_sharpness).toBe(0.8);
    expect(result.current.config.frame_generation_enabled).toBe(true);
  });
});
