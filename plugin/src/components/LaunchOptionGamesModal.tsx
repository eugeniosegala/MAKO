import { FC, useCallback, useEffect, useMemo, useState } from "react";
import {
  ButtonItem,
  DialogBody,
  DialogControlsSection,
  DialogHeader,
  ModalRoot,
  PanelSectionRow,
  Router,
  TextField,
  Toggle,
} from "@decky/ui";
import { FaGamepad, FaSearch } from "react-icons/fa";
import { DEFAULT_STEAM_LAUNCH_OPTION, getLaunchOption } from "../api/makoApi";
import {
  addMakoLaunchOption,
  filterGames,
  getInstalledGames,
  hasMakoLaunchOption,
  isSteamLaunchOptionApiAvailable,
  readGameLaunchDetails,
  removeMakoLaunchOption,
  writeGameLaunchOptions,
  type GameLaunchDetails,
  type InstalledGame,
} from "../api/steamLaunchOptions";
import t from "../i18n/i18n";
import { showErrorToast, showSuccessToast } from "../utils/toastUtils";
import {
  MakoCompactSpinner,
  MakoFocusable,
  makoPanelItemStyle,
  makoPanelSectionHeaderStyle,
  makoPanelStyle,
} from "./MakoUi";

interface LaunchOptionGamesModalProps {
  closeModal?: () => void;
}

type GameDetails = Record<number, GameLaunchDetails | null>;
type BulkAction = "add-all" | "remove-all";

const secondaryTextStyle = {
  marginTop: "3px",
  color: "#b9cbd0",
  fontSize: "12px",
  lineHeight: 1.35,
  overflowWrap: "anywhere" as const,
};

function runningAppId(): number | null {
  const appId = Number(Router.MainRunningApp?.appid);
  return Number.isInteger(appId) && appId > 0 ? appId : null;
}

function setupHint(details: GameLaunchDetails): string | null {
  if (details.shortcutSetup === "flatpak")
    return t(
      "LAUNCH_GAMES_FLATPAK_SHORTCUT",
      "Flatpak shortcut: set it up with Flatpak Setup and the launcher setup guide",
    );
  if (details.shortcutSetup === "launcher")
    return t(
      "LAUNCH_GAMES_LAUNCHER_SHORTCUT",
      "Launcher shortcut: add MAKO in Heroic or Lutris as shown in the launcher setup guide",
    );
  return null;
}

export const LaunchOptionGamesModal: FC<LaunchOptionGamesModalProps> = ({
  closeModal,
}) => {
  const [launchOption, setLaunchOption] = useState(DEFAULT_STEAM_LAUNCH_OPTION);
  const [games, setGames] = useState<InstalledGame[]>([]);
  const [details, setDetails] = useState<GameDetails>({});
  const [query, setQuery] = useState("");
  const [loading, setLoading] = useState(true);
  const [busy, setBusy] = useState<number | BulkAction | null>(null);
  const apiAvailable = useMemo(isSteamLaunchOptionApiAvailable, []);
  const runningId = useMemo(runningAppId, []);

  useEffect(() => {
    let active = true;
    const load = async () => {
      const option = await getLaunchOption()
        .then((result) => result.launch_option || DEFAULT_STEAM_LAUNCH_OPTION)
        .catch(() => DEFAULT_STEAM_LAUNCH_OPTION);
      const installed = apiAvailable ? getInstalledGames() : [];
      installed.sort(
        (left, right) =>
          Number(right.appId === runningId) - Number(left.appId === runningId),
      );
      const entries = await Promise.all(
        installed.map(async (game) => {
          try {
            return [game.appId, await readGameLaunchDetails(game)];
          } catch {
            return [game.appId, null];
          }
        }),
      );
      if (!active) return;
      setLaunchOption(option);
      setGames(installed);
      setDetails(Object.fromEntries(entries));
      setLoading(false);
    };
    void load();
    return () => {
      active = false;
    };
  }, [apiAvailable, runningId]);

  const shownGames = useMemo(() => filterGames(games, query), [games, query]);
  const searchHint = t("LAUNCH_GAMES_SEARCH", "Search for a game");
  const searchHintProps: Record<string, string> = { placeholder: searchHint };
  const searching = query.trim().length > 0;

  const applyToGame = useCallback(
    (game: InstalledGame, enable: boolean): GameLaunchDetails | null => {
      const current = details[game.appId];
      if (!current) return null;
      if (enable && current.shortcutSetup) return null;
      const next = enable
        ? addMakoLaunchOption(current.launchOptions, launchOption)
        : removeMakoLaunchOption(current.launchOptions);
      if (next === current.launchOptions.trim()) return null;
      writeGameLaunchOptions(game, next);
      return { ...current, launchOptions: next };
    },
    [details, launchOption],
  );

  const handleToggle = (game: InstalledGame, enable: boolean) => {
    if (busy !== null) return;
    setBusy(game.appId);
    try {
      const next = applyToGame(game, enable);
      if (next) setDetails((value) => ({ ...value, [game.appId]: next }));
    } catch (error) {
      showErrorToast(
        t("LAUNCH_GAMES_FAILED", "Could not update launch options"),
        error instanceof Error ? error.message : String(error),
      );
    } finally {
      setBusy(null);
    }
  };

  const handleBulk = (action: BulkAction) => {
    if (busy !== null) return;
    setBusy(action);
    const enable = action === "add-all";
    const updated: GameDetails = {};
    let failures = 0;
    for (const game of shownGames) {
      try {
        const next = applyToGame(game, enable);
        if (next) updated[game.appId] = next;
      } catch {
        failures += 1;
      }
    }
    setDetails((value) => ({ ...value, ...updated }));
    setBusy(null);
    const count = Object.keys(updated).length;
    if (failures)
      showErrorToast(
        t("LAUNCH_GAMES_FAILED", "Could not update launch options"),
        t("LAUNCH_GAMES_BULK_FAILED", "{count} games could not be updated", {
          count: String(failures),
        }),
      );
    else
      showSuccessToast(
        t("LAUNCH_GAMES_TITLE", "Apply Launch Option to Games"),
        enable
          ? t("LAUNCH_GAMES_ADDED_COUNT", "MAKO added to {count} games", {
              count: String(count),
            })
          : t("LAUNCH_GAMES_REMOVED_COUNT", "MAKO removed from {count} games", {
              count: String(count),
            }),
      );
  };

  const header = (
    <DialogHeader>
      {t("LAUNCH_GAMES_TITLE", "Apply Launch Option to Games")}
    </DialogHeader>
  );

  if (loading) {
    return (
      <ModalRoot closeModal={closeModal}>
        {header}
        <DialogBody>
          <div
            style={{
              display: "flex",
              justifyContent: "center",
              padding: "20px",
            }}
          >
            <MakoCompactSpinner size={28} />
          </div>
        </DialogBody>
      </ModalRoot>
    );
  }

  const canAdd = shownGames.some((game) => {
    const current = details[game.appId];
    return (
      current &&
      !current.shortcutSetup &&
      !hasMakoLaunchOption(current.launchOptions)
    );
  });
  const canRemove = shownGames.some((game) =>
    hasMakoLaunchOption(details[game.appId]?.launchOptions ?? ""),
  );

  return (
    <ModalRoot closeModal={closeModal}>
      {header}
      <DialogBody>
        <MakoFocusable flow-children="column">
          <div style={{ ...secondaryTextStyle, marginBottom: "10px" }}>
            {t(
              "LAUNCH_GAMES_DESC",
              "Turn a game on to add the MAKO launch option to its Steam launch options. Turning it off removes only the MAKO command and keeps the game's other options. Changes apply the next time the game starts.",
            )}
          </div>

          {games.length > 0 && (
            <div
              style={{
                display: "flex",
                alignItems: "center",
                gap: "10px",
                marginBottom: "10px",
              }}
            >
              <FaSearch
                aria-hidden="true"
                style={{ color: "#b9cbd0", flex: "0 0 16px" }}
              />
              <div style={{ flex: 1, minWidth: 0 }}>
                <TextField
                  aria-label={searchHint}
                  {...searchHintProps}
                  value={query}
                  bShowClearAction
                  onChange={(event) => setQuery(event.target.value)}
                />
              </div>
            </div>
          )}

          {games.length > 0 && (
            <div style={{ display: "flex", gap: "8px", marginBottom: "12px" }}>
              <div className="Mako_BrandButton" style={{ flex: 1 }}>
                <ButtonItem
                  layout="below"
                  bottomSeparator="none"
                  disabled={busy !== null || !canAdd}
                  onClick={() => handleBulk("add-all")}
                >
                  {searching
                    ? t("LAUNCH_GAMES_ADD_SHOWN", "Add to Shown Games")
                    : t("LAUNCH_GAMES_ADD_ALL", "Add to All Games")}
                </ButtonItem>
              </div>
              <div className="Mako_BrandButton" style={{ flex: 1 }}>
                <ButtonItem
                  layout="below"
                  bottomSeparator="none"
                  disabled={busy !== null || !canRemove}
                  onClick={() => handleBulk("remove-all")}
                >
                  {searching
                    ? t("LAUNCH_GAMES_REMOVE_SHOWN", "Remove from Shown Games")
                    : t("LAUNCH_GAMES_REMOVE_ALL", "Remove from All Games")}
                </ButtonItem>
              </div>
            </div>
          )}

          <div style={{ ...makoPanelStyle, margin: "0 0 18px" }}>
            <div style={makoPanelSectionHeaderStyle}>
              {t("LAUNCH_GAMES_INSTALLED", "Installed Games ({count})", {
                count: String(shownGames.length),
              })}
            </div>
            {games.length === 0 ? (
              <div style={makoPanelItemStyle}>
                <div style={{ color: "#edf8fb", fontWeight: 600 }}>
                  {apiAvailable
                    ? t("LAUNCH_GAMES_NONE", "No installed games found")
                    : t(
                        "LAUNCH_GAMES_UNAVAILABLE",
                        "Steam launch options are unavailable",
                      )}
                </div>
                <div style={secondaryTextStyle}>
                  {t(
                    "LAUNCH_GAMES_NONE_DESC",
                    "Copy the launch option and add it under Steam Properties > Launch Options instead.",
                  )}
                </div>
              </div>
            ) : shownGames.length === 0 ? (
              <div style={makoPanelItemStyle}>
                <div style={secondaryTextStyle}>
                  {t("LAUNCH_GAMES_NO_MATCH", 'No games match "{query}"', {
                    query: query.trim(),
                  })}
                </div>
              </div>
            ) : (
              shownGames.map((game) => {
                const current = details[game.appId];
                const enabled =
                  !!current && hasMakoLaunchOption(current.launchOptions);
                const hint = current ? setupHint(current) : null;
                return (
                  <div
                    key={game.appId}
                    style={{
                      ...makoPanelItemStyle,
                      display: "flex",
                      alignItems: "center",
                      gap: "10px",
                    }}
                  >
                    <FaGamepad
                      style={{
                        color: enabled ? "#7dffac" : "#b9cbd0",
                        flex: "0 0 16px",
                      }}
                    />
                    <div style={{ flex: 1, minWidth: 0 }}>
                      <div
                        style={{
                          color: "#edf8fb",
                          fontWeight: 600,
                          overflowWrap: "anywhere",
                        }}
                      >
                        {game.name}
                        {game.isShortcut && (
                          <span
                            style={{
                              marginLeft: "8px",
                              padding: "1px 6px",
                              borderRadius: "4px",
                              background: "rgba(131, 191, 240, 0.18)",
                              color: "#83bff0",
                              fontSize: "11px",
                              fontWeight: 600,
                              verticalAlign: "middle",
                            }}
                          >
                            {t("LAUNCH_GAMES_NON_STEAM", "Non-Steam")}
                          </span>
                        )}
                      </div>
                      {game.appId === runningId && (
                        <div
                          style={{ ...secondaryTextStyle, color: "#83bff0" }}
                        >
                          {t(
                            "LAUNCH_GAMES_RUNNING",
                            "Running now: restart the game to apply changes",
                          )}
                        </div>
                      )}
                      <div
                        style={{
                          ...secondaryTextStyle,
                          fontFamily: current?.launchOptions
                            ? "monospace"
                            : undefined,
                        }}
                      >
                        {!current
                          ? t(
                              "LAUNCH_GAMES_UNREADABLE",
                              "Launch options could not be read",
                            )
                          : current.launchOptions ||
                            t("LAUNCH_GAMES_EMPTY", "No launch options")}
                      </div>
                      {hint && (
                        <div
                          style={{ ...secondaryTextStyle, color: "#f0c674" }}
                        >
                          {hint}
                        </div>
                      )}
                    </div>
                    <div style={{ flex: "0 0 auto" }}>
                      <Toggle
                        value={enabled}
                        disabled={
                          !current || busy !== null || (!enabled && !!hint)
                        }
                        onChange={(value) => handleToggle(game, value)}
                      />
                    </div>
                  </div>
                );
              })
            )}
          </div>

          <DialogControlsSection>
            <PanelSectionRow>
              <div className="Mako_BrandButton">
                <ButtonItem layout="below" onClick={closeModal}>
                  {t("FLATPAK_CLOSE", "Close")}
                </ButtonItem>
              </div>
            </PanelSectionRow>
          </DialogControlsSection>
        </MakoFocusable>
      </DialogBody>
    </ModalRoot>
  );
};
