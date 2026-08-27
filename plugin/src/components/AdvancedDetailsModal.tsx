import { CSSProperties, useState, useEffect } from "react";
import {
  ModalRoot,
  DialogBody,
  DialogHeader,
  DialogControlsSection,
  PanelSectionRow,
  ButtonItem,
} from "@decky/ui";
import {
  getDllStats,
  DllStatsResult,
  getConfigFileContent,
  getLaunchScriptContent,
  FileContentResult,
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
  const [dllStats, setDllStats] = useState<DllStatsResult | null>(null);
  const [configContent, setConfigContent] = useState<FileContentResult | null>(
    null,
  );
  const [scriptContent, setScriptContent] = useState<FileContentResult | null>(
    null,
  );
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    const loadData = async () => {
      try {
        setLoading(true);
        setError(null);

        // Load all data in parallel
        const [dllResult, configResult, scriptResult] = await Promise.all([
          getDllStats(),
          getConfigFileContent(),
          getLaunchScriptContent(),
        ]);

        setDllStats(dllResult);
        setConfigContent(configResult);
        setScriptContent(scriptResult);
      } catch (err) {
        setError(
          err instanceof Error
            ? err.message
            : t("ADVANCED_DETAILS_FAILED_LOAD_DATA", "Failed to load data"),
        );
      } finally {
        setLoading(false);
      }
    };

    loadData();
  }, []);

  const formatSHA256 = (hash: string) => {
    // Format SHA256 hash for better readability (add spaces every 8 characters)
    return hash.replace(/(.{8})/g, "$1 ").trim();
  };

  const copyToClipboard = async (text: string) => {
    try {
      await navigator.clipboard.writeText(text);
      // Could add a toast notification here if desired
    } catch (err) {
      console.error("Failed to copy to clipboard:", err);
    }
  };

  const copyableValueStyle: CSSProperties = {
    display: "block",
    minWidth: 0,
    maxWidth: "100%",
    overflowWrap: "anywhere",
    wordBreak: "break-word",
  };

  const pathStyle: CSSProperties = {
    ...copyableValueStyle,
    marginBottom: "9px",
    color: "#b9cbd0",
    fontSize: "12px",
    lineHeight: 1.35,
  };

  const detailLabelStyle: CSSProperties = {
    marginBottom: "4px",
    color: "#a9c4cb",
    fontSize: "11px",
    fontWeight: 600,
    lineHeight: 1.3,
    textTransform: "uppercase",
    letterSpacing: "0.35px",
  };

  const detailValueStyle: CSSProperties = {
    ...copyableValueStyle,
    color: "#edf8fb",
    fontSize: "13px",
    lineHeight: 1.4,
  };

  const codeBlockStyle: CSSProperties = {
    boxSizing: "border-box",
    width: "100%",
    maxWidth: "100%",
    maxHeight: "180px",
    margin: 0,
    padding: "8px",
    overflow: "auto",
    border: "1px solid rgba(77, 170, 190, 0.18)",
    borderRadius: "4px",
    background: "rgba(0, 10, 18, 0.42)",
    color: "#dcecef",
    fontSize: "0.8em",
    whiteSpace: "pre-wrap",
    overflowWrap: "anywhere",
    wordBreak: "break-word",
  };

  return (
    <ModalRoot closeModal={closeModal}>
      <DialogHeader>
        {t("CONTENT_ADVANCED_DETAILS", "Advanced Details")}
      </DialogHeader>
      <DialogBody>
        {loading && (
          <div
            style={{
              ...makoPanelStyle,
              margin: "8px 0 18px",
              padding: "18px",
              display: "flex",
              alignItems: "center",
              justifyContent: "center",
              gap: "9px",
              color: "#dcecef",
            }}
          >
            <MakoCompactSpinner />
            <span>
              {t("ADVANCED_DETAILS_LOADING", "Loading information...")}
            </span>
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

        {!loading && !error && (
          <MakoFocusable flow-children="column">
            <div style={{ ...makoPanelStyle, margin: "8px 0 18px" }}>
              {dllStats && (
                <>
                  <div style={makoPanelSectionHeaderStyle}>
                    {t("ADVANCED_DETAILS_LIBRARY", "Lossless Scaling Library")}
                  </div>
                  {!dllStats.success ? (
                    <div
                      style={{
                        ...makoPanelItemStyle,
                        color: "#ffb3b9",
                        overflowWrap: "anywhere",
                      }}
                    >
                      {dllStats.error ||
                        t(
                          "ADVANCED_DETAILS_FAILED_DLL_STATS",
                          "Failed to get DLL stats",
                        )}
                    </div>
                  ) : (
                    <>
                      <div style={makoPanelItemStyle}>
                        <div style={detailLabelStyle}>
                          {t("ADVANCED_DETAILS_DLL_PATH", "DLL Path")}
                        </div>
                        <MakoFocusable
                          onClick={() =>
                            dllStats.dll_path &&
                            copyToClipboard(dllStats.dll_path)
                          }
                          onActivate={() =>
                            dllStats.dll_path &&
                            copyToClipboard(dllStats.dll_path)
                          }
                          style={detailValueStyle}
                        >
                          {dllStats.dll_path ||
                            t(
                              "ADVANCED_DETAILS_NOT_AVAILABLE",
                              "Not available",
                            )}
                        </MakoFocusable>
                      </div>
                      <div style={makoPanelItemStyle}>
                        <div style={detailLabelStyle}>
                          {t("ADVANCED_DETAILS_DLL_HASH", "DLL SHA256 Hash")}
                        </div>
                        <MakoFocusable
                          onClick={() =>
                            dllStats.dll_sha256 &&
                            copyToClipboard(dllStats.dll_sha256)
                          }
                          onActivate={() =>
                            dllStats.dll_sha256 &&
                            copyToClipboard(dllStats.dll_sha256)
                          }
                          style={{
                            ...detailValueStyle,
                            fontFamily: "monospace",
                            fontSize: "12px",
                          }}
                        >
                          {dllStats.dll_sha256
                            ? formatSHA256(dllStats.dll_sha256)
                            : t(
                                "ADVANCED_DETAILS_NOT_AVAILABLE",
                                "Not available",
                              )}
                        </MakoFocusable>
                      </div>
                      {dllStats.dll_source && (
                        <div style={makoPanelItemStyle}>
                          <div style={detailLabelStyle}>
                            {t(
                              "ADVANCED_DETAILS_DETECTION_SOURCE",
                              "Detection Source",
                            )}
                          </div>
                          <div style={detailValueStyle}>
                            {dllStats.dll_source}
                          </div>
                        </div>
                      )}
                    </>
                  )}
                </>
              )}

              {scriptContent && (
                <>
                  <div
                    style={{
                      ...makoPanelSectionHeaderStyle,
                      borderTop: makoPanelDivider,
                    }}
                  >
                    {t("ADVANCED_DETAILS_LAUNCH_SCRIPT", "Launch Script")}
                  </div>
                  <div style={makoPanelItemStyle}>
                    {!scriptContent.success ? (
                      <div
                        style={{ color: "#ffb3b9", overflowWrap: "anywhere" }}
                      >
                        {t(
                          "ADVANCED_DETAILS_SCRIPT_NOT_FOUND_PREFIX",
                          "Script not found:",
                        )}{" "}
                        {scriptContent.error}
                      </div>
                    ) : (
                      <div style={{ minWidth: 0 }}>
                        <div style={pathStyle}>
                          {t("ADVANCED_DETAILS_PATH_PREFIX", "Path:")}{" "}
                          {scriptContent.path}
                        </div>
                        <MakoFocusable
                          onClick={() =>
                            scriptContent.content &&
                            copyToClipboard(scriptContent.content)
                          }
                          onActivate={() =>
                            scriptContent.content &&
                            copyToClipboard(scriptContent.content)
                          }
                        >
                          <pre style={codeBlockStyle}>
                            {scriptContent.content ||
                              t("ADVANCED_DETAILS_NO_CONTENT", "No content")}
                          </pre>
                        </MakoFocusable>
                      </div>
                    )}
                  </div>
                </>
              )}

              {configContent && (
                <>
                  <div
                    style={{
                      ...makoPanelSectionHeaderStyle,
                      borderTop: makoPanelDivider,
                    }}
                  >
                    {t("ADVANCED_DETAILS_CONFIG_FILE", "Configuration File")}
                  </div>
                  <div style={makoPanelItemStyle}>
                    {!configContent.success ? (
                      <div
                        style={{ color: "#ffb3b9", overflowWrap: "anywhere" }}
                      >
                        {t(
                          "ADVANCED_DETAILS_CONFIG_NOT_FOUND_PREFIX",
                          "Config not found:",
                        )}{" "}
                        {configContent.error}
                      </div>
                    ) : (
                      <div style={{ minWidth: 0 }}>
                        <div style={pathStyle}>
                          {t("ADVANCED_DETAILS_PATH_PREFIX", "Path:")}{" "}
                          {configContent.path}
                        </div>
                        <MakoFocusable
                          onClick={() =>
                            configContent.content &&
                            copyToClipboard(configContent.content)
                          }
                          onActivate={() =>
                            configContent.content &&
                            copyToClipboard(configContent.content)
                          }
                        >
                          <pre style={codeBlockStyle}>
                            {configContent.content ||
                              t("ADVANCED_DETAILS_NO_CONTENT", "No content")}
                          </pre>
                        </MakoFocusable>
                      </div>
                    )}
                  </div>
                </>
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
