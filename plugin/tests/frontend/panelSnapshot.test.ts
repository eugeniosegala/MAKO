import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";
import type { RemotePlayResult } from "../../src/api/makoApi";
import { getDefaults } from "../../src/config/configSchema";
import {
  forgetPanel,
  panelGeneration,
  rememberPanel,
  resetPanelSnapshotForTests,
  restorePanel,
  type PanelView,
} from "../../src/hooks/panelSnapshot";
import { EMPTY_RUNTIME_SCALING_UI_STATE } from "../../src/utils/runtimeScalingUtils";

const KEY = "mako-panel-snapshot";

const installation = {
  engineUpdateRequired: false,
  installedEngineVersion: "4.6",
  expectedEngineVersion: "4.6",
};

const profiles = {
  profiles: ["mako", "game-123"],
  details: [
    {
      profile_name: "game-123",
      display_name: "Test Game",
      kind: "game",
      steam_app_id: "123",
      processes: ["game.exe"],
    },
  ],
};

const remotePlay: RemotePlayResult = {
  success: true,
  message: "",
  error: null,
  installed: true,
  managed: true,
  available: true,
  running: false,
  frame_generation_active: false,
  profile_name: "mako",
  pids: [],
  conflict: false,
};

function view(profile = "game-123", appId: string | null = "123"): PanelView {
  return {
    installation,
    profiles,
    remotePlay,
    session: {
      profile,
      appId,
      gameRunning: appId !== null,
      remotePlayRunning: false,
    },
    config: {
      profile,
      powerMode: "handheld",
      separatePowerModes: true,
      powerSource: "handheld",
      config: { ...getDefaults(), target_fps: 45 },
      vkBasaltConfigPath: "/tmp/vkBasalt.conf",
      customShaderEffects: [
        { id: "custom/Tone", name: "Tone", path: "/tmp/tone.fx" },
      ],
    },
    runtime: {
      profile,
      state: { ...EMPTY_RUNTIME_SCALING_UI_STATE, hasContext: true },
    },
  };
}

// A fresh plugin load reads storage again.
function reload() {
  resetPanelSnapshotForTests();
}

function stored() {
  return JSON.parse(localStorage.getItem(KEY)!);
}

describe("panel snapshot", () => {
  beforeEach(() => {
    localStorage.clear();
    reload();
  });
  afterEach(() => vi.restoreAllMocks());

  test("restores what was shown for the same running app across plugin loads", () => {
    rememberPanel(view(), panelGeneration());
    reload();
    const restored = restorePanel("123");
    expect(restored?.session?.profile).toBe("game-123");
    expect(restored?.config?.config.target_fps).toBe(45);
    expect(restored?.config?.customShaderEffects).toHaveLength(1);
    expect(restored?.runtime?.state.hasContext).toBe(true);
    expect(restored?.profiles?.details[0].steam_app_id).toBe("123");
    expect(restored?.remotePlay?.managed).toBe(true);
  });

  test("keeps a view for each recent app and for no app", () => {
    rememberPanel(view("game-123", "123"), panelGeneration());
    rememberPanel(view("mako", null), panelGeneration());
    rememberPanel(view("game-456", "456"), panelGeneration());
    reload();
    expect(restorePanel("123")?.session?.profile).toBe("game-123");
    expect(restorePanel(null)?.session?.profile).toBe("mako");
    expect(restorePanel(null)?.runtime).toBeNull();
    expect(restorePanel("456")?.config?.profile).toBe("game-456");
  });

  test("an app never shown keeps only the shared parts", () => {
    rememberPanel(view(), panelGeneration());
    const restored = restorePanel("789");
    expect(restored?.installation.installedEngineVersion).toBe("4.6");
    expect(restored?.profiles?.profiles).toEqual(["mako", "game-123"]);
    expect(restored?.remotePlay?.installed).toBe(true);
    expect(restored?.session).toBeNull();
    expect(restored?.config).toBeNull();
    expect(restored?.runtime).toBeNull();
  });

  test("keeps only the most recent apps", () => {
    for (let app = 0; app < 10; app += 1)
      rememberPanel(view(`game-${app}`, String(app)), panelGeneration());
    expect(stored().apps).toHaveLength(8);
    expect(restorePanel("0")?.session).toBeNull();
    expect(restorePanel("1")?.session).toBeNull();
    expect(restorePanel("2")?.session?.profile).toBe("game-2");
    // Showing an app again makes it the most recent.
    rememberPanel(view("game-2", "2"), panelGeneration());
    rememberPanel(view("game-10", "10"), panelGeneration());
    expect(restorePanel("2")?.session?.profile).toBe("game-2");
    expect(restorePanel("3")?.session).toBeNull();
  });

  test("never pairs settings or live status with a different profile", () => {
    rememberPanel(view(), panelGeneration());
    rememberPanel(
      {
        installation,
        session: { ...view().session!, profile: "other" },
      },
      panelGeneration(),
    );
    const restored = restorePanel("123");
    expect(restored?.session?.profile).toBe("other");
    expect(restored?.config).toBeNull();
    expect(restored?.runtime).toBeNull();
  });

  test("an unknown part keeps the remembered one and null forgets it", () => {
    rememberPanel(view(), panelGeneration());
    rememberPanel(
      {
        installation,
        session: view().session,
        config: undefined,
        runtime: null,
      },
      panelGeneration(),
    );
    const restored = restorePanel("123");
    expect(restored?.session?.profile).toBe("game-123");
    expect(restored?.config?.profile).toBe("game-123");
    expect(restored?.runtime).toBeNull();
    expect(restored?.profiles?.profiles).toHaveLength(2);

    rememberPanel({ installation, profiles: null }, panelGeneration());
    expect(restorePanel("123")?.profiles).toBeNull();
    expect(restorePanel("123")?.session?.profile).toBe("game-123");
  });

  test("settings and live status without a session are not recorded", () => {
    rememberPanel(
      { installation, config: view().config, runtime: view().runtime },
      panelGeneration(),
    );
    expect(stored().apps).toEqual([]);
  });

  test("a check that started before an installation change cannot write", () => {
    const before = panelGeneration();
    forgetPanel();
    rememberPanel(view(), before);
    expect(restorePanel("123")).toBeNull();
    expect(localStorage.getItem(KEY)).toBeNull();
    rememberPanel(view(), panelGeneration());
    expect(restorePanel("123")?.session?.profile).toBe("game-123");
  });

  test("forgetting clears memory and storage", () => {
    rememberPanel(view(), panelGeneration());
    rememberPanel(view("mako", null), panelGeneration());
    forgetPanel();
    expect(restorePanel("123")).toBeNull();
    expect(restorePanel(null)).toBeNull();
    reload();
    expect(restorePanel("123")).toBeNull();
  });

  test("a verified panel without the Renderer clears what an earlier load stored", () => {
    rememberPanel(view(), panelGeneration());
    reload();
    rememberPanel(null, panelGeneration());
    expect(localStorage.getItem(KEY)).toBeNull();
    expect(restorePanel("123")).toBeNull();
  });

  test("a snapshot from another plugin build or schema is discarded", () => {
    rememberPanel(view(), panelGeneration());
    const record = stored();
    for (const changed of [
      { ...record, build: JSON.stringify(["old", null]) },
      { ...record, schema: 1 },
    ]) {
      localStorage.setItem(KEY, JSON.stringify(changed));
      reload();
      expect(restorePanel("123")).toBeNull();
      expect(localStorage.getItem(KEY)).toBeNull();
    }
  });

  test("malformed parts are dropped without discarding valid ones", () => {
    rememberPanel(view(), panelGeneration());
    rememberPanel(view("mako", null), panelGeneration());
    const record = stored();
    const [idle, game] = record.apps;
    localStorage.setItem(
      KEY,
      JSON.stringify({
        ...record,
        profiles: { profiles: ["mako"], details: [{ profile_name: 1 }] },
        remotePlay: { ...remotePlay, managed: "yes" },
        apps: [
          { ...idle, session: { ...idle.session, appId: 5 } },
          {
            ...game,
            config: { ...game.config, customShaderEffects: [{ id: 1 }] },
            runtime: "broken",
          },
        ],
      }),
    );
    reload();
    const restored = restorePanel("123");
    expect(restored?.profiles).toBeNull();
    expect(restored?.remotePlay).toBeNull();
    expect(restored?.session?.profile).toBe("game-123");
    expect(restored?.config).toBeNull();
    expect(restored?.runtime).toBeNull();
    expect(restorePanel(null)?.session).toBeNull();

    localStorage.setItem(KEY, "{not json");
    reload();
    expect(restorePanel("123")).toBeNull();
  });

  test("restored settings fill fields a newer build added", () => {
    rememberPanel(view(), panelGeneration());
    const record = stored();
    delete record.apps[0].config.config.target_fps;
    localStorage.setItem(KEY, JSON.stringify(record));
    reload();
    expect(restorePanel("123")?.config?.config.target_fps).toBe(
      getDefaults().target_fps,
    );
  });

  test("works in memory when storage is unavailable", () => {
    vi.spyOn(Storage.prototype, "getItem").mockImplementation(() => {
      throw new Error("blocked");
    });
    vi.spyOn(Storage.prototype, "setItem").mockImplementation(() => {
      throw new Error("blocked");
    });
    vi.spyOn(Storage.prototype, "removeItem").mockImplementation(() => {
      throw new Error("blocked");
    });
    expect(restorePanel("123")).toBeNull();
    rememberPanel(view(), panelGeneration());
    expect(restorePanel("123")?.session?.profile).toBe("game-123");
    forgetPanel();
    expect(restorePanel("123")).toBeNull();
  });
});
