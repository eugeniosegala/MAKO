import React from "react";
import {
  act,
  cleanup,
  fireEvent,
  render,
  screen,
} from "@testing-library/react";
import { afterEach, beforeEach, expect, test, vi } from "vitest";
import { getDefaults } from "../../src/config/configSchema";

const mocks = vi.hoisted(() => ({
  router: {
    MainRunningApp: undefined as
      { appid: number; display_name: string } | undefined,
  },
  installed: true,
  checkGate: null as Promise<void> | null,
  loadGate: null as Promise<void> | null,
  checkMakoInstalled: vi.fn(),
  getMakoConfig: vi.fn(),
  getProfileConfig: vi.fn(),
  syncCurrentProfile: vi.fn(),
  handleInstall: vi.fn(),
  handleUninstall: vi.fn(),
}));
function pass({ children }: { children: React.ReactNode }) {
  return <div>{children}</div>;
}
function empty() {
  return null;
}
vi.mock("@decky/ui", () => ({
  Router: mocks.router,
  Navigation: {},
  QuickAccessTab: {},
  PanelSection: pass,
  PanelSectionRow: pass,
  ButtonItem: pass,
  showModal: vi.fn(),
}));
vi.mock("../../src/api/makoApi", () => ({
  checkMakoInstalled: mocks.checkMakoInstalled,
  getMakoConfig: mocks.getMakoConfig,
  getProfileConfig: mocks.getProfileConfig,
}));
vi.mock("../../src/hooks/useMakoHooks", async (importOriginal) => ({
  ...(await importOriginal<typeof import("../../src/hooks/useMakoHooks")>()),
  useDllDetection: () => ({}),
  useRuntimeScalingStatus: () => EMPTY_RUNTIME_SCALING_UI_STATE,
}));
vi.mock("../../src/hooks/useProfileManagement", () => ({
  useProfileManagement: () => ({
    updateProfileConfigFields: vi.fn(),
    syncCurrentProfile: mocks.syncCurrentProfile,
    setCurrentProfile: vi.fn(),
  }),
}));
vi.mock("../../src/hooks/useModelStatus", () => ({
  useModelStatus: () => ({}),
}));
vi.mock("../../src/hooks/useInstallationActions", () => ({
  useInstallationActions: () => ({
    isInstalling: false,
    isUninstalling: false,
    isInstallCompletionVisible: false,
    handleInstall: mocks.handleInstall,
    handleUninstall: mocks.handleUninstall,
  }),
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
  ContentNotices: ({ showWelcome }: { showWelcome: boolean }) =>
    showWelcome ? <div>Welcome notice</div> : null,
}));
vi.mock("../../src/components/ProfileManagement", () => ({
  ProfileManagement: ({
    editingProfile,
    restoredProfiles,
    onProfilesLoaded,
  }: {
    editingProfile: string;
    restoredProfiles?: { profiles: string[] } | null;
    onProfilesLoaded?: (profiles: {
      profiles: string[];
      details: never[];
    }) => void;
  }) => {
    React.useEffect(() => {
      onProfilesLoaded?.({ profiles: ["mako", "game-123"], details: [] });
    }, []);
    return (
      <div>
        Editing {editingProfile}
        <div>
          Known profiles {restoredProfiles?.profiles.join(",") ?? "none"}
        </div>
      </div>
    );
  },
}));
vi.mock("../../src/components/StatusDisplay", () => ({
  StatusDisplay: ({ installationStatus }: { installationStatus: string }) => (
    <div>Status: {installationStatus}</div>
  ),
}));
vi.mock("../../src/components/InstallationButton", () => ({
  InstallationButton: ({
    isInstalled,
    onInstall,
    onUninstall,
  }: {
    isInstalled: boolean;
    onInstall: () => void;
    onUninstall: () => void;
  }) => (
    <button onClick={isInstalled ? onUninstall : onInstall}>
      {isInstalled ? "Remove Renderer" : "Install Renderer"}
    </button>
  ),
}));
vi.mock("../../src/components/PowerProfileControls", () => ({
  PowerProfileControls: empty,
}));
vi.mock("../../src/components/RuntimeStatusCard", () => ({
  RuntimeStatusCard: empty,
}));
vi.mock("../../src/components/FeatureSettings", () => ({
  FeatureSettings: ({ config }: { config: ReturnType<typeof getDefaults> }) => (
    <div>Target {config.target_fps}</div>
  ),
}));
vi.mock("../../src/components/RemotePlaySection", () => ({
  RemotePlaySection: ({
    restoredStatus,
    onStatus,
  }: {
    restoredStatus?: { managed: boolean } | null;
    onStatus?: (status: { managed: boolean }) => void;
  }) => {
    React.useEffect(() => onStatus?.({ managed: true }), []);
    return (
      <div>
        Remote Play{" "}
        {restoredStatus ? `managed=${restoredStatus.managed}` : "checking"}
      </div>
    );
  },
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
vi.mock("../../src/utils/toastUtils", () => ({
  showErrorToast: vi.fn(),
  ToastMessages: {},
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));

import { Content } from "../../src/components/Content";
import {
  panelGeneration,
  rememberPanel,
  resetPanelSnapshotForTests,
  restorePanel,
} from "../../src/hooks/panelSnapshot";
import { EMPTY_RUNTIME_SCALING_UI_STATE } from "../../src/utils/runtimeScalingUtils";

function gate() {
  let open!: () => void;
  const promise = new Promise<void>((resolve) => {
    open = resolve;
  });
  return { promise, open };
}

// Opens the panel and lets every backend read answer.
async function openSettled() {
  const view = render(<Content />);
  await act(async () => {});
  await act(async () => {});
  return view;
}

beforeEach(() => {
  window.SP_REACT = React;
  localStorage.clear();
  resetPanelSnapshotForTests();
  mocks.router.MainRunningApp = { appid: 123, display_name: "Test Game" };
  mocks.installed = true;
  mocks.checkGate = null;
  mocks.loadGate = null;
  mocks.checkMakoInstalled.mockImplementation(async () => {
    if (mocks.checkGate) await mocks.checkGate;
    return {
      installed: mocks.installed,
      engine_update_required: false,
      installed_engine_version: "4.6",
      expected_engine_version: "4.6",
    };
  });
  mocks.getMakoConfig.mockResolvedValue({
    success: true,
    config: getDefaults(),
  });
  mocks.getProfileConfig.mockImplementation(async (profile: string) => {
    if (mocks.loadGate) await mocks.loadGate;
    return {
      success: true,
      config: { ...getDefaults(), target_fps: profile === "mako" ? 60 : 45 },
      power_mode: "shared",
      separate_power_modes: false,
      power_source: "handheld",
    };
  });
  mocks.syncCurrentProfile.mockResolvedValue({
    success: true,
    game_running: true,
    profile_name: "game-123",
  });
  mocks.handleInstall.mockImplementation(
    async (setIsInstalled: (value: boolean) => void) => setIsInstalled(true),
  );
  mocks.handleUninstall.mockImplementation(
    async (setIsInstalled: (value: boolean) => void) => setIsInstalled(false),
  );
});
afterEach(() => {
  cleanup();
  vi.clearAllMocks();
});

test("a reopened panel starts from the layout it last showed", async () => {
  // A first open shows the empty layout until the backend answers.
  mocks.checkGate = gate().promise;
  const first = render(<Content />);
  expect(screen.getByText("Install Renderer")).toBeTruthy();
  first.unmount();

  mocks.checkGate = null;
  (await openSettled()).unmount();
  expect(restorePanel("123")?.config?.config.target_fps).toBe(45);

  const check = gate();
  const load = gate();
  mocks.checkGate = check.promise;
  mocks.loadGate = load.promise;
  render(<Content />);
  expect(screen.getByText("Welcome notice")).toBeTruthy();
  expect(screen.getByText("Editing game-123")).toBeTruthy();
  expect(screen.getByText("Target 45")).toBeTruthy();
  expect(screen.queryByText("Install Renderer")).toBeNull();
  expect(screen.queryByText("Loading profile settings...")).toBeNull();

  await act(async () => {
    check.open();
    load.open();
  });
  expect(screen.getByText("Target 45")).toBeTruthy();
  expect(screen.queryByText("Loading profile settings...")).toBeNull();
});

test("installation actions wait for the real check", async () => {
  (await openSettled()).unmount();
  const check = gate();
  mocks.checkGate = check.promise;
  render(<Content />);
  fireEvent.click(screen.getByText("Remove Renderer"));
  expect(mocks.handleUninstall).not.toHaveBeenCalled();

  await act(async () => check.open());
  fireEvent.click(screen.getByText("Remove Renderer"));
  expect(mocks.handleUninstall).toHaveBeenCalledOnce();
});

test("removing the Renderer forgets the panel", async () => {
  const view = await openSettled();
  expect(restorePanel("123")).not.toBeNull();
  await act(async () => {
    fireEvent.click(screen.getByText("Remove Renderer"));
  });
  expect(screen.getByText("Install Renderer")).toBeTruthy();
  expect(restorePanel("123")).toBeNull();
  view.unmount();

  mocks.installed = false;
  mocks.checkGate = gate().promise;
  render(<Content />);
  expect(screen.getByText("Install Renderer")).toBeTruthy();
  expect(screen.queryByText("Welcome notice")).toBeNull();
});

test("installing forgets the old panel and remembers the new one", async () => {
  mocks.installed = false;
  const view = await openSettled();
  mocks.installed = true;
  const before = panelGeneration();
  await act(async () => {
    fireEvent.click(screen.getByText("Install Renderer"));
  });
  await act(async () => {});
  expect(mocks.handleInstall).toHaveBeenCalledOnce();
  expect(panelGeneration()).toBe(before + 2);
  expect(restorePanel("123")?.installation.installedEngineVersion).toBe("4.6");
  view.unmount();
});

test("a stale snapshot gives way when the Renderer is gone", async () => {
  rememberPanel(
    {
      installation: {
        engineUpdateRequired: false,
        installedEngineVersion: "4.6",
        expectedEngineVersion: "4.6",
      },
      session: {
        profile: "game-123",
        appId: "123",
        gameRunning: true,
        remotePlayRunning: false,
      },
      config: null,
      runtime: null,
    },
    panelGeneration(),
  );
  mocks.installed = false;
  const check = gate();
  mocks.checkGate = check.promise;
  render(<Content />);
  expect(screen.getByText("Remove Renderer")).toBeTruthy();
  await act(async () => check.open());
  await act(async () => {});
  expect(screen.getByText("Install Renderer")).toBeTruthy();
  expect(screen.getByText("Status: MAKO Renderer not installed")).toBeTruthy();
  expect(restorePanel("123")).toBeNull();
});

test("a different running game starts from the installation alone", async () => {
  (await openSettled()).unmount();
  mocks.router.MainRunningApp = { appid: 456, display_name: "Other Game" };
  mocks.checkGate = gate().promise;
  render(<Content />);
  expect(screen.getByText("Remove Renderer")).toBeTruthy();
  expect(screen.getByText("Editing mako")).toBeTruthy();
  expect(screen.queryByText("Target 45")).toBeNull();
});

test("restored installation without settings shows the loading row, not defaults", async () => {
  (await openSettled()).unmount();
  mocks.router.MainRunningApp = { appid: 456, display_name: "Other Game" };
  mocks.checkGate = gate().promise;
  render(<Content />);
  expect(screen.getByText("Loading profile settings...")).toBeTruthy();
  expect(
    screen.getByText(`Target ${getDefaults().target_fps}`).closest("[hidden]"),
  ).not.toBeNull();
});

test("a stale snapshot's profile gives way to the global view without the Renderer", async () => {
  (await openSettled()).unmount();
  mocks.getMakoConfig.mockClear();
  mocks.installed = false;
  await openSettled();
  expect(mocks.getMakoConfig).toHaveBeenCalledOnce();
  expect(screen.getByText("Install Renderer")).toBeTruthy();
});

test("removing the Renderer does not reload the settings", async () => {
  await openSettled();
  mocks.getMakoConfig.mockClear();
  await act(async () => {
    fireEvent.click(screen.getByText("Remove Renderer"));
  });
  await act(async () => {});
  expect(mocks.getMakoConfig).not.toHaveBeenCalled();
});

test("each game and Steam's menus reopen on their own view", async () => {
  (await openSettled()).unmount();

  // Back in Steam's menus the editor follows the idle selection.
  mocks.router.MainRunningApp = undefined;
  mocks.syncCurrentProfile.mockResolvedValue({
    success: true,
    game_running: false,
    profile_name: "mako",
  });
  (await openSettled()).unmount();

  for (const [app, profile, target] of [
    [123, "game-123", 45],
    [undefined, "mako", 60],
  ] as const) {
    mocks.router.MainRunningApp = app
      ? { appid: app, display_name: "Test Game" }
      : undefined;
    mocks.checkGate = gate().promise;
    mocks.loadGate = gate().promise;
    const view = render(<Content />);
    expect(screen.getByText(`Editing ${profile}`)).toBeTruthy();
    expect(screen.getByText(`Target ${target}`)).toBeTruthy();
    expect(screen.queryByText("Loading profile settings...")).toBeNull();
    expect(screen.getByText("Known profiles mako,game-123")).toBeTruthy();
    expect(screen.getByText("Remote Play managed=true")).toBeTruthy();
    view.unmount();
  }
});
