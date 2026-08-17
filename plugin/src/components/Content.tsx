import { useEffect, useState, type FocusEvent } from "react";
import { AppOverview, ButtonItem, DialogButton, PanelSection, PanelSectionRow, Router, showModal } from "@decky/ui";
import { useInstallationStatus, useDllDetection, useMakoConfig } from "../hooks/useMakoHooks";
import { useProfileManagement } from "../hooks/useProfileManagement";
import { useInstallationActions } from "../hooks/useInstallationActions";
import { StatusDisplay } from "./StatusDisplay";
import { InstallationButton } from "./InstallationButton";
import { ConfigurationSection } from "./ConfigurationSection";
import { ProfileManagement } from "./ProfileManagement";
import { UsageInstructions } from "./UsageInstructions";
import { SmartClipboardButton } from "./SmartClipboardButton";
import { FgmodClipboardButton } from "./FgmodClipboardButton";
import { FpsMultiplierControl } from "./FpsMultiplierControl";
import { AdvancedDetailsModal } from "./AdvancedDetailsModal";
import { FlatpaksModal } from "./FlatpaksModal";
import { ConfigurationData } from "../config/configSchema";
import { localDevelopmentBuildInfo } from "../config/devBuildInfo.generated";
import t from "../i18n/i18n";

export function Content() {
  const [mainRunningApp, setMainRunningApp] = useState<AppOverview | undefined>(undefined);
  const [showDevelopmentDetails, setShowDevelopmentDetails] = useState(false);
  const {
    isInstalled,
    installationStatus,
    engineUpdateRequired,
    installedEngineVersion,
    expectedEngineVersion,
    setIsInstalled,
    setInstallationStatus,
    checkInstallation
  } = useInstallationStatus();

  const { dllDetected, dllDetectionStatus } = useDllDetection();

  const {
    config,
    loadMakoConfig,
    updateField
  } = useMakoConfig();

  const {
    currentProfile,
    updateProfileConfig,
    loadProfiles
  } = useProfileManagement();

  const { isInstalling, isUninstalling, handleInstall, handleUninstall } = useInstallationActions();

  useEffect(() => {
    if (isInstalled) {
      loadMakoConfig();
    }
  }, [isInstalled, loadMakoConfig]);

  useEffect(() => {
    const checkRunningApp = () => {
      setMainRunningApp(Router.MainRunningApp);
    };

    checkRunningApp();
    const interval = setInterval(checkRunningApp, 2000);
    return () => clearInterval(interval);
  }, []);

  const handleConfigChange = async (fieldName: keyof ConfigurationData, value: boolean | number | string) => {
    if (currentProfile) {
      const newConfig = { ...config, [fieldName]: value };
      const result = await updateProfileConfig(currentProfile, newConfig);
      if (result.success) {
        await loadMakoConfig();
      }
    } else {
      await updateField(fieldName, value);
    }
  };

  const onInstall = async () => {
    await handleInstall(setIsInstalled, setInstallationStatus, loadMakoConfig);
    await checkInstallation();
  };

  const onUninstall = () => {
    handleUninstall(setIsInstalled, setInstallationStatus);
  };

  const handleShowAdvancedDetails = () => {
    showModal(<AdvancedDetailsModal />);
  };

  const handleShowFlatpaks = () => {
    showModal(<FlatpaksModal />);
  };

  const keepFocusedControlVisible = (event: FocusEvent<HTMLDivElement>) => {
    const target = event.target;

    // Decky's controller navigation can move focus before its scroll container
    // has caught up, most noticeably when navigating from the bottom back to
    // the first controls. Centre the newly focused control without animation
    // so the top of the plugin is fully reachable and no scroll requests queue.
    requestAnimationFrame(() => {
      target.scrollIntoView({
        block: "center",
        inline: "nearest",
        behavior: "auto"
      });
    });
  };

  return (
    <div onFocusCapture={keepFocusedControlVisible}>
      <PanelSection>
      {localDevelopmentBuildInfo && (
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
              overflow: "hidden"
            }}
          >
            <div
              style={{
                display: "flex",
                alignItems: "center",
                gap: "8px",
                minWidth: 0
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
                    textOverflow: "ellipsis"
                  }}
                >
                  Decky <code>{localDevelopmentBuildInfo.plugin.commit}</code>
                  {localDevelopmentBuildInfo.plugin.dirty ? "*" : ""}
                  {" · MAKO "}
                  {localDevelopmentBuildInfo.engine
                    ? <code>{localDevelopmentBuildInfo.engine.commit}</code>
                    : "unchanged"}
                  {localDevelopmentBuildInfo.engine?.dirty ? "*" : ""}
                </div>
              </div>
              <DialogButton
                aria-expanded={showDevelopmentDetails}
                style={{
                  width: "72px",
                  minWidth: "72px",
                  height: "30px",
                  padding: "4px 8px",
                  fontSize: "12px"
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
                  overflowWrap: "anywhere"
                }}
              >
                <div style={{ color: "#d6ecff" }}>
                  <span style={{ color: "#83bff0" }}>Deployed</span>{" "}
                  {new Date(localDevelopmentBuildInfo.generatedAt).toLocaleString()}
                </div>
                <div>
                  <div style={{ color: "#83bff0", fontWeight: "600" }}>Decky</div>
                  <div>Commit: <code>{localDevelopmentBuildInfo.plugin.commit}</code>{localDevelopmentBuildInfo.plugin.dirty ? " + local edits" : ""}</div>
                  <div>Frontend: {localDevelopmentBuildInfo.plugin.frontendDeployed ? "deployed" : "unchanged"}</div>
                  <div>Backend: {localDevelopmentBuildInfo.plugin.backendDeployed ? "deployed" : "unchanged"}</div>
                </div>
                <div>
                  <div style={{ color: "#83bff0", fontWeight: "600" }}>MAKO</div>
                  {localDevelopmentBuildInfo.engine ? (
                    <>
                      <div>Commit: <code>{localDevelopmentBuildInfo.engine.commit}</code>{localDevelopmentBuildInfo.engine.dirty ? " + local edits" : ""}</div>
                      <div>
                        64-bit layer: {localDevelopmentBuildInfo.engine.layer64Sha256
                          ? <>deployed · SHA-256 <code>{localDevelopmentBuildInfo.engine.layer64Sha256.slice(0, 12)}</code></>
                          : "unchanged"}
                      </div>
                      <div>
                        32-bit layer: {localDevelopmentBuildInfo.engine.layer32Sha256
                          ? <>deployed · SHA-256 <code>{localDevelopmentBuildInfo.engine.layer32Sha256.slice(0, 12)}</code></>
                          : "unchanged"}
                      </div>
                      <div>
                        Flatpak bundles: {localDevelopmentBuildInfo.engine.flatpakArchiveSha256
                          ? <>23.08, 24.08, 25.08 deployed · SHA-256 <code>{localDevelopmentBuildInfo.engine.flatpakArchiveSha256.slice(0, 12)}</code></>
                          : "unchanged"}
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
      {isInstalled && mainRunningApp && (
        <PanelSectionRow>
          <div
            style={{
              marginTop: localDevelopmentBuildInfo ? "8px" : undefined,
              padding: "8px 12px",
              width: "100%",
              boxSizing: "border-box",
              backgroundColor: "rgba(0, 255, 0, 0.1)",
              borderRadius: "4px",
              border: "1px solid rgba(0, 255, 0, 0.3)",
              fontSize: "13px",
              overflowWrap: "anywhere"
            }}
          >
            <strong>{mainRunningApp.display_name}</strong> {t('CONTENT_RUNNING', 'running.')}{" "}{t('PROFILE_CLOSE_GAME', 'Close game to change profile.')}
          </div>
        </PanelSectionRow>
      )}
      {isInstalled && engineUpdateRequired && (
        <PanelSectionRow>
          <div
            style={{
              marginTop: "8px",
              padding: "12px",
              borderRadius: "8px",
              background: "rgba(255, 152, 0, 0.16)",
              border: "1px solid rgba(255, 152, 0, 0.7)",
              color: "#ffd08a"
            }}
          >
            <div style={{ fontWeight: "bold", marginBottom: "4px" }}>
              {t('CONTENT_ENGINE_UPDATE_REQUIRED', 'MAKO Renderer update required')}
            </div>
            <div style={{ fontSize: "13px", marginBottom: "10px" }}>
              {t('CONTENT_ENGINE_INSTALLED', 'Installed:')} {installedEngineVersion || t('CONTENT_ENGINE_NOT_RECORDED', 'not recorded')}. {t('CONTENT_ENGINE_EXPECTS', 'This plugin expects:')} {expectedEngineVersion || t('CONTENT_ENGINE_BUNDLED_VERSION', 'the bundled version')}.
              {!installedEngineVersion && ` ${t('CONTENT_ENGINE_PREDATES_TRACKING', 'The installed payload predates version tracking.')}`} {t('CONTENT_ENGINE_UPDATE_DESC', "Reinstall the private engine to apply this plugin release's pinned payload. If you use Heroic, refresh its matching runtime extension in Flatpak Extensions afterwards.")}
            </div>
            <ButtonItem
              layout="below"
              onClick={onInstall}
              disabled={isInstalling || isUninstalling}
            >
              {t('CONTENT_REINSTALL_RENDERER', 'Reinstall MAKO Renderer')}
            </ButtonItem>
          </div>
        </PanelSectionRow>
      )}
      {!isInstalled && (
        <>
          <InstallationButton
            isInstalled={isInstalled}
            isInstalling={isInstalling}
            isUninstalling={isUninstalling}
            onInstall={onInstall}
            onUninstall={onUninstall}
          />

          <StatusDisplay
            dllDetected={dllDetected}
            dllDetectionStatus={dllDetectionStatus}
            isInstalled={isInstalled}
            installationStatus={installationStatus}
            topMargin="16px"
          />
        </>
      )}

      {isInstalled && (
        <>
          <PanelSectionRow>
            <div
              style={{
                fontSize: "14px",
                fontWeight: "bold",
                marginTop: "24px",
                marginBottom: "8px",
                color: "white"
              }}
            >
              {t("CONTENT_FPS_MULTIPLIER", "Frame Generation Mode")}
            </div>
          </PanelSectionRow>

          <FpsMultiplierControl
            config={config}
            onConfigChange={handleConfigChange}
          />
        </>
      )}

      {isInstalled && (
        <ProfileManagement
          currentProfile={currentProfile}
          mainRunningApp={mainRunningApp}
          onProfileChange={async () => {
            await loadProfiles();
            await loadMakoConfig();
          }}
        />
      )}

      {isInstalled && (
        <ConfigurationSection
          config={config}
          onConfigChange={handleConfigChange}
        />
      )}

      <UsageInstructions />

      <PanelSectionRow>
        <div style={{ marginTop: "24px" }}>
          <ButtonItem
            layout="below"
            bottomSeparator="none"
            onClick={handleShowFlatpaks}
          >
            {t("CONTENT_FLATPAK_SETUP", "Flatpak Setup")}
          </ButtonItem>
        </div>
      </PanelSectionRow>

      <PanelSectionRow>
        <div>
          <ButtonItem
            layout="below"
            bottomSeparator="none"
            onClick={handleShowAdvancedDetails}
          >
            {t("CONTENT_ADVANCED_DETAILS", "Advanced Details")}
          </ButtonItem>
        </div>
      </PanelSectionRow>

      {isInstalled && (
        <>
          <SmartClipboardButton />
          <FgmodClipboardButton />
        </>
      )}

      {isInstalled && (
        <>
          <StatusDisplay
            dllDetected={dllDetected}
            dllDetectionStatus={dllDetectionStatus}
            isInstalled={isInstalled}
            installationStatus={installationStatus}
            topMargin="24px"
          />

          <InstallationButton
            isInstalled={isInstalled}
            isInstalling={isInstalling}
            isUninstalling={isUninstalling}
            onInstall={onInstall}
            onUninstall={onUninstall}
            topMargin="16px"
          />
        </>
      )}
      </PanelSection>
    </div>
  );
}
