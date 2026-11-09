import { useState, useEffect, useCallback, useRef } from "react";
import {
  checkMakoInstalled,
  checkLosslessScalingDll,
  getMakoConfig,
  getProfileConfig,
  getRuntimeStatus,
  updateMakoConfigFromObject,
  type ConfigUpdateResult,
  type CustomShaderEffect,
  type RuntimeStatusResult,
  configFailureResult,
} from "../api/makoApi";
import {
  ConfigurationData,
  type ConfigurationPatch,
  getDefaults,
} from "../config/configSchema";
import {
  MODEL_STATUS_POLL_INTERVAL_MS,
  RUNTIME_STATUS_POLL_INTERVAL_MS,
} from "../config/uiTiming";
import {
  EMPTY_RUNTIME_SCALING_UI_STATE,
  runtimeScalingUiState,
  type RuntimeScalingUiState,
} from "../utils/runtimeScalingUtils";
import {
  showErrorToast,
  showSuccessToast,
  ToastMessages,
} from "../utils/toastUtils";
import t from "../i18n/i18n";
import {
  panelGeneration,
  type ConfigSnapshot,
  type InstallationSnapshot,
} from "./panelSnapshot";

/**
 * The Renderer installation state. A restored snapshot is only shown until the
 * first check completes: installationVerified stays false until then, and
 * installationGeneration names the snapshot generation of the last successful
 * check, which alone may update the remembered panel.
 */
export function useInstallationStatus(restored?: InstallationSnapshot | null) {
  const [isInstalled, setIsInstalled] = useState<boolean>(Boolean(restored));
  const [installationStatus, setInstallationStatus] = useState<string>(() =>
    restored ? t("STATUS_ENGINE_INSTALLED", "MAKO Renderer installed") : "",
  );
  const [engineUpdateRequired, setEngineUpdateRequired] = useState<boolean>(
    restored?.engineUpdateRequired ?? false,
  );
  const [hostArchitectureSupported, setHostArchitectureSupported] =
    useState<boolean>(true);
  const [installedEngineVersion, setInstalledEngineVersion] = useState<
    string | null | undefined
  >(restored?.installedEngineVersion);
  const [expectedEngineVersion, setExpectedEngineVersion] = useState<
    string | null | undefined
  >(restored?.expectedEngineVersion);
  const [installationVerified, setInstallationVerified] = useState(false);
  const verifiedRef = useRef(false);
  const [installationGeneration, setInstallationGeneration] = useState<
    number | null
  >(null);

  const checkInstallation = async () => {
    const generation = panelGeneration();
    try {
      const status = await checkMakoInstalled();
      setInstallationGeneration(generation);
      verifiedRef.current = true;
      setInstallationVerified(true);
      setIsInstalled(status.installed);
      setEngineUpdateRequired(Boolean(status.engine_update_required));
      setInstalledEngineVersion(status.installed_engine_version);
      setExpectedEngineVersion(status.expected_engine_version);
      setHostArchitectureSupported(
        status.host_architecture_supported !== false,
      );
      if (status.installed) {
        setInstallationStatus(
          t("STATUS_ENGINE_INSTALLED", "MAKO Renderer installed"),
        );
      } else if (status.error) {
        setInstallationStatus(status.error);
      } else if (status.system_renderer_notice) {
        setInstallationStatus(status.system_renderer_notice);
      } else {
        setInstallationStatus(
          t("STATUS_ENGINE_NOT_INSTALLED", "MAKO Renderer not installed"),
        );
      }
      return status.installed;
    } catch (error) {
      setInstallationGeneration(null);
      // A restored snapshot is not evidence either: a failed first check shows
      // the same state as a panel that never had one.
      if (!verifiedRef.current) setIsInstalled(false);
      verifiedRef.current = true;
      setInstallationVerified(true);
      setInstallationStatus(
        t("STATUS_ENGINE_NOT_INSTALLED", "MAKO Renderer not installed"),
      );
      setEngineUpdateRequired(false);
      // A transient RPC failure is not evidence that the native host is
      // unsupported. Only the backend's explicit compatibility result should
      // disable the installation action.
      setHostArchitectureSupported(true);
      setInstalledEngineVersion(undefined);
      setExpectedEngineVersion(undefined);
      return false;
    }
  };

  useEffect(() => {
    checkInstallation();
  }, []);

  return {
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
  };
}

export function useDllDetection(dll = "") {
  const [result, setResult] = useState<{
    dll: string;
    detected: boolean;
    missing: boolean;
  } | null>(null);

  useEffect(() => {
    let active = true;
    let timer: ReturnType<typeof setTimeout>;
    const refresh = async () => {
      try {
        const response = await checkLosslessScalingDll(dll);
        if (!active) return;
        setResult({
          dll,
          detected: response.detected,
          missing: response.detected === false && !response.error,
        });
      } catch {
        if (!active) return;
        setResult({ dll, detected: false, missing: false });
      }
      if (active) timer = setTimeout(refresh, MODEL_STATUS_POLL_INTERVAL_MS);
    };
    void refresh();
    return () => {
      active = false;
      clearTimeout(timer);
    };
  }, [dll]);

  const current = result?.dll === dll ? result : null;
  return {
    dllDetected: current?.detected ?? false,
    dllMissing: current?.missing ?? false,
  };
}

// One notice per game process: the Renderer recorded Wine's switch for the
// next start, so this session runs without Frame Generation.
const wineRestartNotified = new Set<string>();

export function notifyWineCompatibilityRestart(
  status: RuntimeStatusResult,
): void {
  for (const context of status.contexts ?? []) {
    if (
      context.frame_generation_unavailable?.wine_compatibility_fix !==
      "restart-required"
    ) {
      continue;
    }
    const key = `${context.pid}:${context.process_start_ticks}`;
    if (wineRestartNotified.has(key)) continue;
    wineRestartNotified.add(key);
    showSuccessToast(
      t("TOAST_WINE_COMPAT_RESTART_TITLE", "Restart to use Frame Generation"),
      t(
        "TOAST_WINE_COMPAT_RESTART",
        "{game} needs the Proton Offscreen Window Fix for Frame Generation. MAKO applied it; restart the game to use it.",
        { game: context.requested.name },
      ),
    );
  }
}

export function useRuntimeScalingStatus(profileName: string, enabled: boolean) {
  const [runtimeState, setRuntimeState] = useState<RuntimeScalingUiState>({
    ...EMPTY_RUNTIME_SCALING_UI_STATE,
  });

  useEffect(() => {
    let active = true;
    const refresh = async () => {
      if (!enabled) {
        if (active) {
          setRuntimeState({ ...EMPTY_RUNTIME_SCALING_UI_STATE });
        }
        return;
      }
      try {
        const status = await getRuntimeStatus(profileName);
        notifyWineCompatibilityRestart(status);
        if (active) {
          setRuntimeState(runtimeScalingUiState(status, profileName));
        }
      } catch {
        if (active) {
          setRuntimeState({ ...EMPTY_RUNTIME_SCALING_UI_STATE });
        }
      }
    };

    void refresh();
    const interval = setInterval(refresh, RUNTIME_STATUS_POLL_INTERVAL_MS);
    return () => {
      active = false;
      clearInterval(interval);
    };
  }, [enabled, profileName]);

  return runtimeState;
}

/**
 * The edited profile's settings. isConfigLoading covers every load and locks
 * edits; isConfigLoadingShown omits the first reload of a restored snapshot's
 * profile, which refreshes the settings in place instead of hiding them, and
 * covers the first render otherwise. configProfile names the profile the shown
 * settings were loaded for.
 */
export function useMakoConfig(restored?: ConfigSnapshot | null) {
  const [config, setConfig] = useState<ConfigurationData>(
    () => restored?.config ?? getDefaults(),
  );
  const [vkBasaltConfigPath, setVkBasaltConfigPath] = useState(
    restored?.vkBasaltConfigPath ?? "",
  );
  const [customShaderEffects, setCustomShaderEffects] = useState<
    CustomShaderEffect[]
  >(restored?.customShaderEffects ?? []);
  const loadRequestId = useRef(0);
  const [isConfigLoading, setIsConfigLoading] = useState(false);
  // Without a restored profile the first render is already the mount load's.
  const [isConfigLoadingShown, setIsConfigLoadingShown] = useState(!restored);
  const configLoadingRef = useRef(false);
  const canEditConfig = useCallback(() => !configLoadingRef.current, []);
  const [powerMode, setPowerMode] = useState(restored?.powerMode ?? "shared");
  const [separatePowerModes, setSeparatePowerModes] = useState(
    restored?.separatePowerModes ?? false,
  );
  const [powerSource, setPowerSource] = useState(restored?.powerSource ?? "");
  const powerModeRef = useRef(restored?.powerMode ?? "shared");
  const getEditingPowerMode = useCallback(() => powerModeRef.current, []);
  const [configProfile, setConfigProfile] = useState<string | null>(
    restored?.profile ?? null,
  );
  // Cleared by the first completed load or by a load of anything else.
  const restoredViewRef = useRef(
    restored
      ? { profile: restored.profile, powerMode: restored.powerMode }
      : null,
  );

  const loadMakoConfig = useCallback(
    async (
      profileName?: string,
      mode?: string,
      beforeLoad?: () => Promise<void>,
    ) => {
      const requestId = ++loadRequestId.current;
      const restoredView = restoredViewRef.current;
      const inPlace = Boolean(
        restoredView &&
        profileName === restoredView.profile &&
        (!mode || mode === restoredView.powerMode),
      );
      if (!inPlace) restoredViewRef.current = null;
      configLoadingRef.current = true;
      setIsConfigLoading(true);
      setIsConfigLoadingShown(!inPlace);
      if (!inPlace) {
        setVkBasaltConfigPath("");
        setCustomShaderEffects([]);
      }
      const resetConfig = () => {
        powerModeRef.current = "shared";
        setPowerMode("shared");
        setSeparatePowerModes(false);
        setPowerSource("");
        setConfig(getDefaults());
        setVkBasaltConfigPath("");
        setCustomShaderEffects([]);
        setConfigProfile(null);
      };
      try {
        // Lock editing before draining writes, then read their canonical result.
        // Error reconciliation can reload directly without awaiting its own write.
        if (beforeLoad) await beforeLoad();
        if (requestId !== loadRequestId.current) return;
        const result = profileName
          ? await (mode
              ? getProfileConfig(profileName, mode)
              : getProfileConfig(profileName))
          : await getMakoConfig();
        if (requestId !== loadRequestId.current) return;
        if (result.success && result.config) {
          // Older installed configurations (or a backend that has not yet been
          // reloaded) may not contain fields introduced by a newer frontend.
          // Preserve the generated defaults for any fields missing from the
          // response so an in-place plugin update never renders undefined values.
          powerModeRef.current = result.power_mode || "shared";
          setPowerMode(powerModeRef.current);
          setSeparatePowerModes(Boolean(result.separate_power_modes));
          setPowerSource(result.power_source || "");
          setConfig({ ...getDefaults(), ...result.config });
          setVkBasaltConfigPath(result.vkbasalt_config_path || "");
          setCustomShaderEffects(result.custom_shader_effects || []);
          setConfigProfile(profileName ?? null);
        } else {
          console.log(
            "MAKO Renderer config not available, using defaults:",
            result.error,
          );
          resetConfig();
        }
      } catch (error) {
        if (requestId !== loadRequestId.current) return;
        console.error("Error loading MAKO Renderer config:", error);
        resetConfig();
      } finally {
        if (requestId === loadRequestId.current) {
          restoredViewRef.current = null;
          configLoadingRef.current = false;
          setIsConfigLoading(false);
          setIsConfigLoadingShown(false);
        }
      }
    },
    [],
  );

  const updateConfig = useCallback(
    async (newConfig: ConfigurationData): Promise<ConfigUpdateResult> => {
      try {
        const normalizedConfig = { ...getDefaults(), ...newConfig };
        const result = await updateMakoConfigFromObject(normalizedConfig);
        if (result.success) {
          setConfig(normalizedConfig);
        } else {
          showErrorToast(
            ToastMessages.CONFIG_UPDATE_ERROR.title,
            result.error || ToastMessages.CONFIG_UPDATE_ERROR.body,
          );
        }
        return result;
      } catch (error) {
        showErrorToast(ToastMessages.CONFIG_UPDATE_ERROR.title, String(error));
        return configFailureResult(String(error));
      }
    },
    [],
  );

  const updateField = useCallback(
    async (
      fieldName: keyof ConfigurationData,
      value: boolean | number | string,
    ): Promise<ConfigUpdateResult> => {
      const newConfig = { ...config, [fieldName]: value };
      return updateConfig(newConfig);
    },
    [config, updateConfig],
  );

  const applyConfigPatch = useCallback((changes: ConfigurationPatch) => {
    setConfig((currentConfig) => ({ ...currentConfig, ...changes }));
  }, []);

  const replaceConfig = useCallback((canonicalConfig: ConfigurationData) => {
    setConfig({ ...getDefaults(), ...canonicalConfig });
  }, []);

  useEffect(() => {
    // A restored profile is refreshed rather than replaced by the global view.
    loadMakoConfig(restoredViewRef.current?.profile);
  }, []);

  return {
    config,
    configProfile,
    powerMode,
    separatePowerModes,
    powerSource,
    getEditingPowerMode,
    isConfigLoading,
    isConfigLoadingShown,
    canEditConfig,
    vkBasaltConfigPath,
    customShaderEffects,
    setConfig,
    applyConfigPatch,
    replaceConfig,
    loadMakoConfig,
    updateConfig,
    updateField,
  };
}
