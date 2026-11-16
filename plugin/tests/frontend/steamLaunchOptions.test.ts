import { afterEach, describe, expect, test, vi } from "vitest";
import {
  addMakoLaunchOption,
  filterGames,
  getInstalledGames,
  hasMakoLaunchOption,
  makoWrapperCommand,
  readGameLaunchDetails,
  removeMakoLaunchOption,
  shortcutSetupFor,
  writeGameLaunchOptions,
} from "../../src/api/steamLaunchOptions";

const MAKO = "/home/deck/.local/bin/mako-run %command%";
const globals = globalThis as Record<string, unknown>;

afterEach(() => {
  delete globals.SteamClient;
  delete globals.collectionStore;
  vi.useRealTimers();
});

describe("Steam launch option editing", () => {
  test("extracts the wrapper from the Decky launch option", () => {
    expect(makoWrapperCommand(MAKO)).toBe("/home/deck/.local/bin/mako-run");
    expect(makoWrapperCommand("'/home/my deck/mako-run' %command%")).toBe(
      "'/home/my deck/mako-run'",
    );
  });

  test.each([
    ["", MAKO],
    ["%command%", MAKO],
    ["-dx11 -novid", `${MAKO} -dx11 -novid`],
    [
      "PROTON_LOG=1 gamemoderun %command% -dx11",
      "PROTON_LOG=1 gamemoderun /home/deck/.local/bin/mako-run %command% -dx11",
    ],
    [
      "~/fgmod/fgmod %command%",
      "~/fgmod/fgmod /home/deck/.local/bin/mako-run %command%",
    ],
  ])("adds MAKO to %j", (existing, expected) => {
    expect(addMakoLaunchOption(existing, MAKO)).toBe(expected);
  });

  test("keeps an existing MAKO or standalone Renderer launch option", () => {
    expect(addMakoLaunchOption(`${MAKO} -dx11`, MAKO)).toBe(`${MAKO} -dx11`);
    expect(
      addMakoLaunchOption("~/.local/bin/mako-launch %command%", MAKO),
    ).toBe("~/.local/bin/mako-launch %command%");
  });

  test.each([
    [MAKO, ""],
    [`${MAKO} -dx11`, "%command% -dx11"],
    [
      "PROTON_LOG=1 /home/deck/.local/bin/mako-run %command%",
      "PROTON_LOG=1 %command%",
    ],
    ["'/home/my deck/mako-run' %command% -novid", "%command% -novid"],
    ["~/.local/bin/mako-launch %command%", ""],
    ["-dx11", "-dx11"],
  ])("removes MAKO from %j", (existing, expected) => {
    expect(removeMakoLaunchOption(existing)).toBe(expected);
  });

  test("does not mistake similarly named commands for MAKO", () => {
    expect(hasMakoLaunchOption("/opt/mako-runner %command%")).toBe(false);
    expect(hasMakoLaunchOption("~/lsfg %command%")).toBe(false);
    expect(hasMakoLaunchOption("mako-run %command%")).toBe(true);
  });
});

describe("Steam client bindings", () => {
  const steamGame = { appId: 10, name: "alpha", isShortcut: false };
  const shortcut = { appId: 3000000000, name: "Emulator", isShortcut: true };

  test("lists installed Steam games and non-Steam shortcuts, sorted by name", () => {
    globals.collectionStore = {
      localGamesCollection: {
        allApps: [
          { appid: 20, display_name: "Zeta", app_type: 1 },
          { appid: 10, display_name: "alpha", app_type: 1 },
          { appid: 1493710, display_name: "Proton Experimental", app_type: 4 },
          { appid: 20, display_name: "Zeta", app_type: 1 },
        ],
      },
      deckDesktopApps: {
        allApps: [
          {
            appid: 3000000000,
            display_name: "Emulator",
            app_type: 1073741824,
          },
          { appid: 40, display_name: "Mod", BIsShortcut: () => true },
        ],
      },
    };

    expect(getInstalledGames()).toEqual([
      { appId: 10, name: "alpha", isShortcut: false },
      { appId: 3000000000, name: "Emulator", isShortcut: true },
      { appId: 40, name: "Mod", isShortcut: true },
      { appId: 20, name: "Zeta", isShortcut: false },
    ]);
  });

  test("returns no games when Steam's collection store is unavailable", () => {
    expect(getInstalledGames()).toEqual([]);
  });

  test("filters games by name, ignoring case and spaces around the query", () => {
    const games = [
      steamGame,
      shortcut,
      { appId: 20, name: "Zeta", isShortcut: false },
    ];
    expect(filterGames(games, "  EMU ")).toEqual([shortcut]);
    expect(filterGames(games, "")).toBe(games);
    expect(filterGames(games, "missing")).toEqual([]);
  });

  test.each([
    ['"/usr/bin/flatpak"', "flatpak"],
    ["flatpak", "flatpak"],
    ["/usr/bin/flatpak run org.libretro.RetroArch", "flatpak"],
    ["/home/deck/Applications/Heroic.AppImage", "launcher"],
    ["/usr/bin/lutris", "launcher"],
    ["/home/deck/Games/game.exe", undefined],
    [undefined, undefined],
  ])("classifies shortcut target %j", (exe, expected) => {
    expect(shortcutSetupFor(exe)).toBe(expected);
  });

  test("reads launch options and unregisters, including synchronous replies", async () => {
    const unregister = vi.fn();
    globals.SteamClient = {
      Apps: {
        RegisterForAppDetails: (
          _appId: number,
          callback: (details: Record<string, string>) => void,
        ) => {
          callback({
            strLaunchOptions: "-dx11",
            strShortcutLaunchOptions: "%command% --fullscreen",
            strShortcutExe: '"/usr/bin/flatpak"',
          });
          return { unregister };
        },
      },
    };

    await expect(readGameLaunchDetails(steamGame)).resolves.toEqual({
      launchOptions: "-dx11",
    });
    await expect(readGameLaunchDetails(shortcut)).resolves.toEqual({
      launchOptions: "%command% --fullscreen",
      shortcutSetup: "flatpak",
    });
    expect(unregister).toHaveBeenCalledTimes(2);
  });

  test("times out when Steam never answers", async () => {
    vi.useFakeTimers();
    const unregister = vi.fn();
    globals.SteamClient = {
      Apps: { RegisterForAppDetails: () => ({ unregister }) },
    };

    const result = readGameLaunchDetails(steamGame, 50);
    vi.advanceTimersByTime(60);
    await expect(result).rejects.toThrow("Timed out");
    expect(unregister).toHaveBeenCalledTimes(1);
  });

  test("writes Steam games and shortcuts through their own setters", () => {
    const SetAppLaunchOptions = vi.fn();
    const SetShortcutLaunchOptions = vi.fn();
    globals.SteamClient = {
      Apps: { SetAppLaunchOptions, SetShortcutLaunchOptions },
    };
    writeGameLaunchOptions(steamGame, MAKO);
    writeGameLaunchOptions(shortcut, MAKO);
    expect(SetAppLaunchOptions).toHaveBeenCalledWith(10, MAKO);
    expect(SetShortcutLaunchOptions).toHaveBeenCalledWith(3000000000, MAKO);

    delete globals.SteamClient;
    expect(() => writeGameLaunchOptions(steamGame, MAKO)).toThrow();
  });
});
