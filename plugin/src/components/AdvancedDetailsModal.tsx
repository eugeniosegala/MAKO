import { CSSProperties, useEffect, useState } from "react";
import {
  ModalRoot,
  DialogBody,
  DialogHeader,
  DialogControlsSection,
  PanelSectionRow,
  ButtonItem,
} from "@decky/ui";
import {
  checkLosslessScalingDll,
  checkMakoInstalled,
  type DllDetectionResult,
  type InstallationStatus,
} from "../api/makoApi";
import t from "../i18n/i18n";
import {
  MakoCompactSpinner,
  MakoFocusable,
  makoPanelDivider,
  makoPanelItemStyle,
  makoPanelSectionHeaderStyle,
  makoPanelStyle,
} from "./MakoUi";

interface AdvancedDetailsModalProps {
  closeModal?: () => void;
}

export function AdvancedDetailsModal({
  closeModal,
}: AdvancedDetailsModalProps) {
  const [installation, setInstallation] = useState<InstallationStatus | null>(
    null,
  );
  const [dll, setDll] = useState<DllDetectionResult | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let active = true;
    Promise.all([checkMakoInstalled(), checkLosslessScalingDll()])
      .then(([installationResult, dllResult]) => {
        if (active) {
          setInstallation(installationResult);
          setDll(dllResult);
        }
      })
      .catch((err) => {
        if (active) {
          setError(
            err instanceof Error
              ? err.message
              : t("ADVANCED_DETAILS_FAILED_LOAD_DATA", "Failed to load data"),
          );
        }
      })
      .finally(() => {
        if (active) setLoading(false);
      });
    return () => {
      active = false;
    };
  }, []);

  const copyToClipboard = async (value: string) => {
    try {
      await navigator.clipboard.writeText(value);
    } catch (err) {
      console.error("Failed to copy to clipboard:", err);
    }
  };

  const valueStyle: CSSProperties = {
    display: "block",
    boxSizing: "border-box",
    minWidth: 0,
    width: "100%",
    maxWidth: "100%",
    padding: "8px 10px",
    overflowWrap: "anywhere",
    wordBreak: "break-word",
    userSelect: "text",
    border: "1px solid rgba(77, 170, 190, 0.18)",
    borderRadius: "4px",
    background: "rgba(0, 10, 18, 0.3)",
    color: "#edf8fb",
    fontSize: "13px",
    lineHeight: 1.4,
  };
  const labelStyle: CSSProperties = {
    marginBottom: "4px",
    color: "#a9c4cb",
    fontSize: "11px",
    fontWeight: 600,
    textTransform: "uppercase",
    letterSpacing: "0.35px",
  };
  const detail = (label: string, value: string | null | undefined) => {
    const displayedValue =
      value || t("ADVANCED_DETAILS_NOT_AVAILABLE", "Not available");
    return (
      <div style={makoPanelItemStyle}>
        <div style={labelStyle}>{label}</div>
        <MakoFocusable
          onClick={() => void copyToClipboard(displayedValue)}
          onActivate={() => void copyToClipboard(displayedValue)}
          style={valueStyle}
        >
          {displayedValue}
        </MakoFocusable>
      </div>
    );
  };

  const installationSummary = installation?.error
    ? installation.error
    : installation?.host_architecture_supported === false
      ? t("ADVANCED_DETAILS_UNSUPPORTED_HOST", "Unsupported host")
      : installation?.installed
        ? installation.engine_update_required
          ? t("ADVANCED_DETAILS_UPDATE_REQUIRED", "Bundled update available")
          : t("STATUS_ENGINE_INSTALLED", "MAKO Renderer installed")
        : installation &&
            (installation.lib_exists ||
              installation.json_exists ||
              installation.script_exists)
          ? t("ADVANCED_DETAILS_INCOMPLETE", "Incomplete installation")
          : t("STATUS_ENGINE_NOT_INSTALLED", "MAKO Renderer not installed");

  return (
    <ModalRoot closeModal={closeModal}>
      <DialogHeader>
        {t("CONTENT_ADVANCED_DETAILS", "Advanced Details")}
      </DialogHeader>
      <DialogBody>
        {loading && (
          <div
            style={{ ...makoPanelStyle, margin: "8px 0 18px", padding: "18px" }}
          >
            <MakoCompactSpinner />{" "}
            {t("ADVANCED_DETAILS_LOADING", "Loading information...")}
          </div>
        )}
        {error && (
          <div
            style={{
              ...makoPanelStyle,
              margin: "8px 0 18px",
              padding: "14px",
              color: "#ffb3b9",
            }}
          >
            {t("ADVANCED_DETAILS_ERROR_PREFIX", "Error:")} {error}
          </div>
        )}
        {!loading && !error && installation && dll && (
          <MakoFocusable flow-children="column">
            <div style={{ ...makoPanelStyle, margin: "8px 0 18px" }}>
              <div style={makoPanelSectionHeaderStyle}>
                {t("ADVANCED_DETAILS_RENDERER", "MAKO Renderer")}
              </div>
              {detail(
                t("ADVANCED_DETAILS_INSTALLATION", "Installation"),
                installationSummary,
              )}
              {detail(
                t("ADVANCED_DETAILS_INSTALLED_VERSION", "Installed version"),
                installation.installed && installation.engine_version_known
                  ? installation.installed_engine_version
                  : null,
              )}
              {detail(
                t("ADVANCED_DETAILS_BUNDLED_VERSION", "Bundled version"),
                installation.expected_engine_version,
              )}
              {detail(
                t("ADVANCED_DETAILS_HOST_ARCHITECTURE", "Host architecture"),
                installation.host_architecture,
              )}
              {installation.lib_exists &&
                detail(
                  t("ADVANCED_DETAILS_LAYER_PATH", "Renderer library"),
                  installation.lib_path,
                )}
              {installation.json_exists &&
                detail(
                  t("ADVANCED_DETAILS_MANIFEST_PATH", "Vulkan manifest"),
                  installation.json_path,
                )}
              {installation.script_exists &&
                detail(
                  t("ADVANCED_DETAILS_LAUNCHER_PATH", "Launch wrapper"),
                  installation.script_path,
                )}

              <div
                style={{
                  ...makoPanelSectionHeaderStyle,
                  borderTop: makoPanelDivider,
                }}
              >
                {t("ADVANCED_DETAILS_LIBRARY", "Lossless Scaling Library")}
              </div>
              {detail(
                t("ADVANCED_DETAILS_DETECTION", "Detection"),
                dll.detected
                  ? t("STATUS_LOSSLESS_INSTALLED", "Lossless Scaling installed")
                  : dll.error ||
                      t(
                        "ADVANCED_DETAILS_DLL_NOT_DETECTED",
                        "Lossless Scaling not detected",
                      ),
              )}
              {dll.detected &&
                detail(t("ADVANCED_DETAILS_DLL_PATH", "DLL Path"), dll.path)}
              {dll.detected &&
                dll.source &&
                detail(
                  t("ADVANCED_DETAILS_DETECTION_SOURCE", "Detection Source"),
                  dll.source,
                )}
            </div>
            <DialogControlsSection>
              <PanelSectionRow>
                <div className="Mako_BrandButton">
                  <ButtonItem layout="below" onClick={closeModal}>
                    {t("ADVANCED_DETAILS_CLOSE", "Close")}
                  </ButtonItem>
                </div>
              </PanelSectionRow>
            </DialogControlsSection>
          </MakoFocusable>
        )}
      </DialogBody>
    </ModalRoot>
  );
}
