import { useState, useEffect, useCallback, useRef } from "react";
import {
  checkMakoInstalled,
  checkLosslessScalingDll,
  getMakoConfig,
  getProfileConfig,
  updateMakoConfigFromObject,
  type ConfigUpdateResult
} from "../api/makoApi";
import { ConfigurationData, getDefaults } from "../config/configSchema";
import { showErrorToast, ToastMessages } from "../utils/toastUtils";
import t from "../i18n/i18n";

export function useInstallationStatus() {
  const [isInstalled, setIsInstalled] = useState<boolean>(false);
  const [installationStatus, setInstallationStatus] = useState<string>("");
  const [engineUpdateRequired, setEngineUpdateRequired] = useState<boolean>(false);
  const [hostArchitectureSupported, setHostArchitectureSupported] = useState<boolean>(true);
  const [installedEngineVersion, setInstalledEngineVersion] = useState<string | undefined>();
  const [expectedEngineVersion, setExpectedEngineVersion] = useState<string | undefined>();

  const checkInstallation = async () => {
    try {
      const status = await checkMakoInstalled();
      setIsInstalled(status.installed);
      setEngineUpdateRequired(Boolean(status.engine_update_required));
      setInstalledEngineVersion(status.installed_engine_version);
      setExpectedEngineVersion(status.expected_engine_version);
      setHostArchitectureSupported(status.host_architecture_supported !== false);
      if (status.installed) {
        setInstallationStatus(t("STATUS_ENGINE_INSTALLED", "MAKO Renderer installed"));
      } else if (status.host_architecture_supported === false && status.error) {
        setInstallationStatus(status.error);
      } else {
        setInstallationStatus(t("STATUS_ENGINE_NOT_INSTALLED", "MAKO Renderer not installed"));
      }
      return status.installed;
    } catch (error) {
      setInstallationStatus(t("STATUS_ENGINE_NOT_INSTALLED", "MAKO Renderer not installed"));
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
    checkInstallation
  };
}

export function useDllDetection() {
  const [dllDetected, setDllDetected] = useState<boolean>(false);
  const [dllDetectionStatus, setDllDetectionStatus] = useState<string>("");

  const checkDllDetection = async () => {
    try {
      const result = await checkLosslessScalingDll();
      setDllDetected(result.detected);
      if (result.detected) {
        setDllDetectionStatus(t("STATUS_LOSSLESS_INSTALLED", "Lossless Scaling installed"));
      } else {
        setDllDetectionStatus(t("STATUS_LOSSLESS_NOT_INSTALLED", "Lossless Scaling not installed"));
      }
    } catch (error) {
      setDllDetectionStatus(t("STATUS_LOSSLESS_NOT_INSTALLED", "Lossless Scaling not installed"));
    }
  };

  useEffect(() => {
    checkDllDetection();
  }, []);

  return {
    dllDetected,
    dllDetectionStatus
  };
}

export function useMakoConfig() {
  const [config, setConfig] = useState<ConfigurationData>(() => getDefaults());
  const loadRequestId = useRef(0);

  const loadMakoConfig = useCallback(async (profileName?: string) => {
    const requestId = ++loadRequestId.current;
    try {
      const result = profileName
        ? await getProfileConfig(profileName)
        : await getMakoConfig();
      if (requestId !== loadRequestId.current) return;
      if (result.success && result.config) {
        // Older installed configurations (or a backend that has not yet been
        // reloaded) may not contain fields introduced by a newer frontend.
        // Preserve the generated defaults for any fields missing from the
        // response so an in-place plugin update never renders undefined values.
        setConfig({ ...getDefaults(), ...result.config });
      } else {
        console.log("MAKO Renderer config not available, using defaults:", result.error);
        setConfig(getDefaults());
      }
    } catch (error) {
      if (requestId !== loadRequestId.current) return;
      console.error("Error loading MAKO Renderer config:", error);
      setConfig(getDefaults());
    }
  }, []);

  const updateConfig = useCallback(async (newConfig: ConfigurationData): Promise<ConfigUpdateResult> => {
    try {
      const normalizedConfig = { ...getDefaults(), ...newConfig };
      const result = await updateMakoConfigFromObject(normalizedConfig);
      if (result.success) {
        setConfig(normalizedConfig);
      } else {
        showErrorToast(
          ToastMessages.CONFIG_UPDATE_ERROR.title,
          result.error || ToastMessages.CONFIG_UPDATE_ERROR.body
        );
      }
      return result;
    } catch (error) {
      showErrorToast(ToastMessages.CONFIG_UPDATE_ERROR.title, String(error));
      return { success: false, error: String(error) };
    }
  }, []);

  const updateField = useCallback(async (fieldName: keyof ConfigurationData, value: boolean | number | string): Promise<ConfigUpdateResult> => {
    const newConfig = { ...config, [fieldName]: value };
    return updateConfig(newConfig);
  }, [config, updateConfig]);

  useEffect(() => {
    loadMakoConfig();
  }, []);

  return {
    config,
    setConfig,
    loadMakoConfig,
    updateConfig,
    updateField
  };
}
