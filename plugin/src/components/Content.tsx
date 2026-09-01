import type { FocusEvent } from "react";
import {
  ButtonItem,
  PanelSection,
  PanelSectionRow,
  showModal,
} from "@decky/ui";
import {
  useInstallationStatus,
  useDllDetection,
  useMakoConfig,
  useRuntimeScalingStatus,
} from "../hooks/useMakoHooks";
import { useProfileManagement } from "../hooks/useProfileManagement";
import { useInstallationActions } from "../hooks/useInstallationActions";
import { useProfileSession } from "../hooks/useProfileSession";
import { useProfileConfigWriter } from "../hooks/useProfileConfigWriter";
import { StatusDisplay } from "./StatusDisplay";
import { InstallationButton } from "./InstallationButton";
import { ConfigurationSection } from "./ConfigurationSection";
import { ProfileManagement } from "./ProfileManagement";
import { UsageInstructions } from "./UsageInstructions";
import { FgmodClipboardButton } from "./FgmodClipboardButton";
import { FeatureSettings } from "./FeatureSettings";
import { RuntimeStatusCard } from "./RuntimeStatusCard";
import { ContentNotices } from "./ContentNotices";
import { AdvancedDetailsModal } from "./AdvancedDetailsModal";
import { FlatpaksModal } from "./FlatpaksModal";
import { localDevelopmentBuildInfo } from "../config/devBuildInfo.generated";
import { currentRelease } from "virtual:mako-release-info";
import { MakoButtonTheme, MakoReleaseIdentity } from "./MakoUi";
import t from "../i18n/i18n";

export function Content() {
  const {
    isInstalled,
    installationStatus,
    engineUpdateRequired,
    hostArchitectureSupported,
    installedEngineVersion,
    expectedEngineVersion,
    setIsInstalled,
    setInstallationStatus,
    checkInstallation,
  } = useInstallationStatus();

  const { dllDetected, dllDetectionStatus } = useDllDetection();

  const { config, applyConfigPatch, replaceConfig, loadMakoConfig } =
    useMakoConfig();

  const { updateProfileConfigFields, syncCurrentProfile } =
    useProfileManagement();

  const {
    isInstalling,
    isUninstalling,
    isInstallCompletionVisible,
    handleInstall,
    handleUninstall,
  } = useInstallationActions();

  const {
    mainRunningApp,
    editingProfile,
    selectEditingProfile,
    getEditingProfile,
  } = useProfileSession({
    isInstalled,
    loadProfileConfig: loadMakoConfig,
    syncCurrentProfile,
  });
  const scalingRuntimeState = useRuntimeScalingStatus(
    editingProfile,
    Boolean(isInstalled && mainRunningApp),
  );
  const {
    saveConfigChanges: handleConfigChanges,
    saveConfigField: handleConfigChange,
  } = useProfileConfigWriter({
    editingProfile,
    getEditingProfile,
    updateProfileConfigFields,
    loadProfileConfig: loadMakoConfig,
    applyConfigPatch,
    replaceConfig,
  });

  const onInstall = async () => {
    await handleInstall(
      setIsInstalled,
      setInstallationStatus,
      loadMakoConfig,
      engineUpdateRequired ? "update" : "install",
    );
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
        behavior: "auto",
      });
    });
  };

  const hasDevelopmentNotice = Boolean(localDevelopmentBuildInfo);
  const hasRunningAppNotice = Boolean(isInstalled && mainRunningApp);
  const hasEngineUpdateNotice = Boolean(isInstalled && engineUpdateRequired);
  const hasTopNotice =
    isInstalled ||
    hasDevelopmentNotice ||
    hasRunningAppNotice ||
    hasEngineUpdateNotice;

  return (
    <div onFocusCapture={keepFocusedControlVisible}>
      <MakoButtonTheme />
      <PanelSection>
        <MakoReleaseIdentity
          version={currentRelease.version}
          codename={currentRelease.codename}
          bottomMargin={hasTopNotice ? "8px" : "2px"}
        />
        <ContentNotices
          developmentBuildInfo={localDevelopmentBuildInfo}
          mainRunningApp={isInstalled ? mainRunningApp : undefined}
          showWelcome={isInstalled}
          engineUpdateRequired={isInstalled && engineUpdateRequired}
          installedEngineVersion={installedEngineVersion}
          expectedEngineVersion={expectedEngineVersion}
          isInstalling={isInstalling}
          isInstallCompletionVisible={isInstallCompletionVisible}
          isUninstalling={isUninstalling}
          onInstall={onInstall}
        />
        {!isInstalled && (
          <>
            <InstallationButton
              isInstalled={isInstalled}
              isInstalling={isInstalling}
              isInstallCompletionVisible={isInstallCompletionVisible}
              isUninstalling={isUninstalling}
              hostArchitectureSupported={hostArchitectureSupported}
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
          <ProfileManagement
            editingProfile={editingProfile}
            mainRunningApp={mainRunningApp}
            topMargin="18px"
            onProfileChange={async (profileName) => {
              selectEditingProfile(profileName);
              await loadMakoConfig(profileName);
            }}
          />
        )}

        {isInstalled && (
          <>
            {mainRunningApp && (
              <RuntimeStatusCard runtimeState={scalingRuntimeState} />
            )}
            <FeatureSettings
              config={config}
              disabled={engineUpdateRequired}
              runtimeState={scalingRuntimeState}
              onConfigChange={handleConfigChange}
              onConfigUpdate={handleConfigChanges}
            />
          </>
        )}

        <UsageInstructions />

        {isInstalled && <FgmodClipboardButton />}

        {isInstalled && (
          <ConfigurationSection
            config={config}
            onConfigChange={handleConfigChange}
            onConfigUpdate={handleConfigChanges}
            includeAdvancedRendering={false}
          />
        )}

        <PanelSectionRow>
          <div
            className="Mako_BrandButton"
            style={{
              width: "100%",
              boxSizing: "border-box",
              marginTop: "16px",
              paddingTop: "16px",
              borderTop: "1px solid rgba(77, 170, 190, 0.28)",
            }}
          >
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
          <div className="Mako_BrandButton">
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
            <StatusDisplay
              dllDetected={dllDetected}
              dllDetectionStatus={dllDetectionStatus}
              isInstalled={isInstalled}
              installationStatus={installationStatus}
              topMargin="16px"
            />

            <InstallationButton
              isInstalled={isInstalled}
              isInstalling={isInstalling}
              isInstallCompletionVisible={isInstallCompletionVisible}
              isUninstalling={isUninstalling}
              hostArchitectureSupported={hostArchitectureSupported}
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
