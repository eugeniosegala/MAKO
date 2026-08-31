import { useState, type ReactNode } from "react";
import {
  ButtonItem,
  DialogButton,
  PanelSectionRow,
  type AppOverview,
} from "@decky/ui";
import type { LocalDevelopmentBuildInfo } from "../config/devBuildInfo";
import { SUPPORTED_FLATPAK_RUNTIMES } from "../config/configSchema";
import { MakoInstallCompletion } from "./MakoInstallCountdown";
import { MakoCompactSpinner, makoPanelDivider, makoPanelStyle } from "./MakoUi";
import { usePersistentCollapseState } from "../hooks/usePersistentCollapseState";
import t from "../i18n/i18n";

const SUPPORTED_FLATPAK_RUNTIME_VERSION_LIST = SUPPORTED_FLATPAK_RUNTIMES.map(
  ({ version }) => version,
).join(", ");

interface ContentNoticesProps {
  developmentBuildInfo: LocalDevelopmentBuildInfo | null;
  mainRunningApp?: AppOverview;
  showWelcome: boolean;
  engineUpdateRequired: boolean;
  installedEngineVersion?: string | null;
  expectedEngineVersion?: string | null;
  isInstalling: boolean;
  isInstallCompletionVisible: boolean;
  isUninstalling: boolean;
  onInstall: () => Promise<void>;
}

function UnderlinedWelcomeText({ children }: { children: ReactNode }) {
  return (
    <span
      style={{
        textDecorationLine: "underline",
        textDecorationColor: "rgba(131, 191, 240, 0.8)",
        textUnderlineOffset: "2px",
      }}
    >
      {children}
    </span>
  );
}

function WelcomeNotice({ separated }: { separated: boolean }) {
  const [tipsCollapsed, setTipsCollapsed] = usePersistentCollapseState(
    "mako-welcome-tips-collapsed",
    false,
    "welcome tips",
  );
  const expanded = !tipsCollapsed;

  return (
    <PanelSectionRow>
      <div
        role="note"
        style={{
          ...makoPanelStyle,
          width: "100%",
          boxSizing: "border-box",
          marginTop: separated ? "8px" : undefined,
          padding: "12px",
        }}
      >
        <div
          style={{
            color: "#edf8fb",
            fontSize: "13px",
            fontWeight: 700,
            lineHeight: 1.3,
            display: "flex",
            alignItems: "flex-start",
            gap: "7px",
          }}
        >
          <span aria-hidden="true" style={{ fontSize: "16px", lineHeight: 1 }}>
            🦈
          </span>
          <span style={{ flex: 1, minWidth: 0 }}>
            {t("WELCOME_TITLE", "Hello from the MAKO Team!")}
          </span>
        </div>
        {expanded && (
          <>
            <div
              style={{
                marginTop: "7px",
                color: "#c8dce8",
                fontSize: "11px",
                lineHeight: 1.42,
              }}
            >
              <div>
                {t("WELCOME_LIVE_UPDATES", "Many settings apply live.")}{" "}
                {t(
                  "WELCOME_RESTART_REQUIRED",
                  "Options marked Restart require a game restart.",
                )}{" "}
                {t(
                  "WELCOME_PERFORMANCE_NOTE",
                  "Game resolution and scaling changes can affect performance.",
                )}{" "}
                {t("WELCOME_CLEAN_SESSION_PREFIX", "If anything ")}
                <UnderlinedWelcomeText>
                  {t("WELCOME_CLEAN_SESSION_WRONG", "looks or feels wrong")}
                </UnderlinedWelcomeText>
                {t("WELCOME_CLEAN_SESSION_AFTER", " after ")}
                <UnderlinedWelcomeText>
                  {t("WELCOME_CLEAN_SESSION_CHANGES", "several changes")}
                </UnderlinedWelcomeText>
                {t("WELCOME_CLEAN_SESSION_RESTART_SEPARATOR", ", ")}
                <UnderlinedWelcomeText>
                  {t(
                    "WELCOME_CLEAN_SESSION_RESTART",
                    "restart the game for a clean new session.",
                  )}
                </UnderlinedWelcomeText>
              </div>
            </div>
            <div
              style={{
                marginTop: "9px",
                paddingTop: "8px",
                borderTop: makoPanelDivider,
                color: "#9fc1ca",
                fontSize: "10.5px",
                lineHeight: 1.4,
              }}
            >
              {t(
                "WELCOME_ENJOY",
                "Every game is different. Find the best settings that work for you and enjoy playing. MAKO keeps improving with every release, so keep an eye on the release page!",
              )}
            </div>
          </>
        )}
        <div
          style={{
            display: "flex",
            justifyContent: "center",
            marginTop: expanded ? "9px" : "8px",
          }}
        >
          <DialogButton
            aria-expanded={expanded}
            style={{
              width: "auto",
              minWidth: "78px",
              height: "28px",
              padding: "4px 10px",
              fontSize: "11px",
              flexShrink: 0,
            }}
            onClick={() => setTipsCollapsed((current) => !current)}
          >
            {expanded
              ? t("WELCOME_TIPS_COLLAPSE", "Hide tips")
              : t("WELCOME_TIPS_EXPAND", "Show tips")}
          </DialogButton>
        </div>
      </div>
    </PanelSectionRow>
  );
}

/** Purely visual status notices shown above MAKO Decky's controls. */
export function ContentNotices({
  developmentBuildInfo,
  mainRunningApp,
  showWelcome,
  engineUpdateRequired,
  installedEngineVersion,
  expectedEngineVersion,
  isInstalling,
  isInstallCompletionVisible,
  isUninstalling,
  onInstall,
}: ContentNoticesProps) {
  const [showDevelopmentDetails, setShowDevelopmentDetails] = useState(false);
  const hasDevelopmentNotice = Boolean(developmentBuildInfo);
  const hasRunningAppNotice = Boolean(mainRunningApp);

  return (
    <>
      {developmentBuildInfo && (
        <PanelSectionRow>
          <div
            style={{
              padding: "8px 12px",
              width: "100%",
              boxSizing: "border-box",
              backgroundColor: "rgba(33, 150, 243, 0.16)",
              borderRadius: "4px",
              border: "1px solid rgba(33, 150, 243, 0.5)",
              color: "#a8d8ff",
              fontSize: "13px",
              overflow: "hidden",
            }}
          >
            <div
              style={{
                display: "flex",
                alignItems: "center",
                gap: "8px",
                minWidth: 0,
              }}
            >
              <div style={{ flex: 1, minWidth: 0 }}>
                <div style={{ fontWeight: "bold" }}>
                  🧪 Local development deployment
                </div>
                <div
                  style={{
                    marginTop: "2px",
                    color: "#d6ecff",
                    fontSize: "11px",
                    whiteSpace: "nowrap",
                    overflow: "hidden",
                    textOverflow: "ellipsis",
                  }}
                >
                  MAKO Decky <code>{developmentBuildInfo.plugin.commit}</code>
                  {developmentBuildInfo.plugin.dirty ? "*" : ""}
                  {" · MAKO Renderer "}
                  {developmentBuildInfo.engine ? (
                    <code>{developmentBuildInfo.engine.commit}</code>
                  ) : (
                    "unchanged"
                  )}
                  {developmentBuildInfo.engine?.dirty ? "*" : ""}
                </div>
              </div>
              <DialogButton
                aria-expanded={showDevelopmentDetails}
                style={{
                  width: "72px",
                  minWidth: "72px",
                  height: "30px",
                  padding: "4px 8px",
                  fontSize: "12px",
                }}
                onClick={() => setShowDevelopmentDetails((current) => !current)}
              >
                {showDevelopmentDetails ? "Hide" : "Details"}
              </DialogButton>
            </div>
            {showDevelopmentDetails && (
              <div
                style={{
                  display: "flex",
                  flexDirection: "column",
                  gap: "8px",
                  marginTop: "8px",
                  paddingTop: "8px",
                  borderTop: "1px solid rgba(33, 150, 243, 0.35)",
                  overflowWrap: "anywhere",
                }}
              >
                <div style={{ color: "#d6ecff" }}>
                  <span style={{ color: "#83bff0" }}>Deployed</span>{" "}
                  {new Date(developmentBuildInfo.generatedAt).toLocaleString()}
                </div>
                <div>
                  <div style={{ color: "#83bff0", fontWeight: "600" }}>
                    MAKO Decky
                  </div>
                  <div>
                    Commit: <code>{developmentBuildInfo.plugin.commit}</code>
                    {developmentBuildInfo.plugin.dirty ? " + local edits" : ""}
                  </div>
                  <div>
                    Frontend:{" "}
                    {developmentBuildInfo.plugin.frontendDeployed
                      ? "deployed"
                      : "unchanged"}
                  </div>
                  <div>
                    Backend:{" "}
                    {developmentBuildInfo.plugin.backendDeployed
                      ? "deployed"
                      : "unchanged"}
                  </div>
                </div>
                <div>
                  <div style={{ color: "#83bff0", fontWeight: "600" }}>
                    MAKO Renderer
                  </div>
                  {developmentBuildInfo.engine ? (
                    <>
                      <div>
                        Commit:{" "}
                        <code>{developmentBuildInfo.engine.commit}</code>
                        {developmentBuildInfo.engine.dirty
                          ? " + local edits"
                          : ""}
                      </div>
                      <div>
                        64-bit layer:{" "}
                        {developmentBuildInfo.engine.layer64Sha256 ? (
                          <>
                            deployed · SHA-256{" "}
                            <code>
                              {developmentBuildInfo.engine.layer64Sha256.slice(
                                0,
                                12,
                              )}
                            </code>
                          </>
                        ) : (
                          "unchanged"
                        )}
                      </div>
                      <div>
                        32-bit layer:{" "}
                        {developmentBuildInfo.engine.layer32Sha256 ? (
                          <>
                            deployed · SHA-256{" "}
                            <code>
                              {developmentBuildInfo.engine.layer32Sha256.slice(
                                0,
                                12,
                              )}
                            </code>
                          </>
                        ) : (
                          "unchanged"
                        )}
                      </div>
                      <div>
                        Flatpak bundles:{" "}
                        {developmentBuildInfo.engine.flatpakArchiveSha256 ? (
                          <>
                            {SUPPORTED_FLATPAK_RUNTIME_VERSION_LIST} deployed ·
                            SHA-256{" "}
                            <code>
                              {developmentBuildInfo.engine.flatpakArchiveSha256.slice(
                                0,
                                12,
                              )}
                            </code>
                          </>
                        ) : (
                          "unchanged"
                        )}
                      </div>
                    </>
                  ) : (
                    <div>Unchanged by this deployment</div>
                  )}
                </div>
              </div>
            )}
          </div>
        </PanelSectionRow>
      )}

      {showWelcome && <WelcomeNotice separated={hasDevelopmentNotice} />}

      {mainRunningApp && (
        <PanelSectionRow>
          <div
            style={{
              marginTop:
                hasDevelopmentNotice || showWelcome ? "8px" : undefined,
              padding: "8px 12px",
              width: "100%",
              boxSizing: "border-box",
              backgroundColor: "rgba(0, 255, 0, 0.1)",
              borderRadius: "4px",
              border: "1px solid rgba(0, 255, 0, 0.3)",
              fontSize: "13px",
              overflowWrap: "anywhere",
            }}
          >
            <strong>{mainRunningApp.display_name}</strong>{" "}
            {t("CONTENT_RUNNING", "running.")}{" "}
            {t(
              "PROFILE_CAPTURE_READY",
              "MAKO selects saved profiles automatically. If this game is new, save it below; restart the game after changing restart-only settings.",
            )}
          </div>
        </PanelSectionRow>
      )}

      {engineUpdateRequired && (
        <PanelSectionRow>
          <div
            style={{
              marginTop:
                hasDevelopmentNotice || showWelcome || hasRunningAppNotice
                  ? "8px"
                  : undefined,
              padding: "12px",
              borderRadius: "8px",
              background: "rgba(255, 152, 0, 0.16)",
              border: "1px solid rgba(255, 152, 0, 0.7)",
              color: "#ffd08a",
            }}
          >
            <div style={{ fontWeight: "bold", marginBottom: "4px" }}>
              {t(
                "CONTENT_ENGINE_UPDATE_REQUIRED",
                "MAKO Renderer update required",
              )}
            </div>
            <div style={{ fontSize: "13px", marginBottom: "10px" }}>
              {t("CONTENT_ENGINE_INSTALLED", "Installed:")}{" "}
              {installedEngineVersion ||
                t("CONTENT_ENGINE_NOT_RECORDED", "not recorded")}
              . {t("CONTENT_ENGINE_EXPECTS", "This plugin expects:")}{" "}
              {expectedEngineVersion ||
                t("CONTENT_ENGINE_BUNDLED_VERSION", "the bundled version")}
              .
              {!installedEngineVersion &&
                ` ${t(
                  "CONTENT_ENGINE_PREDATES_TRACKING",
                  "The installed payload predates version tracking.",
                )}`}{" "}
              {t(
                "CONTENT_ENGINE_UPDATE_DESC",
                "Reinstall the private engine to apply this plugin release's pinned payload. If you use Heroic, refresh its matching runtime extension in Flatpak Extensions afterwards.",
              )}
            </div>
            <div className="Mako_BrandButton">
              <ButtonItem
                layout="below"
                onClick={onInstall}
                disabled={
                  isInstalling || isInstallCompletionVisible || isUninstalling
                }
              >
                {isInstallCompletionVisible ? (
                  <MakoInstallCompletion />
                ) : (
                  <div
                    style={{
                      display: "flex",
                      alignItems: "center",
                      justifyContent: "center",
                      gap: "8px",
                    }}
                  >
                    {isInstalling && <MakoCompactSpinner />}
                    <span>
                      {isInstalling
                        ? t(
                            "CONTENT_UPDATING_RENDERER",
                            "Updating MAKO Renderer...",
                          )
                        : t("CONTENT_UPDATE_RENDERER", "Update MAKO Renderer")}
                    </span>
                  </div>
                )}
              </ButtonItem>
            </div>
          </div>
        </PanelSectionRow>
      )}
    </>
  );
}
