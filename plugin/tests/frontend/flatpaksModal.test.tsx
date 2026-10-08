import React from "react";
import {
  cleanup,
  act,
  fireEvent,
  render,
  screen,
  waitFor,
  within,
} from "@testing-library/react";
import { afterEach, describe, expect, test, vi } from "vitest";

const api = vi.hoisted(() => ({
  checkFlatpakExtensionStatus: vi.fn(),
  getFlatpakApps: vi.fn(),
  getLaunchOption: vi.fn(),
  installFlatpakExtension: vi.fn(),
  uninstallFlatpakExtension: vi.fn(),
  setFlatpakAppOverride: vi.fn(),
  removeFlatpakAppOverride: vi.fn(),
}));
const navigation = vi.hoisted(() => ({
  NavigateToExternalWeb: vi.fn(),
}));
const modalUi = vi.hoisted(() => ({ showModal: vi.fn() }));

vi.mock("@decky/ui", () => ({
  ModalRoot: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  DialogBody: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  DialogHeader: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  DialogControlsSection: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  ButtonItem: ({
    children,
    onClick,
  }: {
    children: React.ReactNode;
    onClick?: () => void;
  }) => <button onClick={onClick}>{children}</button>,
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  Toggle: ({
    value,
    onChange,
  }: {
    value: boolean;
    onChange?: (value: boolean) => void;
  }) => (
    <button
      aria-label="Flatpak application toggle"
      onClick={() => onChange?.(!value)}
    >
      {value ? "Enabled" : "Disabled"}
    </button>
  ),
  Focusable: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  Navigation: navigation,
  showModal: modalUi.showModal,
  ConfirmModal: () => <div />,
}));
vi.mock("../../src/api/makoApi", () => api);
vi.mock("../../src/components/MakoUi", () => ({
  MakoCompactSpinner: () => <span>Working</span>,
  MakoFocusable: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  makoPanelDivider: "1px solid",
  makoPanelItemStyle: {},
  makoPanelSectionHeaderStyle: {},
  makoPanelStyle: {},
}));
vi.mock("../../src/utils/toastUtils", () => ({
  showErrorToast: vi.fn(),
  showSuccessToast: vi.fn(),
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (
    _key: string,
    fallback: string,
    values: Record<string, string> = {},
  ) =>
    fallback.replace(
      /\{(\w+)\}/g,
      (_match, key: string) => values[key] ?? `{${key}}`,
    ),
}));

import { FlatpaksModal } from "../../src/components/FlatpaksModal";

afterEach(() => {
  cleanup();
  vi.clearAllMocks();
});

describe("Flatpak application preparation", () => {
  test.each([
    "Install the MAKO 26.08 runtime extension before enabling this application.",
    "Could not determine a supported Flatpak runtime for this application. Install the matching MAKO runtime extension first.",
  ])(
    "shows preparation guidance and keeps the app disabled: %s",
    async (error) => {
      window.SP_REACT = React;
      api.checkFlatpakExtensionStatus.mockResolvedValue({
        success: true,
        installed_23_08: false,
        installed_24_08: false,
        installed_25_08: false,
        installed_26_08: false,
      });
      api.getFlatpakApps.mockResolvedValue({
        success: true,
        apps: [
          {
            app_id: "org.DolphinEmu.dolphin-emu",
            app_name: "Dolphin",
            has_filesystem_override: false,
            has_wrapper_override: false,
            has_env_override: false,
            has_required_env_override: false,
          },
        ],
      });
      api.getLaunchOption.mockResolvedValue({
        wrapper_path: "/home/deck/.local/bin/mako-run",
      });
      api.setFlatpakAppOverride.mockResolvedValue({ success: false, error });

      render(<FlatpaksModal />);
      const toggle = await screen.findByRole("button", {
        name: "Flatpak application toggle",
      });
      fireEvent.click(toggle);

      expect(await screen.findByText(error)).toBeTruthy();
      expect(toggle.textContent).toBe("Disabled");
      expect(api.setFlatpakAppOverride).toHaveBeenCalledWith(
        "org.DolphinEmu.dolphin-emu",
      );
      expect(api.getFlatpakApps).toHaveBeenCalledTimes(1);
    },
  );

  test("installs, updates, and removes 26.08 while retaining older runtime rows", async () => {
    window.SP_REACT = React;
    let installed = false;
    api.checkFlatpakExtensionStatus.mockImplementation(async () => ({
      success: true,
      installed_23_08: false,
      installed_24_08: false,
      installed_25_08: false,
      installed_26_08: installed,
    }));
    api.getFlatpakApps.mockResolvedValue({ success: true, apps: [] });
    api.getLaunchOption.mockResolvedValue({
      wrapper_path: "/home/deck/.local/bin/mako-run",
    });
    api.installFlatpakExtension.mockImplementation(async () => {
      installed = true;
      return { success: true };
    });
    api.uninstallFlatpakExtension.mockImplementation(async () => {
      installed = false;
      return { success: true };
    });

    render(<FlatpaksModal />);
    await screen.findByText("Runtime 26.08");
    for (const version of ["23.08", "24.08", "25.08", "26.08"]) {
      expect(screen.getByText(`Runtime ${version}`)).toBeTruthy();
    }
    const row = () =>
      within(
        screen.getByText("Runtime 26.08").parentElement!.parentElement!
          .parentElement!,
      );
    fireEvent.click(row().getByRole("button", { name: "Install" }));
    await waitFor(() =>
      expect(row().getByRole("button", { name: "Update" })).toBeTruthy(),
    );
    expect(api.installFlatpakExtension).toHaveBeenCalledWith("26.08");
    expect(row().getByText("Installed")).toBeTruthy();

    fireEvent.click(row().getByRole("button", { name: "Update" }));
    await waitFor(() =>
      expect(api.installFlatpakExtension).toHaveBeenCalledTimes(2),
    );
    await waitFor(() =>
      expect(row().getByRole("button", { name: "Update" })).toBeTruthy(),
    );

    fireEvent.click(row().getByRole("button", { name: "Uninstall" }));
    expect(api.uninstallFlatpakExtension).not.toHaveBeenCalled();
    const confirmation = modalUi.showModal.mock
      .calls[0][0] as React.ReactElement<{
      onOK: () => void;
    }>;
    await act(async () => confirmation.props.onOK());
    await waitFor(() =>
      expect(row().getByRole("button", { name: "Install" })).toBeTruthy(),
    );
    expect(api.uninstallFlatpakExtension).toHaveBeenCalledWith("26.08");
    expect(row().getByText("Not installed")).toBeTruthy();
  });

  test("retains toggle focus while an application update is loading", async () => {
    window.SP_REACT = React;
    let resolveUpdate: (value: {
      success: boolean;
      message: string;
      error: string | null;
    }) => void;
    const pendingUpdate = new Promise<{
      success: boolean;
      message: string;
      error: string | null;
    }>((resolve) => {
      resolveUpdate = resolve;
    });

    api.checkFlatpakExtensionStatus.mockResolvedValue({
      success: true,
      message: "",
      error: null,
      installed_23_08: false,
      installed_24_08: false,
      installed_25_08: false,
      installed_26_08: false,
    });
    api.getFlatpakApps.mockResolvedValue({
      success: true,
      message: "",
      error: null,
      apps: [
        {
          app_id: "com.heroicgameslauncher.hgl",
          app_name: "Heroic",
          wrapper_path: "/home/deck/.local/bin/mako-run",
          has_filesystem_override: false,
          has_wrapper_override: false,
          has_env_override: false,
          has_required_env_override: false,
        },
      ],
      total_apps: 1,
    });
    api.getLaunchOption.mockResolvedValue({
      launch_option: "",
      wrapper_path: "/home/deck/.local/bin/mako-run",
      instructions: "",
      explanation: "",
    });
    api.setFlatpakAppOverride.mockReturnValue(pendingUpdate);

    render(<FlatpaksModal />);

    const toggle = await screen.findByRole("button", {
      name: "Flatpak application toggle",
    });
    toggle.focus();
    fireEvent.click(toggle);

    const busyOverlay = await screen.findByRole("status");
    expect(busyOverlay.style.inset).toBe("1px 4px 1px 1px");
    expect(busyOverlay.style.borderRadius).toBe("999px");
    expect(busyOverlay.style.alignItems).toBe("center");
    expect(busyOverlay.style.justifyContent).toBe("center");
    expect(busyOverlay.parentElement?.style.display).toBe("inline-flex");
    expect(
      screen.getByRole("button", { name: "Flatpak application toggle" }),
    ).toBe(toggle);
    expect(document.activeElement).toBe(toggle);

    resolveUpdate!({ success: false, message: "", error: "Unavailable" });
  });

  test.each([
    ["com.heroicgameslauncher.hgl", "Heroic"],
    ["net.lutris.Lutris", "Lutris"],
    ["org.DolphinEmu.dolphin-emu", "Dolphin"],
  ])(
    "keeps %s prepared after refresh and reopening, then allows removal",
    async (appId, appName) => {
      window.SP_REACT = React;
      const wrapperPath = "/home/deck/.local/bin/mako-run";
      const partialApp = {
        app_id: appId,
        app_name: appName,
        wrapper_path: wrapperPath,
        has_filesystem_override: true,
        has_wrapper_override: true,
        has_env_override: true,
        has_required_env_override: false,
      };
      const preparedApp = {
        ...partialApp,
        has_env_override: appName === "Dolphin",
        has_required_env_override: true,
      };
      api.checkFlatpakExtensionStatus.mockResolvedValue({
        success: true,
        installed_25_08: true,
      });
      api.getLaunchOption.mockResolvedValue({ wrapper_path: wrapperPath });
      api.getFlatpakApps
        .mockResolvedValueOnce({ success: true, apps: [partialApp] })
        .mockResolvedValue({ success: true, apps: [preparedApp] });
      api.setFlatpakAppOverride.mockResolvedValue({ success: true });
      api.removeFlatpakAppOverride.mockResolvedValue({ success: true });

      const modal = render(<FlatpaksModal />);
      const toggle = await screen.findByRole("button", {
        name: "Flatpak application toggle",
      });
      expect(toggle.textContent).toBe("Disabled");
      fireEvent.click(toggle);
      await waitFor(() => expect(toggle.textContent).toBe("Enabled"));
      expect(api.setFlatpakAppOverride).toHaveBeenCalledWith(appId);
      expect(api.getFlatpakApps).toHaveBeenCalledTimes(2);
      expect(screen.queryByText(/ - Partial\./)).toBeNull();

      modal.unmount();
      render(<FlatpaksModal />);
      const reopenedToggle = await screen.findByRole("button", {
        name: "Flatpak application toggle",
      });
      expect(reopenedToggle.textContent).toBe("Enabled");
      expect(api.getFlatpakApps).toHaveBeenCalledTimes(3);

      api.getFlatpakApps.mockResolvedValue({
        success: true,
        apps: [
          {
            ...partialApp,
            has_filesystem_override: false,
            has_wrapper_override: false,
            has_env_override: false,
          },
        ],
      });
      fireEvent.click(reopenedToggle);
      await waitFor(() => expect(reopenedToggle.textContent).toBe("Disabled"));
      expect(api.removeFlatpakAppOverride).toHaveBeenCalledWith(appId);
      expect(api.setFlatpakAppOverride).toHaveBeenCalledTimes(1);
    },
  );

  test.each([
    ["com.heroicgameslauncher.hgl", "Heroic"],
    ["net.lutris.Lutris", "Lutris"],
  ])(
    "re-prepares stale %s activation with per-game instructions",
    async (appId, appName) => {
      window.SP_REACT = React;
      const wrapperPath = "/var/home/test user/.local/bin/mako-run";
      api.checkFlatpakExtensionStatus.mockResolvedValue({
        success: true,
        installed_25_08: true,
      });
      api.getLaunchOption.mockResolvedValue({ wrapper_path: wrapperPath });
      api.getFlatpakApps.mockResolvedValue({
        success: true,
        apps: [
          {
            app_id: appId,
            app_name: appName,
            wrapper_path: wrapperPath,
            has_filesystem_override: true,
            has_wrapper_override: true,
            has_env_override: true,
            has_required_env_override: false,
          },
        ],
      });
      api.setFlatpakAppOverride.mockResolvedValue({
        success: false,
        error: "Unavailable",
      });

      render(<FlatpaksModal />);
      const toggle = await screen.findByRole("button", {
        name: "Flatpak application toggle",
      });
      expect(toggle.textContent).toBe("Disabled");
      expect(
        screen.getByText(
          `${appId} - Partial. Enable MAKO per game using ${wrapperPath}. See the launcher setup guide for the correct field.`,
        ),
      ).toBeTruthy();
      expect(
        screen.queryByText(/Preparation applies to this entire Flatpak app/),
      ).toBeNull();
      fireEvent.click(toggle);
      expect(api.setFlatpakAppOverride).toHaveBeenCalledWith(appId);
      expect(api.removeFlatpakAppOverride).not.toHaveBeenCalled();
      await screen.findByText("Unavailable");
    },
  );

  test("separates manual shortcuts from app-wide preparation and links the guide", async () => {
    window.SP_REACT = React;
    api.checkFlatpakExtensionStatus.mockResolvedValue({
      success: true,
      message: "",
      error: null,
      installed_23_08: false,
      installed_24_08: false,
      installed_25_08: true,
    });
    api.getFlatpakApps.mockResolvedValue({
      success: true,
      message: "",
      error: null,
      apps: [
        {
          app_id: "org.DolphinEmu.dolphin-emu",
          app_name: "Dolphin Emulator",
          wrapper_path: "/var/home/test/.local/bin/mako-run",
          has_filesystem_override: true,
          has_wrapper_override: true,
          has_env_override: true,
          has_required_env_override: true,
        },
      ],
      total_apps: 1,
    });
    api.getLaunchOption.mockResolvedValue({
      launch_option: "'/var/home/test user/.local/bin/mako-run' %command%",
      wrapper_path: "/var/home/test user/.local/bin/mako-run",
      instructions: "",
      explanation: "",
    });

    render(<FlatpaksModal />);

    expect(
      await screen.findByText("Manual Steam shortcut reference"),
    ).toBeTruthy();
    expect(
      screen.getByText(
        '"/var/home/test user/.local/bin/mako-run" "/usr/bin/flatpak"',
      ),
    ).toBeTruthy();
    expect(
      screen.getByText(/Preparation applies to this entire Flatpak app/),
    ).toBeTruthy();
    expect(
      screen.getByText(
        /Heroic, Lutris, and EmuDeck use the launcher guide/,
      ),
    ).toBeTruthy();

    fireEvent.click(screen.getByText("Open launcher setup guide"));
    expect(navigation.NavigateToExternalWeb).toHaveBeenCalledWith(
      "https://github.com/eugeniosegala/MAKO/blob/main/plugin/docs/LAUNCHERS.md",
    );
  });
});
