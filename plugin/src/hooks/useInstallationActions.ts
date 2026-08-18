import { useState } from "react";
import { installMako, uninstallMako } from "../api/makoApi";
import {
  showInstallSuccessToast,
  showInstallErrorToast,
  showUninstallSuccessToast,
  showUninstallErrorToast
} from "../utils/toastUtils";
import t from "../i18n/i18n";

export function useInstallationActions() {
  const [isInstalling, setIsInstalling] = useState<boolean>(false);
  const [isUninstalling, setIsUninstalling] = useState<boolean>(false);

  const handleInstall = async (
    setIsInstalled: (value: boolean) => void,
    setInstallationStatus: (value: string) => void,
    reloadConfig?: () => Promise<void>,
    operation: "install" | "update" = "install"
  ) => {
    setIsInstalling(true);
    setInstallationStatus(operation === "update"
      ? t("STATUS_ENGINE_UPDATING", "Updating MAKO Renderer...")
      : t("STATUS_ENGINE_INSTALLING", "Installing MAKO Renderer..."));

    try {
      const result = await installMako();
      if (result.success) {
        setIsInstalled(true);
        setInstallationStatus(result.message || t("STATUS_ENGINE_INSTALLED", "MAKO Renderer installed"));
        showInstallSuccessToast();

        // Reload MAKO Renderer config after installation
        if (reloadConfig) {
          await reloadConfig();
        }
      } else {
        setInstallationStatus(`${t("STATUS_INSTALL_FAILED", "Installation failed:")} ${result.error}`);
        showInstallErrorToast(result.error);
      }
    } catch (error) {
      setInstallationStatus(`${t("STATUS_INSTALL_FAILED", "Installation failed:")} ${error}`);
      showInstallErrorToast(String(error));
    } finally {
      setIsInstalling(false);
    }
  };

  const handleUninstall = async (
    setIsInstalled: (value: boolean) => void,
    setInstallationStatus: (value: string) => void
  ) => {
    setIsUninstalling(true);
    setInstallationStatus(t("STATUS_ENGINE_REMOVING", "Removing MAKO Renderer..."));

    try {
      const result = await uninstallMako();
      if (result.success) {
        setIsInstalled(false);
        setInstallationStatus(t("STATUS_ENGINE_REMOVED", "MAKO Renderer removed successfully!"));
        showUninstallSuccessToast();
      } else {
        setInstallationStatus(`${t("STATUS_UNINSTALL_FAILED", "Uninstallation failed:")} ${result.error}`);
        showUninstallErrorToast(result.error);
      }
    } catch (error) {
      setInstallationStatus(`${t("STATUS_UNINSTALL_FAILED", "Uninstallation failed:")} ${error}`);
      showUninstallErrorToast(String(error));
    } finally {
      setIsUninstalling(false);
    }
  };

  return {
    isInstalling,
    isUninstalling,
    handleInstall,
    handleUninstall
  };
}
