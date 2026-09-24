import React from "react";
import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";
import type { InstallationStatus } from "../../src/api/makoApi";

const api = vi.hoisted(() => ({
  checkMakoInstalled: vi.fn(),
  checkLosslessScalingDll: vi.fn(),
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
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  ButtonItem: ({ children }: { children: React.ReactNode }) => (
    <button>{children}</button>
  ),
}));
vi.mock("../../src/api/makoApi", () => api);
vi.mock("../../src/components/MakoUi", () => ({
  MakoCompactSpinner: () => <span>Loading</span>,
  MakoFocusable: ({
    children,
    onClick,
    onActivate,
    style,
  }: {
    children: React.ReactNode;
    onClick?: () => void;
    onActivate?: () => void;
    style?: React.CSSProperties;
  }) => (
    <div
      role={onClick ? "button" : undefined}
      tabIndex={onClick ? 0 : undefined}
      onClick={onClick}
      onKeyDown={(event) => {
        if (event.key === "Enter") onActivate?.();
      }}
      style={style}
    >
      {children}
    </div>
  ),
  makoPanelDivider: "1px solid",
  makoPanelItemStyle: {},
  makoPanelSectionHeaderStyle: {},
  makoPanelStyle: {},
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));

import { AdvancedDetailsModal } from "../../src/components/AdvancedDetailsModal";

beforeEach(() => {
  window.SP_REACT = React;
});

const installation: InstallationStatus = {
  installed: true,
  lib_exists: true,
  json_exists: true,
  script_exists: true,
  lib_path: "/home/deck/.local/lib/libmako-render.so",
  json_path: "/home/deck/.local/share/vulkan/implicit_layer.d/mako.json",
  script_path: "/home/deck/.local/bin/mako-run",
  installed_engine_version: "3.2.0",
  expected_engine_version: "3.3.0",
  engine_version_known: true,
  engine_update_required: true,
  host_architecture: "x86_64",
  host_architecture_supported: true,
  error: null,
};

afterEach(() => {
  cleanup();
  vi.clearAllMocks();
});

describe("Advanced Details", () => {
  test("shows installed and bundled Renderer versions without reading raw files or hashing the DLL", async () => {
    const writeText = vi.fn().mockResolvedValue(undefined);
    Object.defineProperty(navigator, "clipboard", {
      configurable: true,
      value: { writeText },
    });
    api.checkMakoInstalled.mockResolvedValue(installation);
    api.checkLosslessScalingDll.mockResolvedValue({
      detected: true,
      path: "/games/Lossless.dll",
      source: "Steam",
      message: null,
      error: null,
    });

    render(<AdvancedDetailsModal />);

    expect(await screen.findByText("Bundled update available")).toBeTruthy();
    expect(screen.getByText("3.2.0")).toBeTruthy();
    expect(screen.getByText("3.3.0")).toBeTruthy();
    expect(screen.getByText("x86_64")).toBeTruthy();
    expect(screen.getByText(installation.script_path)).toBeTruthy();
    expect(screen.getByText("/games/Lossless.dll")).toBeTruthy();
    for (const value of [
      "Bundled update available",
      "3.2.0",
      "3.3.0",
      "x86_64",
      installation.lib_path,
      installation.json_path,
      installation.script_path,
      "Lossless Scaling installed",
      "/games/Lossless.dll",
      "Steam",
    ]) {
      const field = screen.getByText(value).closest('[role="button"]');
      expect(field).not.toBeNull();
      expect((field as HTMLElement).style.padding).toBe("8px 10px");
    }
    fireEvent.click(screen.getByText("3.2.0"));
    fireEvent.keyDown(screen.getByText("Steam"), { key: "Enter" });
    expect(writeText).toHaveBeenCalledWith("3.2.0");
    expect(writeText).toHaveBeenCalledWith("Steam");
    expect(screen.queryByText("Launch Script")).toBeNull();
    expect(screen.queryByText("DLL SHA256 Hash")).toBeNull();
    expect(api.checkMakoInstalled).toHaveBeenCalledOnce();
    expect(api.checkLosslessScalingDll).toHaveBeenCalledOnce();
  });

  test("does not describe stale version metadata as an installed Renderer", async () => {
    api.checkMakoInstalled.mockResolvedValue({
      ...installation,
      installed: false,
      script_exists: false,
    });
    api.checkLosslessScalingDll.mockResolvedValue({
      detected: false,
      path: null,
      source: null,
      message: null,
      error: null,
    });

    render(<AdvancedDetailsModal />);

    expect(await screen.findByText("Incomplete installation")).toBeTruthy();
    expect(screen.queryByText("3.2.0")).toBeNull();
    expect(screen.getByText("Lossless Scaling not detected")).toBeTruthy();
    expect(
      screen.getByText("Not available").closest('[role="button"]'),
    ).not.toBeNull();
  });
});
