import React from "react";
import { cleanup, fireEvent, render, screen } from "@testing-library/react";
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
  showModal: vi.fn(),
  ConfirmModal: () => <div />,
}));
vi.mock("../../src/api/makoApi", () => api);
vi.mock("../../src/components/MakoUi", () => ({
  MakoCompactSpinner: () => <span>Working</span>,
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
  default: (_key: string, fallback: string) => fallback,
}));

import { FlatpaksModal } from "../../src/components/FlatpaksModal";

afterEach(() => {
  cleanup();
  vi.clearAllMocks();
});

describe("Flatpak application preparation", () => {
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

    await screen.findByRole("status");
    expect(
      screen.getByRole("button", { name: "Flatpak application toggle" }),
    ).toBe(toggle);
    expect(document.activeElement).toBe(toggle);

    resolveUpdate!({ success: false, message: "", error: "Unavailable" });
  });
});
