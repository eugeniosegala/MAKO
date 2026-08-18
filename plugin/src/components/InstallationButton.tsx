import { ButtonItem, PanelSectionRow } from "@decky/ui";
import { FaDownload, FaTrash } from "react-icons/fa";
import t from "../i18n/i18n";

interface InstallationButtonProps {
  isInstalled: boolean;
  isInstalling: boolean;
  isUninstalling: boolean;
  onInstall: () => void;
  onUninstall: () => void;
  topMargin?: string;
}

export function InstallationButton({
  isInstalled,
  isInstalling,
  isUninstalling,
  onInstall,
  onUninstall,
  topMargin = "0"
}: InstallationButtonProps) {
  const renderButtonContent = () => {
    if (isInstalling) {
      return (
        <div style={{ display: "flex", alignItems: "center", gap: "8px" }}>
          <div>{t("INSTALL_INSTALLING", "Installing MAKO Renderer...")}</div>
        </div>
      );
    }

    if (isUninstalling) {
      return (
        <div style={{ display: "flex", alignItems: "center", gap: "8px" }}>
          <div>{t("INSTALL_UNINSTALLING", "Removing MAKO Renderer...")}</div>
        </div>
      );
    }

    if (isInstalled) {
      return (
        <div style={{ display: "flex", alignItems: "center", gap: "8px" }}>
          <FaTrash />
          <div>{t("INSTALL_REMOVE_RENDERER", "Remove MAKO Renderer")}</div>
        </div>
      );
    }

    return (
      <div style={{ display: "flex", alignItems: "center", gap: "8px" }}>
        <FaDownload />
        <div>{t("INSTALL_RENDERER", "Install MAKO Renderer")}</div>
      </div>
    );
  };

  return (
    <PanelSectionRow>
      <div
        className={`Mako_BrandButton${isInstalled ? " Mako_BrandButton--danger" : ""}`}
        style={{ marginTop: topMargin }}
      >
        <ButtonItem
          layout="below"
          onClick={isInstalled ? onUninstall : onInstall}
          disabled={isInstalling || isUninstalling}
        >
          {renderButtonContent()}
        </ButtonItem>
      </div>
    </PanelSectionRow>
  );
}
