import React from "react";
import {
  cleanup,
  fireEvent,
  render,
  screen,
  waitFor,
} from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";

const mocks = vi.hoisted(() => ({
  getLaunchOption: vi.fn(),
  showErrorToast: vi.fn(),
  showSuccessToast: vi.fn(),
  router: { MainRunningApp: undefined as { appid: number } | undefined },
}));

vi.mock("@decky/ui", () => ({
  ModalRoot: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  DialogBody: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  DialogHeader: ({ children }: { children: React.ReactNode }) => (
    <h1>{children}</h1>
  ),
  DialogControlsSection: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  Focusable: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  ButtonItem: ({
    children,
    onClick,
    disabled,
  }: {
    children: React.ReactNode;
    onClick?: () => void;
    disabled?: boolean;
  }) => (
    <button onClick={onClick} disabled={disabled}>
      {children}
    </button>
  ),
  Toggle: ({
    value,
    disabled,
    onChange,
  }: {
    value: boolean;
    disabled?: boolean;
    onChange?: (value: boolean) => void;
  }) => (
    <button
      aria-label="Game launch option toggle"
      disabled={disabled}
      onClick={() => onChange?.(!value)}
    >
      {value ? "On" : "Off"}
    </button>
  ),
  TextField: ({
    placeholder,
    value,
    onChange,
  }: {
    placeholder: string;
    value: string;
    onChange: (event: { target: { value: string } }) => void;
  }) => <input placeholder={placeholder} value={value} onChange={onChange} />,
  Spinner: () => <span role="progressbar" />,
  Router: mocks.router,
}));
vi.mock("../../src/api/makoApi", () => ({
  DEFAULT_STEAM_LAUNCH_OPTION: "/home/deck/.local/bin/mako-run %command%",
  getLaunchOption: mocks.getLaunchOption,
}));
vi.mock("../../src/utils/toastUtils", () => ({
  showErrorToast: mocks.showErrorToast,
  showSuccessToast: mocks.showSuccessToast,
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (
    _key: string,
    fallback: string,
    replacements: Record<string, string> = {},
  ) =>
    Object.entries(replacements).reduce(
      (text, [name, value]) => text.split(`{${name}}`).join(value),
      fallback,
    ),
}));

import { LaunchOptionGamesModal } from "../../src/components/LaunchOptionGamesModal";

const MAKO = "/home/deck/.local/bin/mako-run %command%";
const globals = globalThis as Record<string, unknown>;
let stored: Record<number, string>;
let setLaunchOptions: ReturnType<typeof vi.fn>;
let setShortcutLaunchOptions: ReturnType<typeof vi.fn>;
const shortcutExe: Record<number, string> = {
  900: "/home/deck/Games/emulator.AppImage",
  901: '"/usr/bin/flatpak"',
};

function installSteam(apps: { appid: number; display_name: string }[]) {
  globals.collectionStore = {
    localGamesCollection: {
      allApps: apps.map((app) => ({ ...app, app_type: 1 })),
    },
    deckDesktopApps: {
      allApps: [
        { appid: 900, display_name: "Emulator", app_type: 1073741824 },
        { appid: 901, display_name: "Flatpak App", app_type: 1073741824 },
      ],
    },
  };
  const save = (appId: number, options: string) => {
    stored[appId] = options;
  };
  setLaunchOptions = vi.fn(save);
  setShortcutLaunchOptions = vi.fn(save);
  globals.SteamClient = {
    Apps: {
      RegisterForAppDetails: (
        appId: number,
        callback: (details: Record<string, string>) => void,
      ) => {
        if (!(appId in stored)) throw new Error("details unavailable");
        callback(
          appId in shortcutExe
            ? {
                strShortcutLaunchOptions: stored[appId],
                strShortcutExe: shortcutExe[appId],
              }
            : { strLaunchOptions: stored[appId] },
        );
        return { unregister: vi.fn() };
      },
      SetAppLaunchOptions: setLaunchOptions,
      SetShortcutLaunchOptions: setShortcutLaunchOptions,
    },
  };
}

describe("LaunchOptionGamesModal", () => {
  beforeEach(() => {
    window.SP_REACT = React;
    mocks.getLaunchOption.mockResolvedValue({ launch_option: MAKO });
    mocks.router.MainRunningApp = undefined;
    stored = {
      10: "",
      20: `${MAKO} -dx11`,
      30: "PROTON_LOG=1 %command%",
      900: "",
      901: "",
    };
    installSteam([
      { appid: 10, display_name: "Alpha" },
      { appid: 20, display_name: "Beta" },
      { appid: 30, display_name: "Gamma" },
    ]);
  });

  afterEach(() => {
    cleanup();
    delete globals.SteamClient;
    delete globals.collectionStore;
  });

  test("shows installed games with their current MAKO state", async () => {
    render(<LaunchOptionGamesModal />);

    expect(await screen.findByText("Installed Games (5)")).toBeTruthy();
    const toggles = screen.getAllByLabelText("Game launch option toggle");
    expect(toggles.map((toggle) => toggle.textContent)).toEqual([
      "Off",
      "On",
      "Off",
      "Off",
      "Off",
    ]);
    expect(screen.getAllByText("Non-Steam")).toHaveLength(2);
    expect(screen.getAllByText("No launch options")).toHaveLength(3);
    expect(screen.getByText("PROTON_LOG=1 %command%")).toBeTruthy();
  });

  test("adds and removes MAKO for one game while keeping its other options", async () => {
    render(<LaunchOptionGamesModal />);
    await screen.findByText("Gamma");

    fireEvent.click(screen.getAllByLabelText("Game launch option toggle")[4]);
    expect(setLaunchOptions).toHaveBeenLastCalledWith(
      30,
      `PROTON_LOG=1 ${MAKO}`,
    );
    await screen.findByText(`PROTON_LOG=1 ${MAKO}`);

    fireEvent.click(screen.getAllByLabelText("Game launch option toggle")[1]);
    expect(setLaunchOptions).toHaveBeenLastCalledWith(20, "%command% -dx11");
  });

  test("adds MAKO to every game that does not have it yet", async () => {
    render(<LaunchOptionGamesModal />);
    await screen.findByText("Gamma");

    fireEvent.click(screen.getByText("Add to All Games"));

    expect(setLaunchOptions).toHaveBeenCalledTimes(2);
    expect(setShortcutLaunchOptions).toHaveBeenCalledTimes(1);
    expect(stored).toEqual({
      10: MAKO,
      20: `${MAKO} -dx11`,
      30: `PROTON_LOG=1 ${MAKO}`,
      900: MAKO,
      901: "",
    });
    expect(mocks.showSuccessToast).toHaveBeenCalledWith(
      "Apply Launch Option to Games",
      "MAKO added to 3 games",
    );
    await waitFor(() =>
      expect(
        (screen.getByText("Add to All Games") as HTMLButtonElement).disabled,
      ).toBe(true),
    );
  });

  test("puts the running game first and never edits unreadable games", async () => {
    mocks.router.MainRunningApp = { appid: 30 };
    delete stored[10];
    render(<LaunchOptionGamesModal />);

    expect(
      await screen.findByText("Running now: restart the game to apply changes"),
    ).toBeTruthy();
    const names = screen
      .getAllByText(/^(Alpha|Beta|Emulator|Flatpak App|Gamma)$/)
      .map((element) => element.firstChild?.textContent);
    expect(names).toEqual([
      "Gamma",
      "Alpha",
      "Beta",
      "Emulator",
      "Flatpak App",
    ]);
    expect(screen.getByText("Launch options could not be read")).toBeTruthy();

    fireEvent.click(screen.getByText("Remove from All Games"));
    expect(setLaunchOptions).toHaveBeenCalledTimes(1);
    expect(setLaunchOptions).toHaveBeenCalledWith(20, "%command% -dx11");
  });

  test("sets non-Steam shortcuts through Steam's shortcut launch options", async () => {
    render(<LaunchOptionGamesModal />);
    await screen.findByText("Emulator");

    fireEvent.click(screen.getAllByLabelText("Game launch option toggle")[2]);
    expect(setShortcutLaunchOptions).toHaveBeenCalledWith(900, MAKO);
    expect(setLaunchOptions).not.toHaveBeenCalled();
  });

  test("keeps Flatpak shortcuts on the launcher setup path", async () => {
    render(<LaunchOptionGamesModal />);

    expect(
      await screen.findByText(
        "Flatpak shortcut: set it up with Flatpak Setup and the launcher setup guide",
      ),
    ).toBeTruthy();
    const flatpakToggle = screen.getAllByLabelText(
      "Game launch option toggle",
    )[3] as HTMLButtonElement;
    expect(flatpakToggle.disabled).toBe(true);
  });

  test("searches by name and limits bulk actions to the shown games", async () => {
    render(<LaunchOptionGamesModal />);
    await screen.findByText("Gamma");

    fireEvent.change(screen.getByPlaceholderText("Search for a game"), {
      target: { value: "  gam " },
    });

    expect(screen.getByText("Installed Games (1)")).toBeTruthy();
    expect(screen.queryByText("Alpha")).toBeNull();
    fireEvent.click(screen.getByText("Add to Shown Games"));
    expect(setLaunchOptions).toHaveBeenCalledTimes(1);
    expect(setLaunchOptions).toHaveBeenCalledWith(30, `PROTON_LOG=1 ${MAKO}`);

    fireEvent.change(screen.getByPlaceholderText("Search for a game"), {
      target: { value: "zelda" },
    });
    expect(screen.getByText('No games match "zelda"')).toBeTruthy();
  });

  test("explains the manual path when Steam's launch option API is missing", async () => {
    delete globals.SteamClient;
    render(<LaunchOptionGamesModal />);

    expect(
      await screen.findByText("Steam launch options are unavailable"),
    ).toBeTruthy();
    expect(screen.queryByText("Add to All Games")).toBeNull();
  });
});
