import { beforeEach, describe, expect, test, vi } from "vitest";

const { callableMock } = vi.hoisted(() => ({
  callableMock: vi.fn((method: string) => vi.fn((...args: unknown[]) =>
    Promise.resolve({ method, args })))
}));

vi.mock("@decky/api", () => ({ callable: callableMock }));

describe("Decky RPC contract", () => {
  beforeEach(() => {
    vi.resetModules();
    callableMock.mockClear();
  });

  test("registers every frontend operation under its backend method name", async () => {
    await import("../../src/api/makoApi");

    expect(callableMock.mock.calls.map(([method]) => method)).toEqual([
      "install_mako",
      "uninstall_mako",
      "check_mako_installed",
      "check_lossless_scaling_dll",
      "get_dll_stats",
      "get_mako_config",
      "get_profile_config",
      "get_runtime_status",
      "get_config_schema",
      "get_launch_option",
      "get_config_file_content",
      "get_launch_script_content",
      "check_fgmod_directory",
      "check_flatpak_extension_status",
      "install_flatpak_extension",
      "uninstall_flatpak_extension",
      "get_flatpak_apps",
      "set_flatpak_app_override",
      "remove_flatpak_app_override",
      "update_mako_config",
      "get_profiles",
      "create_profile",
      "delete_profile",
      "rename_profile",
      "capture_game_profile",
      "set_current_profile",
      "sync_current_profile",
      "update_profile_config",
      "update_profile_config_fields",
    ]);
  });

  test("forwards a complete configuration object without reshaping it", async () => {
    const api = await import("../../src/api/makoApi");
    const config = (await import("../../src/config/configSchema")).getDefaults();

    await api.updateMakoConfigFromObject(config);

    const updateIndex = callableMock.mock.calls.findIndex(([method]) => method === "update_mako_config");
    const updateBinding = callableMock.mock.results[updateIndex]?.value as ReturnType<typeof vi.fn>;
    expect(updateBinding).toHaveBeenCalledWith(config);
  });

  test("forwards a profile field patch without expanding stale defaults", async () => {
    const api = await import("../../src/api/makoApi");
    const changes = { target_fps: 90 };

    await api.updateProfileConfigFields("game-profile", changes);

    const updateIndex = callableMock.mock.calls.findIndex(
      ([method]) => method === "update_profile_config_fields",
    );
    const updateBinding = callableMock.mock.results[updateIndex]
      ?.value as ReturnType<typeof vi.fn>;
    expect(updateBinding).toHaveBeenCalledWith("game-profile", changes);
  });
});
