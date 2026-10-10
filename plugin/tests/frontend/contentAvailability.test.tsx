import React from "react";
import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, beforeEach, expect, test, vi } from "vitest";
import { getDefaults } from "../../src/config/configSchema";
import { EMPTY_RUNTIME_SCALING_UI_STATE } from "../../src/utils/runtimeScalingUtils";

const state = vi.hoisted(() => ({
  installed: true,
  updateRequired: false,
  save: vi.fn(),
}));
function pass({ children }: { children: React.ReactNode }) {
  return <div>{children}</div>;
}
vi.mock("@decky/ui", () => ({
  PanelSection: pass,
  PanelSectionRow: pass,
  ButtonItem: pass,
  showModal: vi.fn(),
  Field: ({
    label,
    children,
  }: {
    label: React.ReactNode;
    children: React.ReactNode;
  }) => (
    <div>
      {label}
      {children}
    </div>
  ),
  ToggleField: ({
    label,
    checked,
    disabled,
    onChange,
  }: {
    label: React.ReactNode;
    checked: boolean;
    disabled?: boolean;
    onChange: (value: boolean) => void;
  }) => (
    <button
      aria-pressed={checked}
      disabled={disabled}
      onClick={() => onChange(!checked)}
    >
      {label}
    </button>
  ),
  Dropdown: ({
    rgOptions,
    selectedOption,
    disabled,
    onChange,
  }: {
    rgOptions: { data: string | number; label: React.ReactNode }[];
    selectedOption: string | number;
    disabled?: boolean;
    onChange: (option: {
      data: string | number;
      label: React.ReactNode;
    }) => void;
  }) => (
    <select
      value={selectedOption}
      disabled={disabled}
      onChange={(event) => {
        const option = rgOptions.find(
          (item) => String(item.data) === event.target.value,
        );
        if (option) onChange(option);
      }}
    >
      {rgOptions.map((option) => (
        <option key={option.data} value={option.data}>
          {option.label}
        </option>
      ))}
    </select>
  ),
  SliderField: ({
    label,
    value,
    disabled,
    onChange,
  }: {
    label: string;
    value: number;
    disabled?: boolean;
    onChange: (value: number) => void;
  }) => (
    <input
      type="range"
      aria-label={label}
      value={value}
      disabled={disabled}
      onChange={(event) => onChange(Number(event.target.value))}
    />
  ),
}));
vi.mock("../../src/hooks/useMakoHooks", () => ({
  useInstallationStatus: () => ({
    isInstalled: state.installed,
    engineUpdateRequired: state.updateRequired,
  }),
  useDllDetection: () => ({
    dllDetected: false,
    dllMissing: true,
    dllDetectionStatus: "missing",
  }),
  useMakoConfig: () => ({ config: getDefaults(), isConfigLoading: false }),
  useRuntimeScalingStatus: () => EMPTY_RUNTIME_SCALING_UI_STATE,
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
vi.mock("../../src/components/MakoUi", async (importOriginal) => ({
  ...(await importOriginal<typeof import("../../src/components/MakoUi")>()),
  MakoButtonTheme: () => null,
  MakoReleaseIdentity: () => null,
  MakoFocusable: ({
    children,
    onActivate: _onActivate,
    onGamepadFocus: _onGamepadFocus,
    onGamepadBlur: _onGamepadBlur,
    noFocusRing: _noFocusRing,
    "flow-children": _flowChildren,
    ...props
  }: React.HTMLAttributes<HTMLDivElement> & {
    onActivate?: () => void;
    onGamepadFocus?: () => void;
    onGamepadBlur?: () => void;
    noFocusRing?: boolean;
    "flow-children"?: string;
  }) => <div {...props}>{children}</div>,
}));
vi.mock("../../src/components/ContentNotices", () => ({
  ContentNotices: ({
    modelStatus,
    engineUpdateRequired,
  }: {
    modelStatus: { dllMissing: boolean };
    engineUpdateRequired: boolean;
  }) => (
    <>
      {modelStatus.dllMissing && (
        <div role="alert">MAKO Scaler and Shaders are still available</div>
      )}
      {engineUpdateRequired && <div>MAKO Renderer update required</div>}
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
vi.mock("../../src/components/RemotePlaySection", () => ({
  RemotePlaySection: () => null,
}));
vi.mock("../../src/components/ConfigurationSection", () => ({
  ConfigurationSection: () => null,
  FrameGenerationConfigurationSection: () => null,
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
vi.mock("@decky/api", () => ({
  FileSelectionType: { FILE: 0 },
  openFilePicker: vi.fn(),
}));
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
  state.updateRequired = false;
  state.save.mockClear();
});

test.each([false, true])(
  "installed Renderer keeps all feature edits accessible with update required=%s and missing Lossless Scaling",
  (updateRequired) => {
    state.updateRequired = updateRequired;
    render(<Content />);
    expect(screen.getByRole("alert")).toBeTruthy();
    expect(screen.getByText("Profile editor")).toBeTruthy();
    expect(Boolean(screen.queryByText("MAKO Renderer update required"))).toBe(
      updateRequired,
    );
    const frameGeneration = screen.getByRole("button", {
      name: "Enable Frame-gen (Restart)",
    });
    expect((frameGeneration as HTMLButtonElement).disabled).toBe(false);
    expect(frameGeneration.getAttribute("aria-pressed")).toBe("true");
    expect(state.save).not.toHaveBeenCalled();
    fireEvent.click(frameGeneration);
    expect(state.save).toHaveBeenCalledWith(
      "frame_generation_provisioned",
      false,
    );

    fireEvent.click(screen.getByRole("tab", { name: "Scaling" }));
    const scaling = screen.getByRole("button", {
      name: "Enable Scaling (Restart) Experimental",
    });
    expect((scaling as HTMLButtonElement).disabled).toBe(false);
    fireEvent.click(scaling);
    expect(state.save).toHaveBeenCalledWith("scaling_enabled", true);

    fireEvent.click(screen.getByRole("tab", { name: "Shaders" }));
    const shaders = screen.getByRole("button", {
      name: "Enable Shaders (Restart) Experimental",
    });
    expect((shaders as HTMLButtonElement).disabled).toBe(false);
    fireEvent.click(shaders);
    expect(state.save).toHaveBeenCalledWith(
      "external_vulkan_layer",
      "vkbasalt",
    );
  },
);

test("the profile gate still requires MAKO Renderer itself", () => {
  state.installed = false;
  render(<Content />);
  expect(screen.queryByText("Profile editor")).toBeNull();
  expect(
    screen.queryByRole("tablist", { name: "Image Processing" }),
  ).toBeNull();
  expect(state.save).not.toHaveBeenCalled();
});
