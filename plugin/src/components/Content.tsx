import {
  ButtonItem,
  PanelSection,
  PanelSectionRow,
  showModal,
} from "@decky/ui";
import { useCallback, useRef, useState } from "react";
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
import { PowerProfileControls } from "./PowerProfileControls";
import { StatusDisplay } from "./StatusDisplay";
import { InstallationButton } from "./InstallationButton";
import { RemotePlaySection } from "./RemotePlaySection";
import { ConfigurationSection } from "./ConfigurationSection";
import { useModelStatus } from "../hooks/useModelStatus";
import { effectiveScalingMethod } from "../config/ultraPerformancePreset";
import { ProfileManagement } from "./ProfileManagement";
import { UsageInstructions } from "./UsageInstructions";
import { FgmodClipboardButton } from "./FgmodClipboardButton";
import { FeatureSettings } from "./FeatureSettings";
import { RuntimeStatusCard } from "./RuntimeStatusCard";
import { ContentNotices } from "./ContentNotices";
import { InfoVisibility } from "./InfoVisibility";
import { AdvancedDetailsModal } from "./AdvancedDetailsModal";
import { FlatpaksModal } from "./FlatpaksModal";
import { localDevelopmentBuildInfo } from "../config/devBuildInfo.generated";
import { currentRelease } from "virtual:mako-release-info";
import { MakoButtonTheme, MakoReleaseIdentity } from "./MakoUi";
import t from "../i18n/i18n";
import { addProfileShader, deleteProfileShaders } from "../api/makoApi";

export function Content() {
  const [profileRevision, setProfileRevision] = useState(0);
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

  const {
    config,
    vkBasaltConfigPath,
    customShaderEffects,
    powerMode,
    separatePowerModes,
    powerSource,
    getEditingPowerMode,
    isConfigLoading,
    canEditConfig,
    applyConfigPatch,
    replaceConfig,
    loadMakoConfig,
  } = useMakoConfig();

  // Session selection and the writer depend on each other. Keep only the
  // current queue drain here; useMakoConfig owns load locking and stale replies.
  const flushConfigChangesRef = useRef<(() => Promise<void>) | undefined>();
  const loadEditorConfig = useCallback(
    (profileName?: string, mode?: string) =>
      loadMakoConfig(
        profileName,
        mode,
        () => flushConfigChangesRef.current?.() ?? Promise.resolve(),
      ),
    [loadMakoConfig],
  );

  const { dllDetected, dllMissing, dllDetectionStatus } = useDllDetection(
    config.dll,
  );

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
    remotePlayRunning,
    powerSource: currentPowerSource,
    editingProfile,
    selectEditingProfile,
    getEditingProfile,
  } = useProfileSession({
    isInstalled,
    loadProfileConfig: loadEditorConfig,
    syncCurrentProfile,
  });
  const scalingRuntimeState = useRuntimeScalingStatus(
    editingProfile,
    Boolean(isInstalled && (mainRunningApp || remotePlayRunning)),
  );
  const modelStatus = useModelStatus(config, isInstalled);
  const {
    saveConfigChanges: handleConfigChanges,
    saveConfigField: handleConfigChange,
    flushConfigChanges,
  } = useProfileConfigWriter({
    editingProfile,
    editingPowerMode: powerMode,
    getEditingPowerMode,
    canEditConfig,
    getEditingProfile,
    updateProfileConfigFields,
    loadProfileConfig: loadMakoConfig,
    applyConfigPatch,
    replaceConfig,
  });
  flushConfigChangesRef.current = flushConfigChanges;

  const refreshShaders = async () => {
    const profile = editingProfile;
    await flushConfigChanges();
    if (getEditingProfile() === profile) {
      await loadEditorConfig(profile, getEditingPowerMode());
    }
  };

  const addShader = async (path: string) => {
    const profile = editingProfile;
    await flushConfigChanges();
    const result = await addProfileShader(profile, path);
    if (!result.success)
      throw new Error(result.error || "Unable to add shader");
    if (getEditingProfile() === profile) {
      await loadEditorConfig(profile, getEditingPowerMode());
    }
  };

  const deleteShaders = async (shaderIds: string[]) => {
    const profile = editingProfile;
    await flushConfigChanges();
    const result = await deleteProfileShaders(profile, shaderIds);
    if (!result.success)
      throw new Error(result.error || "Unable to delete custom shaders");
    if (getEditingProfile() === profile) {
      await loadEditorConfig(profile, getEditingPowerMode());
    }
  };

  const onInstall = async () => {
    await handleInstall(
      setIsInstalled,
      setInstallationStatus,
      loadEditorConfig,
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

  const hasDevelopmentNotice = Boolean(localDevelopmentBuildInfo);
  const hasRunningAppNotice = Boolean(
    isInstalled && (mainRunningApp || remotePlayRunning),
  );
  const hasEngineUpdateNotice = Boolean(isInstalled && engineUpdateRequired);
  const hasTopNotice =
    isInstalled ||
    hasDevelopmentNotice ||
    hasRunningAppNotice ||
    hasEngineUpdateNotice;

  return (
    <InfoVisibility>
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
          modelStatus={{
            ...modelStatus,
            dllMissing: isInstalled && dllMissing,
            ls1RuntimeFallback:
              isInstalled &&
              !config.disable_mako &&
              config.scaling_enabled &&
              ["ls1", "ls1-performance"].includes(
                effectiveScalingMethod(config),
              ) &&
              scalingRuntimeState.requestedMethod ===
                effectiveScalingMethod(config) &&
              scalingRuntimeState.scalingActive &&
              scalingRuntimeState.activeMethod === "mako" &&
              Boolean(scalingRuntimeState.fallbackReason),
          }}
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
            sessionRunning={remotePlayRunning}
            profileRevision={profileRevision}
            mainRunningApp={mainRunningApp}
            topMargin="18px"
            onProfileChange={async (profileName) => {
              selectEditingProfile(profileName);
              await loadEditorConfig(profileName);
            }}
          />
        )}

        {isInstalled && (
          <>
            <PowerProfileControls
              profileName={editingProfile}
              enabled={separatePowerModes}
              powerMode={powerMode}
              powerSource={currentPowerSource ?? powerSource}
              disabled={isConfigLoading}
              flushConfigChanges={flushConfigChanges}
              loadProfileConfig={loadEditorConfig}
            />
            {(mainRunningApp || remotePlayRunning) && (
              <RuntimeStatusCard runtimeState={scalingRuntimeState} />
            )}
            {isConfigLoading && (
              <PanelSectionRow>
                {t("POWER_LOADING", "Loading profile settings...")}
              </PanelSectionRow>
            )}
            <div hidden={isConfigLoading}>
              <FeatureSettings
                config={config}
                disabled={engineUpdateRequired}
                runtimeState={scalingRuntimeState}
                profileName={editingProfile}
                vkBasaltConfigPath={vkBasaltConfigPath}
                customShaderEffects={customShaderEffects}
                onAddShader={addShader}
                onRefreshShaders={refreshShaders}
                onDeleteShaders={deleteShaders}
                onConfigChange={handleConfigChange}
                onConfigUpdate={handleConfigChanges}
              />
            </div>
          </>
        )}

        <UsageInstructions />

        {isInstalled && <FgmodClipboardButton />}

        {isInstalled && (
          <div hidden={isConfigLoading}>
            <ConfigurationSection
              config={config}
              onConfigChange={handleConfigChange}
              onConfigUpdate={handleConfigChanges}
              includeAdvancedRendering={false}
            />
          </div>
        )}

        {isInstalled && (
          <RemotePlaySection
            onPrepare={flushConfigChanges}
            onChanged={() => setProfileRevision((value) => value + 1)}
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
    </InfoVisibility>
  );
}
