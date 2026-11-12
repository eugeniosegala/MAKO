import { ShaderParametersModal } from "./settings/shaders/ShaderParametersModal";
import {
  ButtonItem,
  PanelSection,
  PanelSectionRow,
  showModal,
  Navigation,
  QuickAccessTab,
  Router,
} from "@decky/ui";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
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
import {
  forgetPanel,
  rememberPanel,
  restorePanel,
  type PanelView,
  type ProfilesSnapshot,
} from "../hooks/panelSnapshot";
import { PowerProfileControls } from "./PowerProfileControls";
import { StatusDisplay } from "./StatusDisplay";
import { InstallationButton } from "./InstallationButton";
import { RemotePlaySection } from "./RemotePlaySection";
import { ConfigurationSection } from "./ConfigurationSection";
import { useModelStatus } from "../hooks/useModelStatus";
import { useModelFallback } from "../hooks/useModelFallback";
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
import {
  addProfileShader,
  deleteProfileShaders,
  reloadProfileShaders,
  type RemotePlayResult,
} from "../api/makoApi";

function currentAppId() {
  const app = Router.MainRunningApp;
  return app ? String(app.appid) : null;
}

export function Content() {
  const [profileRevision, setProfileRevision] = useState(0);
  // Decky remounts the panel on every open; start from what it last showed.
  const [restored] = useState(() => restorePanel(currentAppId()));
  const {
    isInstalled,
    installationStatus,
    engineUpdateRequired,
    hostArchitectureSupported,
    installedEngineVersion,
    expectedEngineVersion,
    installationVerified,
    installationGeneration,
    setIsInstalled,
    setInstallationStatus,
    checkInstallation,
  } = useInstallationStatus(restored?.installation);

  const {
    config,
    configProfile,
    vkBasaltConfigPath,
    customShaderEffects,
    powerMode,
    separatePowerModes,
    powerSource,
    getEditingPowerMode,
    isConfigLoading,
    isConfigLoadingShown,
    canEditConfig,
    applyConfigPatch,
    replaceConfig,
    loadMakoConfig,
  } = useMakoConfig(restored?.config);

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

  const { dllDetected, dllMissing } = useDllDetection(config.dll);

  const { updateProfileConfigFields, syncCurrentProfile, setCurrentProfile } =
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
    profileSynced,
    selectEditingProfile,
    getEditingProfile,
  } = useProfileSession({
    isInstalled,
    loadProfileConfig: loadEditorConfig,
    syncCurrentProfile,
    restored: restored?.session,
  });
  const scalingRuntimeState = useRuntimeScalingStatus(
    editingProfile,
    Boolean(isInstalled && (mainRunningApp || remotePlayRunning)),
  );
  // Live status shows the remembered reading only until the first poll, and
  // only for display: model fallbacks use live readings alone.
  const initialRuntimeState = useRef(scalingRuntimeState);
  const [runtimeLive, setRuntimeLive] = useState(false);
  useEffect(() => {
    if (scalingRuntimeState !== initialRuntimeState.current)
      setRuntimeLive(true);
  }, [scalingRuntimeState]);
  const shownRuntimeState =
    !runtimeLive && restored?.runtime?.profile === editingProfile
      ? restored.runtime.state
      : scalingRuntimeState;
  const [loadedProfiles, setLoadedProfiles] =
    useState<ProfilesSnapshot | null>();
  const [remotePlayStatus, setRemotePlayStatus] =
    useState<RemotePlayResult | null>();
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
  const availableModelStatus = useModelFallback({
    config,
    profileName: editingProfile,
    powerMode,
    enabled: isInstalled && installationVerified && !isConfigLoading,
    dllMissing,
    dllDetected,
    ...modelStatus,
    ls1RuntimeFallback:
      scalingRuntimeState.requestedMethod === effectiveScalingMethod(config) &&
      scalingRuntimeState.scalingActive &&
      scalingRuntimeState.activeMethod === "mako" &&
      Boolean(scalingRuntimeState.fallbackReason),
    saveConfigChanges: handleConfigChanges,
  });

  const customizeShaders = async () => {
    const profile = editingProfile;
    await flushConfigChanges();
    if (getEditingProfile() !== profile) return;
    Navigation.CloseSideMenus();
    showModal(
      <ShaderParametersModal
        profileName={profile}
        onReturn={() => Navigation.OpenQuickAccessMenu(QuickAccessTab.Decky)}
      />,
      undefined,
      { bNeverPopOut: true },
    );
  };
  const refreshShaders = async () => {
    const profile = editingProfile;
    await flushConfigChanges();
    const result = await reloadProfileShaders(profile);
    if (!result.success)
      throw new Error(result.error || "Unable to reload shaders");
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

  // Installation actions act on the real check, never on a restored snapshot,
  // and the snapshot is dropped before and after every change they make.
  const onInstall = async () => {
    if (!installationVerified) return;
    forgetPanel();
    await handleInstall(
      setIsInstalled,
      setInstallationStatus,
      loadEditorConfig,
      engineUpdateRequired ? "update" : "install",
      async () => {
        forgetPanel();
        await checkInstallation();
      },
    );
  };

  const onUninstall = async () => {
    if (!installationVerified) return;
    forgetPanel();
    try {
      await handleUninstall(setIsInstalled, setInstallationStatus);
    } finally {
      forgetPanel();
    }
  };

  // A snapshot that outlived the Renderer loaded a profile; show the global
  // settings view instead, as on a panel that never restored one.
  const restoredConfigChecked = useRef(!restored?.config);
  useEffect(() => {
    if (!installationVerified || restoredConfigChecked.current) return;
    restoredConfigChecked.current = true;
    if (!isInstalled) void loadMakoConfig();
  }, [installationVerified, isInstalled, loadMakoConfig]);

  const panelView = useMemo((): PanelView | null => {
    if (!isInstalled || !hostArchitectureSupported) return null;
    const settled = profileSynced && !isConfigLoading;
    return {
      installation: {
        engineUpdateRequired,
        installedEngineVersion: installedEngineVersion ?? null,
        expectedEngineVersion: expectedEngineVersion ?? null,
      },
      profiles: loadedProfiles,
      remotePlay: remotePlayStatus,
      session: profileSynced
        ? {
            profile: editingProfile,
            appId: currentAppId(),
            gameRunning: Boolean(mainRunningApp),
            remotePlayRunning,
          }
        : undefined,
      config: !settled
        ? undefined
        : configProfile === editingProfile
          ? {
              profile: editingProfile,
              powerMode,
              separatePowerModes,
              powerSource,
              config,
              vkBasaltConfigPath,
              customShaderEffects,
            }
          : null,
      runtime:
        !profileSynced || !runtimeLive
          ? undefined
          : mainRunningApp || remotePlayRunning
            ? { profile: editingProfile, state: scalingRuntimeState }
            : null,
    };
  }, [
    isInstalled,
    hostArchitectureSupported,
    profileSynced,
    isConfigLoading,
    engineUpdateRequired,
    installedEngineVersion,
    expectedEngineVersion,
    loadedProfiles,
    remotePlayStatus,
    editingProfile,
    mainRunningApp,
    remotePlayRunning,
    configProfile,
    powerMode,
    separatePowerModes,
    powerSource,
    config,
    vkBasaltConfigPath,
    customShaderEffects,
    runtimeLive,
    scalingRuntimeState,
  ]);
  // Only a successful check, outside an installation change, describes what
  // the next panel should show.
  const canRemember =
    installationGeneration !== null &&
    !isInstalling &&
    !isUninstalling &&
    !isInstallCompletionVisible;
  useEffect(() => {
    if (canRemember && installationGeneration !== null)
      rememberPanel(panelView, installationGeneration);
  }, [canRemember, installationGeneration, panelView]);

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
          installationVerified={installationVerified}
          onInstall={onInstall}
          modelStatus={{
            ...availableModelStatus,
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
              installationVerified={installationVerified}
              onInstall={onInstall}
              onUninstall={onUninstall}
            />

            <StatusDisplay
              dllDetected={dllDetected}
              isInstalled={isInstalled}
              installationStatus={installationStatus}
              topMargin="16px"
            />
          </>
        )}

        {isInstalled && (
          <ProfileManagement
            editingProfile={editingProfile}
            onBeforeProfileMutation={flushConfigChanges}
            sessionRunning={remotePlayRunning}
            profileRevision={profileRevision}
            mainRunningApp={mainRunningApp}
            topMargin="18px"
            restoredProfiles={restored?.profiles}
            onProfilesLoaded={setLoadedProfiles}
            onProfileChange={async (profileName) => {
              await flushConfigChanges();
              const result = await setCurrentProfile(profileName);
              if (!result.success)
                throw new Error(result.error || "Could not select profile");
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
              disabled={isConfigLoadingShown}
              flushConfigChanges={flushConfigChanges}
              loadProfileConfig={loadEditorConfig}
            />
            {(mainRunningApp || remotePlayRunning) && (
              <RuntimeStatusCard
                runtimeState={shownRuntimeState}
                activeConfig={
                  !isConfigLoadingShown &&
                  (!separatePowerModes ||
                    powerMode === (currentPowerSource ?? powerSource))
                    ? config
                    : undefined
                }
              />
            )}
            {isConfigLoadingShown && (
              <PanelSectionRow>
                {t("POWER_LOADING", "Loading profile settings...")}
              </PanelSectionRow>
            )}
            <div hidden={isConfigLoadingShown}>
              <FeatureSettings
                config={config}
                losslessFrameGenerationAvailable={
                  dllDetected && availableModelStatus.lsfg?.compatible !== false
                }
                losslessScalingAvailable={
                  dllDetected && availableModelStatus.ls1?.compatible !== false
                }
                runtimeState={shownRuntimeState}
                profileName={editingProfile}
                vkBasaltConfigPath={vkBasaltConfigPath}
                customShaderEffects={customShaderEffects}
                onAddShader={addShader}
                onRefreshShaders={refreshShaders}
                onCustomizeShaders={customizeShaders}
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
          <div hidden={isConfigLoadingShown}>
            <ConfigurationSection
              config={config}
              onConfigChange={handleConfigChange}
              onConfigUpdate={handleConfigChanges}
              groups={["external-tools", "manual-overrides"]}
            />
          </div>
        )}

        {isInstalled && (
          <RemotePlaySection
            onPrepare={async () => {
              await flushConfigChanges();
              const result = await setCurrentProfile(getEditingProfile());
              if (!result.success)
                throw new Error(result.error || "Could not select profile");
            }}
            onChanged={() => setProfileRevision((value) => value + 1)}
            restoredStatus={restored?.remotePlay}
            onStatus={setRemotePlayStatus}
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
              installationVerified={installationVerified}
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
