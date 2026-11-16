const COMMAND_TOKEN = "%command%";
const APP_TYPE_GAME = 1;
const APP_TYPE_SHORTCUT = 1073741824;
const DEFAULT_READ_TIMEOUT_MS = 4000;
const MAKO_WRAPPER_TOKEN =
  /(^|\s)(?:"[^"]*\/mako-(?:run|launch)"|'[^']*\/mako-(?:run|launch)'|(?:\S*\/)?mako-(?:run|launch))(?=\s|$)/g;

export interface InstalledGame {
  appId: number;
  name: string;
  isShortcut: boolean;
}

export type ShortcutSetup = "flatpak" | "launcher";

export interface GameLaunchDetails {
  launchOptions: string;
  shortcutSetup?: ShortcutSetup;
}

interface AppOverviewLike {
  appid?: number;
  display_name?: string;
  sort_as?: string;
  app_type?: number;
  BIsShortcut?: () => boolean;
}

interface AppDetailsLike {
  strLaunchOptions?: string;
  strShortcutLaunchOptions?: string;
  strShortcutExe?: string;
}

interface SteamAppsLike {
  RegisterForAppDetails?: (
    appId: number,
    callback: (details: AppDetailsLike | null | undefined) => void,
  ) => { unregister?: () => void } | undefined;
  SetAppLaunchOptions?: (appId: number, options: string) => void;
  SetShortcutLaunchOptions?: (appId: number, options: string) => void;
}

interface SteamWindowLike {
  collectionStore?: {
    localGamesCollection?: { allApps?: AppOverviewLike[] };
    deckDesktopApps?: { allApps?: AppOverviewLike[] };
  };
}

function steamApps(): SteamAppsLike | undefined {
  return (globalThis as { SteamClient?: { Apps?: SteamAppsLike } }).SteamClient
    ?.Apps;
}

export function makoWrapperCommand(launchOption: string): string {
  const trimmed = launchOption.trim();
  const index = trimmed.lastIndexOf(COMMAND_TOKEN);
  return (index >= 0 ? trimmed.slice(0, index) : trimmed).trim();
}

function collapseWhitespace(value: string): string {
  return value.replace(/\s+/g, " ").trim();
}

export function hasMakoLaunchOption(options: string): boolean {
  MAKO_WRAPPER_TOKEN.lastIndex = 0;
  return MAKO_WRAPPER_TOKEN.test(options);
}

export function addMakoLaunchOption(
  options: string,
  launchOption: string,
): string {
  const current = options.trim();
  if (hasMakoLaunchOption(current)) return current;
  const wrapper = makoWrapperCommand(launchOption);
  if (!wrapper) return current;
  if (!current) return `${wrapper} ${COMMAND_TOKEN}`;
  const index = current.indexOf(COMMAND_TOKEN);
  if (index < 0) return `${wrapper} ${COMMAND_TOKEN} ${current}`;
  return collapseWhitespace(
    `${current.slice(0, index)} ${wrapper} ${current.slice(index)}`,
  );
}

export function removeMakoLaunchOption(options: string): string {
  const result = collapseWhitespace(options.replace(MAKO_WRAPPER_TOKEN, "$1"));
  return result === COMMAND_TOKEN ? "" : result;
}

export function shortcutSetupFor(
  shortcutExe: string | undefined,
): ShortcutSetup | undefined {
  const exe = (shortcutExe ?? "").toLowerCase();
  if (/(^|[\s"'/])flatpak(["'\s]|$)/.test(exe)) return "flatpak";
  if (/heroic|lutris/.test(exe)) return "launcher";
  return undefined;
}

function isShortcutApp(app: AppOverviewLike): boolean {
  return (
    app.app_type === APP_TYPE_SHORTCUT ||
    (typeof app.BIsShortcut === "function" && app.BIsShortcut())
  );
}

export function getInstalledGames(): InstalledGame[] {
  const store = (globalThis as SteamWindowLike).collectionStore;
  const apps = [
    ...(store?.localGamesCollection?.allApps ?? []),
    ...(store?.deckDesktopApps?.allApps ?? []),
  ];
  const games = new Map<number, InstalledGame & { sortKey: string }>();
  for (const app of apps) {
    const appId = Number(app?.appid);
    if (!Number.isInteger(appId) || appId <= 0 || games.has(appId)) continue;
    const isShortcut = isShortcutApp(app);
    if (
      !isShortcut &&
      app.app_type !== undefined &&
      app.app_type !== APP_TYPE_GAME
    )
      continue;
    const name = app.display_name?.trim() || String(appId);
    games.set(appId, {
      appId,
      name,
      isShortcut,
      sortKey: (app.sort_as || name).toLocaleLowerCase(),
    });
  }
  return [...games.values()]
    .sort((left, right) => left.sortKey.localeCompare(right.sortKey))
    .map(({ appId, name, isShortcut }) => ({ appId, name, isShortcut }));
}

export function filterGames(
  games: InstalledGame[],
  query: string,
): InstalledGame[] {
  const needle = query.trim().toLocaleLowerCase();
  if (!needle) return games;
  return games.filter((game) => game.name.toLocaleLowerCase().includes(needle));
}

export function isSteamLaunchOptionApiAvailable(): boolean {
  const apps = steamApps();
  return (
    typeof apps?.RegisterForAppDetails === "function" &&
    typeof apps?.SetAppLaunchOptions === "function"
  );
}

export function readGameLaunchDetails(
  game: InstalledGame,
  timeoutMs = DEFAULT_READ_TIMEOUT_MS,
): Promise<GameLaunchDetails> {
  const apps = steamApps();
  if (typeof apps?.RegisterForAppDetails !== "function")
    return Promise.reject(new Error("Steam app details are unavailable"));
  return new Promise((resolve, reject) => {
    let settled = false;
    let registration: { unregister?: () => void } | undefined;
    const finish = () => {
      settled = true;
      clearTimeout(timer);
      registration?.unregister?.();
    };
    const timer = setTimeout(() => {
      if (settled) return;
      finish();
      reject(new Error(`Timed out reading launch options for ${game.appId}`));
    }, timeoutMs);
    try {
      registration = apps.RegisterForAppDetails!(game.appId, (details) => {
        if (settled) return;
        finish();
        resolve(
          game.isShortcut
            ? {
                launchOptions: details?.strShortcutLaunchOptions ?? "",
                shortcutSetup: shortcutSetupFor(details?.strShortcutExe),
              }
            : { launchOptions: details?.strLaunchOptions ?? "" },
        );
      });
      if (settled) registration?.unregister?.();
    } catch (error) {
      if (!settled) {
        finish();
        reject(error);
      }
    }
  });
}

export function writeGameLaunchOptions(
  game: InstalledGame,
  options: string,
): void {
  const apps = steamApps();
  const setter = game.isShortcut
    ? apps?.SetShortcutLaunchOptions
    : apps?.SetAppLaunchOptions;
  if (typeof setter !== "function")
    throw new Error("Steam launch options cannot be changed");
  setter.call(apps, game.appId, options);
}
