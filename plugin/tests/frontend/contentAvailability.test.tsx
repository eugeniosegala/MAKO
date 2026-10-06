import React from "react";
import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, beforeEach, expect, test, vi } from "vitest";
import { getDefaults } from "../../src/config/configSchema";

const state = vi.hoisted(() => ({ installed: true, save: vi.fn() }));
function pass({ children }: { children: React.ReactNode }) {
  return <div>{children}</div>;
}
vi.mock("@decky/ui", () => ({
  PanelSection: pass,
  PanelSectionRow: pass,
  ButtonItem: pass,
  showModal: vi.fn(),
}));
vi.mock("../../src/hooks/useMakoHooks", () => ({
  useInstallationStatus: () => ({
    isInstalled: state.installed,
    engineUpdateRequired: false,
  }),
  useDllDetection: () => ({
    dllDetected: false,
    dllMissing: true,
    dllDetectionStatus: "missing",
  }),
  useMakoConfig: () => ({ config: getDefaults(), isConfigLoading: false }),
  useRuntimeScalingStatus: () => ({}),
}));
vi.mock("../../src/hooks/useModelStatus", () => ({
  useModelStatus: () => ({}),
}));
vi.mock("../../src/hooks/useProfileManagement", () => ({
  useProfileManagement: () => ({}),
}));
vi.mock("../../src/hooks/useInstallationActions", () => ({
  useInstallationActions: () => ({}),
}));
vi.mock("../../src/hooks/useProfileSession", () => ({
  useProfileSession: () => ({ editingProfile: "mako" }),
}));
vi.mock("../../src/hooks/useProfileConfigWriter", () => ({
  useProfileConfigWriter: () => ({ saveConfigField: state.save }),
}));
vi.mock("../../src/components/InfoVisibility", () => ({
  InfoVisibility: pass,
}));
vi.mock("../../src/components/MakoUi", () => ({
  MakoButtonTheme: () => null,
  MakoReleaseIdentity: () => null,
}));
vi.mock("../../src/components/ContentNotices", () => ({
  ContentNotices: ({
    modelStatus,
  }: {
    modelStatus: { dllMissing: boolean };
  }) =>
    modelStatus.dllMissing ? (
      <div role="alert">MAKO Scaler and Shaders are still available</div>
    ) : null,
}));
vi.mock("../../src/components/FeatureSettings", () => ({
  FeatureSettings: ({
    onConfigChange,
  }: {
    onConfigChange: (name: string, value: boolean | string) => void;
  }) => (
    <>
      <button onClick={() => onConfigChange("scaling_enabled", true)}>
        Enable Scaling
      </button>
      <button
        onClick={() => onConfigChange("external_vulkan_layer", "vkbasalt")}
      >
        Enable Shaders
      </button>
    </>
  ),
}));
vi.mock("../../src/components/ProfileManagement", () => ({
  ProfileManagement: () => <div>Profile editor</div>,
}));
vi.mock("../../src/components/PowerProfileControls", () => ({
  PowerProfileControls: () => null,
}));
vi.mock("../../src/components/StatusDisplay", () => ({
  StatusDisplay: () => null,
}));
vi.mock("../../src/components/InstallationButton", () => ({
  InstallationButton: () => null,
}));
vi.mock("../../src/components/ConfigurationSection", () => ({
  ConfigurationSection: () => null,
}));
vi.mock("../../src/components/UsageInstructions", () => ({
  UsageInstructions: () => null,
}));
vi.mock("../../src/components/FgmodClipboardButton", () => ({
  FgmodClipboardButton: () => null,
}));
vi.mock("../../src/components/RuntimeStatusCard", () => ({
  RuntimeStatusCard: () => null,
}));
vi.mock("../../src/components/AdvancedDetailsModal", () => ({
  AdvancedDetailsModal: () => null,
}));
vi.mock("../../src/components/FlatpaksModal", () => ({
  FlatpaksModal: () => null,
}));
vi.mock("../../src/api/makoApi", () => ({ addProfileShader: vi.fn() }));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));
vi.mock("virtual:mako-release-info", () => ({
  currentRelease: { version: "test", codename: "test" },
}));

import { Content } from "../../src/components/Content";
beforeEach(() => {
  window.SP_REACT = React;
});
afterEach(() => {
  cleanup();
  state.installed = true;
  state.save.mockClear();
});

test("missing Lossless Scaling keeps installed Renderer profiles and feature edits accessible", () => {
  render(<Content />);
  expect(screen.getByRole("alert")).toBeTruthy();
  expect(screen.getByText("Profile editor")).toBeTruthy();
  fireEvent.click(screen.getByText("Enable Scaling"));
  fireEvent.click(screen.getByText("Enable Shaders"));
  expect(state.save).toHaveBeenCalledWith("scaling_enabled", true);
  expect(state.save).toHaveBeenCalledWith("external_vulkan_layer", "vkbasalt");
});

test("the profile gate still requires MAKO Renderer itself", () => {
  state.installed = false;
  render(<Content />);
  expect(screen.queryByText("Profile editor")).toBeNull();
  expect(screen.queryByText("Enable Scaling")).toBeNull();
});
