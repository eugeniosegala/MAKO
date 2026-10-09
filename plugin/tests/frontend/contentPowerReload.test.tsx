import React from "react";
import {
  act,
  cleanup,
  fireEvent,
  render,
  screen,
} from "@testing-library/react";
import { afterEach, beforeEach, expect, test, vi } from "vitest";
import {
  getDefaults,
  type ConfigurationPatch,
} from "../../src/config/configSchema";

const mocks = vi.hoisted(() => ({
  powerSource: "handheld",
  sharpness: 0.5,
  targets: { handheld: 60, docked: 144 },
  featuresOffModes: [] as string[],
  loadGate: null as Promise<void> | null,
  saveGate: null as Promise<void> | null,
  getProfileConfig: vi.fn(),
  updateProfileConfigFields: vi.fn(),
  syncCurrentProfile: vi.fn(),
  setCurrentProfile: vi.fn(),
  deleteProfileShaders: vi.fn(),
  reloadProfileShaders: vi.fn(),
  createProfile: vi.fn(),
  getProfiles: vi.fn(),
}));
function pass({ children }: { children: React.ReactNode }) {
  return <div>{children}</div>;
}
function empty() {
  return null;
}
vi.mock("@decky/ui", () => ({
  Router: { MainRunningApp: { appid: 123, display_name: "Test Game" } },
  PanelSection: pass,
  PanelSectionRow: pass,
  ButtonItem: pass,
  showModal: vi.fn(),
}));
vi.mock("../../src/api/makoApi", () => ({
  getMakoConfig: async () => ({ success: true, config: getDefaults() }),
  getProfileConfig: mocks.getProfileConfig,
  deleteProfileShaders: mocks.deleteProfileShaders,
  reloadProfileShaders: mocks.reloadProfileShaders,
  createProfile: mocks.createProfile,
  getProfiles: mocks.getProfiles,
}));
vi.mock("../../src/hooks/useMakoHooks", async (importOriginal) => ({
  ...(await importOriginal<typeof import("../../src/hooks/useMakoHooks")>()),
  useInstallationStatus: () => ({
    isInstalled: true,
    engineUpdateRequired: false,
  }),
  useDllDetection: () => ({}),
  useRuntimeScalingStatus: () => EMPTY_RUNTIME_SCALING_UI_STATE,
}));
vi.mock("../../src/hooks/useProfileManagement", () => ({
  useProfileManagement: () => ({
    updateProfileConfigFields: mocks.updateProfileConfigFields,
    syncCurrentProfile: mocks.syncCurrentProfile,
    setCurrentProfile: mocks.setCurrentProfile,
  }),
}));
vi.mock("../../src/hooks/useModelStatus", () => ({
  useModelStatus: () => ({}),
}));
vi.mock("../../src/hooks/useInstallationActions", () => ({
  useInstallationActions: () => ({}),
}));
vi.mock("../../src/components/InfoVisibility", () => ({
  InfoVisibility: pass,
}));
vi.mock("../../src/components/MakoUi", async (importOriginal) => ({
  ...(await importOriginal<typeof import("../../src/components/MakoUi")>()),
  MakoButtonTheme: empty,
  MakoReleaseIdentity: empty,
}));
vi.mock("../../src/components/ContentNotices", () => ({
  ContentNotices: empty,
}));
vi.mock("../../src/components/ProfileManagement", async () => {
  const { useProfileEditorModel } =
    await import("../../src/hooks/useProfileEditorModel");
  return {
    ProfileManagement: ({
      onProfileChange,
      onBeforeProfileMutation,
      editingProfile,
    }: {
      onProfileChange: (name: string) => Promise<void>;
      onBeforeProfileMutation: () => Promise<void>;
      editingProfile: string;
    }) => {
      const { createSelectedProfile } = useProfileEditorModel({
        editingProfile,
        onProfileChange,
        onBeforeProfileMutation,
      });
      return (
        <>
          <button onClick={() => void onProfileChange("Streaming")}>
            Select Streaming
          </button>
          <button onClick={() => void createSelectedProfile("Streaming")}>
            Create Streaming
          </button>
        </>
      );
    },
  };
});
vi.mock("../../src/components/StatusDisplay", () => ({ StatusDisplay: empty }));
vi.mock("../../src/components/InstallationButton", () => ({
  InstallationButton: empty,
}));
vi.mock("../../src/components/RemotePlaySection", () => ({
  RemotePlaySection: () => null,
}));
vi.mock("../../src/components/ConfigurationSection", () => ({
  ConfigurationSection: empty,
}));
vi.mock("../../src/components/UsageInstructions", () => ({
  UsageInstructions: empty,
}));
vi.mock("../../src/components/FgmodClipboardButton", () => ({
  FgmodClipboardButton: empty,
}));
vi.mock("../../src/components/AdvancedDetailsModal", () => ({
  AdvancedDetailsModal: empty,
}));
vi.mock("../../src/components/FlatpaksModal", () => ({ FlatpaksModal: empty }));
vi.mock("../../src/components/PowerProfileControls", () => ({
  PowerProfileControls: ({
    powerMode,
    loadProfileConfig,
  }: {
    powerMode: string;
    loadProfileConfig: (profile: string, mode: string) => Promise<void>;
  }) => (
    <select
      aria-label="Power set"
      value={powerMode}
      onChange={(event) => void loadProfileConfig("mako", event.target.value)}
    >
      <option value="handheld">Battery</option>
      <option value="docked">AC</option>
    </select>
  ),
}));
vi.mock("../../src/components/FeatureSettings", () => ({
  FeatureSettings: ({
    config,
    onConfigChange,
    onDeleteShaders,
    onRefreshShaders,
  }: {
    config: ReturnType<typeof getDefaults>;
    onConfigChange: (field: string, value: number) => void;
    onDeleteShaders: (ids: string[]) => Promise<void>;
    onRefreshShaders: () => Promise<void>;
  }) => (
    <>
      <input
        aria-label="Shared shader sharpness"
        value={config.vkbasalt_sharpness}
        onChange={(event) =>
          onConfigChange("vkbasalt_sharpness", Number(event.target.value))
        }
      />
      <button onClick={() => void onRefreshShaders()}>Refresh shaders</button>
      <span>Native target: {config.target_fps}</span>
      <button onClick={() => void onDeleteShaders(["custom/Tone"])}>
        Delete custom
      </button>
    </>
  ),
}));
vi.mock("../../src/utils/toastUtils", () => ({
  showErrorToast: vi.fn(),
  ToastMessages: {},
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));

import { Content } from "../../src/components/Content";
import { EMPTY_RUNTIME_SCALING_UI_STATE } from "../../src/utils/runtimeScalingUtils";

function configForMode(mode: string) {
  return {
    ...getDefaults(),
    target_fps: mocks.targets[mode as keyof typeof mocks.targets] ?? 90,
    vkbasalt_sharpness: mocks.sharpness,
    frame_generation_provisioned: !mocks.featuresOffModes.includes(mode),
    scaling_enabled: false,
  };
}

beforeEach(() => {
  vi.useFakeTimers();
  window.SP_REACT = React;
  mocks.powerSource = "handheld";
  mocks.sharpness = 0.5;
  mocks.saveGate = null;
  mocks.featuresOffModes = [];
  mocks.loadGate = null;
  mocks.reloadProfileShaders.mockResolvedValue({ success: true });
  mocks.deleteProfileShaders.mockResolvedValue({ success: true });
  mocks.setCurrentProfile.mockResolvedValue({ success: true });
  mocks.getProfiles.mockResolvedValue({
    success: true,
    profiles: ["mako", "Streaming"],
    current_profile: "mako",
  });
  mocks.createProfile.mockResolvedValue({
    success: true,
    profile_name: "Streaming",
  });
  mocks.getProfileConfig.mockImplementation(
    async (_name: string, mode?: string) => {
      if (mocks.loadGate) await mocks.loadGate;
      return {
        success: true,
        config: configForMode(mode || mocks.powerSource),
        power_mode: mode || mocks.powerSource,
        separate_power_modes: true,
        power_source: mocks.powerSource,
      };
    },
  );
  mocks.updateProfileConfigFields.mockImplementation(
    async (_name: string, changes: ConfigurationPatch, mode: string) => {
      if (mocks.saveGate) await mocks.saveGate;
      if (changes.vkbasalt_sharpness !== undefined)
        mocks.sharpness = changes.vkbasalt_sharpness;
      return {
        success: true,
        config: configForMode(mode),
        message: "saved",
        error: null,
      };
    },
  );
  mocks.syncCurrentProfile.mockImplementation(async () => ({
    success: true,
    game_running: true,
    profile_name: "mako",
    power_source: mocks.powerSource,
  }));
});
afterEach(() => {
  cleanup();
  vi.useRealTimers();
});

test("editing an inactive power set cannot hide missing telemetry for the running game", async () => {
  mocks.featuresOffModes = ["docked"];
  render(<Content />);
  await act(async () => {});
  expect(screen.getByText("Waiting for MAKO")).toBeTruthy();

  fireEvent.change(screen.getByLabelText("Power set"), {
    target: { value: "docked" },
  });
  await act(async () => {});
  expect(screen.getByText("Waiting for MAKO")).toBeTruthy();
  expect(
    screen.queryByText("Frame Generation and Scaling are off in this profile."),
  ).toBeNull();

  mocks.powerSource = "docked";
  await act(async () => {
    await vi.advanceTimersByTimeAsync(2000);
  });
  expect(
    screen.getByText("Frame Generation and Scaling are off in this profile."),
  ).toBeTruthy();
});

test("stale disabled settings cannot replace missing telemetry during a profile load", async () => {
  mocks.featuresOffModes = ["handheld"];
  render(<Content />);
  await act(async () => {});
  expect(
    screen.getByText("Frame Generation and Scaling are off in this profile."),
  ).toBeTruthy();

  let finishLoad!: () => void;
  mocks.loadGate = new Promise<void>((resolve) => {
    finishLoad = resolve;
  });
  fireEvent.change(screen.getByLabelText("Power set"), {
    target: { value: "docked" },
  });
  await act(async () => {});
  expect(screen.getByText("Waiting for MAKO")).toBeTruthy();
  expect(
    screen.queryByText("Frame Generation and Scaling are off in this profile."),
  ).toBeNull();
  await act(async () => {
    finishLoad();
  });
});

test("profile selection drains queued edits before persisting the stream selection", async () => {
  render(<Content />);
  await act(async () => {});
  let finishSave!: () => void;
  mocks.saveGate = new Promise<void>((resolve) => {
    finishSave = resolve;
  });
  fireEvent.change(screen.getByLabelText("Shared shader sharpness"), {
    target: { value: "0.73" },
  });
  fireEvent.click(screen.getByText("Select Streaming"));
  await act(async () => {});
  expect(mocks.setCurrentProfile).not.toHaveBeenCalled();
  await act(async () => {
    finishSave();
  });
  expect(mocks.updateProfileConfigFields).toHaveBeenCalledWith(
    "mako",
    { vkbasalt_sharpness: 0.73 },
    "handheld",
  );
  expect(mocks.setCurrentProfile).toHaveBeenCalledWith("Streaming");
  expect(mocks.getProfileConfig).toHaveBeenLastCalledWith("Streaming");
});

test("new profiles copy settings only after the real save queue finishes", async () => {
  render(<Content />);
  await act(async () => {});
  let finishSave!: () => void;
  mocks.saveGate = new Promise<void>((resolve) => {
    finishSave = resolve;
  });
  let copiedSharpness = 0;
  mocks.createProfile.mockImplementation(async () => {
    copiedSharpness = mocks.sharpness;
    return { success: true, profile_name: "Streaming" };
  });
  fireEvent.change(screen.getByLabelText("Shared shader sharpness"), {
    target: { value: "0.73" },
  });
  fireEvent.click(screen.getByText("Create Streaming"));
  await act(async () => {});
  expect(mocks.createProfile).not.toHaveBeenCalled();
  await act(async () => {
    finishSave();
  });
  expect(mocks.createProfile).toHaveBeenCalledWith("Streaming", "mako");
  expect(copiedSharpness).toBe(0.73);
  expect(mocks.setCurrentProfile).toHaveBeenCalledWith("Streaming");
});

test("creating a profile in a stream redirects later edits and retains selection after exit", async () => {
  let selected = "mako";
  let streaming = true;
  mocks.syncCurrentProfile.mockImplementation(async () => ({
    success: true,
    profile_name: selected,
    game_running: streaming,
    remote_play_running: streaming,
    power_source: mocks.powerSource,
  }));
  mocks.setCurrentProfile.mockImplementation(async (profile: string) => {
    selected = profile;
    return { success: true };
  });
  render(<Content />);
  await act(async () => {});
  fireEvent.change(screen.getByLabelText("Shared shader sharpness"), {
    target: { value: "0.73" },
  });
  fireEvent.click(screen.getByText("Create Streaming"));
  await act(async () => {});
  expect(mocks.createProfile).toHaveBeenCalledWith("Streaming", "mako");
  expect(selected).toBe("Streaming");
  await act(async () => {
    await vi.advanceTimersByTimeAsync(2000);
  });
  fireEvent.change(screen.getByLabelText("Shared shader sharpness"), {
    target: { value: "0.91" },
  });
  await act(async () => {
    await vi.advanceTimersByTimeAsync(1000);
  });
  expect(mocks.updateProfileConfigFields).toHaveBeenLastCalledWith(
    "Streaming",
    { vkbasalt_sharpness: 0.91 },
    "handheld",
  );
  streaming = false;
  await act(async () => {
    await vi.advanceTimersByTimeAsync(2000);
  });
  expect(mocks.getProfileConfig).toHaveBeenLastCalledWith("Streaming");
  expect(selected).toBe("Streaming");
});

test("shader refresh drains saves before requesting a rebuild and preserves the editing power set", async () => {
  render(<Content />);
  await act(async () => {});
  let finishSave!: () => void;
  mocks.saveGate = new Promise<void>((resolve) => {
    finishSave = resolve;
  });
  fireEvent.change(screen.getByLabelText("Shared shader sharpness"), {
    target: { value: "0.73" },
  });
  fireEvent.click(screen.getByText("Refresh shaders"));
  await act(async () => {});
  expect(mocks.reloadProfileShaders).not.toHaveBeenCalled();
  await act(async () => {
    finishSave();
  });
  expect(mocks.reloadProfileShaders).toHaveBeenCalledWith("mako");
  expect(mocks.getProfileConfig).toHaveBeenLastCalledWith("mako", "handheld");
  expect(screen.getByLabelText("Shared shader sharpness")).toHaveProperty(
    "value",
    "0.73",
  );
});

test("custom shader deletion waits for queued settings and reloads the selected power set", async () => {
  render(<Content />);
  await act(async () => {});
  let finishSave!: () => void;
  mocks.saveGate = new Promise<void>((resolve) => {
    finishSave = resolve;
  });
  fireEvent.change(screen.getByLabelText("Shared shader sharpness"), {
    target: { value: "0.73" },
  });
  fireEvent.click(screen.getByText("Delete custom"));
  await act(async () => {});
  expect(mocks.deleteProfileShaders).not.toHaveBeenCalled();
  expect(mocks.updateProfileConfigFields).toHaveBeenCalledWith(
    "mako",
    { vkbasalt_sharpness: 0.73 },
    "handheld",
  );
  await act(async () => {
    finishSave();
  });
  expect(mocks.deleteProfileShaders).toHaveBeenCalledWith("mako", [
    "custom/Tone",
  ]);
  expect(mocks.getProfileConfig).toHaveBeenLastCalledWith("mako", "handheld");
  expect(
    (screen.getByLabelText("Shared shader sharpness") as HTMLInputElement)
      .value,
  ).toBe("0.73");
});

test("manual power-set reload drains shared edits without copying native settings", async () => {
  render(<Content />);
  await act(async () => {});
  expect((screen.getByLabelText("Power set") as HTMLSelectElement).value).toBe(
    "handheld",
  );
  fireEvent.change(screen.getByLabelText("Shared shader sharpness"), {
    target: { value: "0.73" },
  });
  fireEvent.change(screen.getByLabelText("Power set"), {
    target: { value: "docked" },
  });
  await act(async () => {});
  expect(mocks.updateProfileConfigFields).toHaveBeenCalledWith(
    "mako",
    { vkbasalt_sharpness: 0.73 },
    "handheld",
  );
  expect(mocks.getProfileConfig).toHaveBeenLastCalledWith("mako", "docked");
  expect(
    (screen.getByLabelText("Shared shader sharpness") as HTMLInputElement)
      .value,
  ).toBe("0.73");
  expect(screen.getByText("Native target: 144")).toBeTruthy();
});

test("automatic power-source reload waits for an in-flight shared save", async () => {
  render(<Content />);
  await act(async () => {});
  let finishSave!: () => void;
  mocks.saveGate = new Promise<void>((resolve) => {
    finishSave = resolve;
  });
  fireEvent.change(screen.getByLabelText("Shared shader sharpness"), {
    target: { value: "0.91" },
  });
  mocks.getProfileConfig.mockClear();
  mocks.powerSource = "docked";
  await act(async () => {
    await vi.advanceTimersByTimeAsync(2000);
  });
  expect(mocks.updateProfileConfigFields).toHaveBeenCalledOnce();
  expect(mocks.getProfileConfig).not.toHaveBeenCalled();
  expect(screen.getByText("Loading profile settings...")).toBeTruthy();
  await act(async () => {
    finishSave();
  });
  expect((screen.getByLabelText("Power set") as HTMLSelectElement).value).toBe(
    "docked",
  );
  expect(
    (screen.getByLabelText("Shared shader sharpness") as HTMLInputElement)
      .value,
  ).toBe("0.91");
  expect(screen.getByText("Native target: 144")).toBeTruthy();
});

test("a failed write reconciles directly without waiting on its own queue", async () => {
  render(<Content />);
  await act(async () => {});
  mocks.updateProfileConfigFields.mockResolvedValueOnce({
    success: false,
    config: null,
  });
  fireEvent.change(screen.getByLabelText("Shared shader sharpness"), {
    target: { value: "0.73" },
  });
  await act(async () => {
    await vi.advanceTimersByTimeAsync(250);
  });
  expect(
    (screen.getByLabelText("Shared shader sharpness") as HTMLInputElement)
      .value,
  ).toBe("0.5");
  expect(screen.queryByText("Loading profile settings...")).toBeNull();
});
