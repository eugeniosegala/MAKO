import { useState } from "react";
import { installMako, uninstallMako } from "../api/makoApi";
import {
  showInstallErrorToast,
  showUninstallSuccessToast,
  showUninstallErrorToast,
} from "../utils/toastUtils";
import { MAKO_INSTALL_COMPLETION_DURATION_MS } from "../config/uiTiming";
import t from "../i18n/i18n";

export function useInstallationActions() {
  const [isInstalling, setIsInstalling] = useState<boolean>(false);
  const [isUninstalling, setIsUninstalling] = useState<boolean>(false);
  const [isInstallCompletionVisible, setIsInstallCompletionVisible] =
    useState<boolean>(false);

  const handleInstall = async (
    setIsInstalled: (value: boolean) => void,
    setInstallationStatus: (value: string) => void,
    reloadConfig?: () => Promise<void>,
    operation: "install" | "update" = "install",
  ) => {
    setIsInstalling(true);
    setInstallationStatus(
      operation === "update"
        ? t("STATUS_ENGINE_UPDATING", "Updating MAKO Renderer...")
        : t("STATUS_ENGINE_INSTALLING", "Installing MAKO Renderer..."),
    );

    try {
      const result = await installMako();
      if (result.success) {
        setInstallationStatus(
          result.message ||
            t("STATUS_ENGINE_INSTALLED", "MAKO Renderer installed"),
        );
        setIsInstallCompletionVisible(true);

        const completionDelay = new Promise<void>((resolve) => {
          setTimeout(resolve, MAKO_INSTALL_COMPLETION_DURATION_MS);
        });

        if (reloadConfig) {
          await Promise.all([reloadConfig(), completionDelay]);
        } else {
          await completionDelay;
        }

        setIsInstallCompletionVisible(false);
        setIsInstalled(true);
      } else {
        setInstallationStatus(
          `${t("STATUS_INSTALL_FAILED", "Installation failed:")} ${result.error}`,
        );
        showInstallErrorToast(result.error);
      }
    } catch (error) {
      setInstallationStatus(
        `${t("STATUS_INSTALL_FAILED", "Installation failed:")} ${error}`,
      );
      showInstallErrorToast(String(error));
    } finally {
      setIsInstallCompletionVisible(false);
      setIsInstalling(false);
    }
  };

  const handleUninstall = async (
    setIsInstalled: (value: boolean) => void,
    setInstallationStatus: (value: string) => void,
  ) => {
    setIsUninstalling(true);
    setInstallationStatus(
      t("STATUS_ENGINE_REMOVING", "Removing MAKO Renderer..."),
    );

    try {
      const result = await uninstallMako();
      if (result.success) {
        setIsInstalled(false);
        setInstallationStatus(
          t("STATUS_ENGINE_REMOVED", "MAKO Renderer removed successfully!"),
        );
        showUninstallSuccessToast();
      } else {
        setInstallationStatus(
          `${t("STATUS_UNINSTALL_FAILED", "Uninstallation failed:")} ${result.error}`,
        );
        showUninstallErrorToast(result.error);
      }
    } catch (error) {
      setInstallationStatus(
        `${t("STATUS_UNINSTALL_FAILED", "Uninstallation failed:")} ${error}`,
      );
      showUninstallErrorToast(String(error));
    } finally {
      setIsUninstalling(false);
    }
  };

  return {
    isInstalling,
    isUninstalling,
    isInstallCompletionVisible,
    handleInstall,
    handleUninstall,
  };
}
