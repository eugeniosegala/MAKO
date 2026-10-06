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
import { showErrorToast, ToastMessages } from "../utils/toastUtils";
import t from "../i18n/i18n";

export function useInstallationStatus() {
  const [isInstalled, setIsInstalled] = useState<boolean>(false);
  const [installationStatus, setInstallationStatus] = useState<string>("");
  const [engineUpdateRequired, setEngineUpdateRequired] =
    useState<boolean>(false);
  const [hostArchitectureSupported, setHostArchitectureSupported] =
    useState<boolean>(true);
  const [installedEngineVersion, setInstalledEngineVersion] = useState<
    string | null | undefined
  >();
  const [expectedEngineVersion, setExpectedEngineVersion] = useState<
    string | null | undefined
  >();

  const checkInstallation = async () => {
    try {
      const status = await checkMakoInstalled();
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
      } else if (status.host_architecture_supported === false && status.error) {
        setInstallationStatus(status.error);
      } else {
        setInstallationStatus(
          t("STATUS_ENGINE_NOT_INSTALLED", "MAKO Renderer not installed"),
        );
      }
      return status.installed;
    } catch (error) {
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
    status: string;
  } | null>(null);

  useEffect(() => {
    let active = true;
    let timer: ReturnType<typeof setTimeout>;
    const refresh = async () => {
      try {
        const response = await checkLosslessScalingDll(dll);
        if (!active) return;
        const missing = response.detected === false && !response.error;
        setResult({
          dll,
          detected: response.detected,
          missing,
          status: response.detected
            ? t("STATUS_LOSSLESS_INSTALLED", "Lossless Scaling installed")
            : missing
              ? t("STATUS_LOSSLESS_NOT_INSTALLED", "Lossless Scaling not found")
              : t(
                  "STATUS_LOSSLESS_UNKNOWN",
                  "Unable to check Lossless Scaling.",
                ),
        });
      } catch {
        if (!active) return;
        setResult({
          dll,
          detected: false,
          missing: false,
          status: t(
            "STATUS_LOSSLESS_UNKNOWN",
            "Unable to check Lossless Scaling.",
          ),
        });
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
    dllDetectionStatus: current?.status ?? "",
  };
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

export function useMakoConfig() {
  const [config, setConfig] = useState<ConfigurationData>(() => getDefaults());
  const [vkBasaltConfigPath, setVkBasaltConfigPath] = useState("");
  const [customShaderEffects, setCustomShaderEffects] = useState<
    CustomShaderEffect[]
  >([]);
  const loadRequestId = useRef(0);
  const [isConfigLoading, setIsConfigLoading] = useState(false);
  const configLoadingRef = useRef(false);
  const canEditConfig = useCallback(() => !configLoadingRef.current, []);
  const [powerMode, setPowerMode] = useState("shared");
  const [separatePowerModes, setSeparatePowerModes] = useState(false);
  const [powerSource, setPowerSource] = useState("");
  const powerModeRef = useRef("shared");
  const getEditingPowerMode = useCallback(() => powerModeRef.current, []);

  const loadMakoConfig = useCallback(
    async (
      profileName?: string,
      mode?: string,
      beforeLoad?: () => Promise<void>,
    ) => {
      const requestId = ++loadRequestId.current;
      configLoadingRef.current = true;
      setIsConfigLoading(true);
      setVkBasaltConfigPath("");
      setCustomShaderEffects([]);
      const resetConfig = () => {
        powerModeRef.current = "shared";
        setPowerMode("shared");
        setSeparatePowerModes(false);
        setPowerSource("");
        setConfig(getDefaults());
        setVkBasaltConfigPath("");
        setCustomShaderEffects([]);
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
          configLoadingRef.current = false;
          setIsConfigLoading(false);
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
    loadMakoConfig();
  }, []);

  return {
    config,
    powerMode,
    separatePowerModes,
    powerSource,
    getEditingPowerMode,
    isConfigLoading,
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
