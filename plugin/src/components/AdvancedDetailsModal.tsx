import { CSSProperties, useState, useEffect } from "react";
import {
  ModalRoot,
  DialogBody,
  DialogHeader,
  Field,
  Focusable,
  DialogControlsSection,
  PanelSectionRow,
  ButtonItem
} from "@decky/ui";
import { getDllStats, DllStatsResult, getConfigFileContent, getLaunchScriptContent, FileContentResult } from "../api/makoApi";
import t from '../i18n/i18n';

interface AdvancedDetailsModalProps {
  closeModal?: () => void;
}

export function AdvancedDetailsModal({ closeModal }: AdvancedDetailsModalProps) {
  const [dllStats, setDllStats] = useState<DllStatsResult | null>(null);
  const [configContent, setConfigContent] = useState<FileContentResult | null>(null);
  const [scriptContent, setScriptContent] = useState<FileContentResult | null>(null);
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
          getLaunchScriptContent()
        ]);

        setDllStats(dllResult);
        setConfigContent(configResult);
        setScriptContent(scriptResult);
      } catch (err) {
        setError(err instanceof Error ? err.message : t('ADVANCED_DETAILS_FAILED_LOAD_DATA', 'Failed to load data'));
      } finally {
        setLoading(false);
      }
    };

    loadData();
  }, []);

  const formatSHA256 = (hash: string) => {
    // Format SHA256 hash for better readability (add spaces every 8 characters)
    return hash.replace(/(.{8})/g, '$1 ').trim();
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
    wordBreak: "break-word"
  };

  const pathStyle: CSSProperties = {
    ...copyableValueStyle,
    marginBottom: "8px",
    fontSize: "0.9em",
    opacity: 0.8
  };

  const codeBlockStyle: CSSProperties = {
    boxSizing: "border-box",
    width: "100%",
    maxWidth: "100%",
    maxHeight: "180px",
    margin: 0,
    padding: "8px",
    overflow: "auto",
    borderRadius: "4px",
    background: "rgba(255, 255, 255, 0.1)",
    fontSize: "0.8em",
    whiteSpace: "pre-wrap",
    overflowWrap: "anywhere",
    wordBreak: "break-word"
  };

  return (
    <ModalRoot closeModal={closeModal}>
      <DialogHeader>{t("CONTENT_ADVANCED_DETAILS", "Advanced Details")}</DialogHeader>
      <DialogBody>
        {loading && (
          <div>{t('ADVANCED_DETAILS_LOADING', 'Loading information...')}</div>
        )}

        {error && (
          <div style={copyableValueStyle}>{t('ADVANCED_DETAILS_ERROR_PREFIX', 'Error:')} {error}</div>
        )}

        {!loading && !error && (
          <Focusable flow-children="vertical">
          {/* DLL Stats Section */}
          {dllStats && (
            <>
              {!dllStats.success ? (
                <div style={copyableValueStyle}>{dllStats.error || t('ADVANCED_DETAILS_FAILED_DLL_STATS', 'Failed to get DLL stats')}</div>
              ) : (
                <div>
                  <Field label={t('ADVANCED_DETAILS_DLL_PATH', 'DLL Path')}>
                    <Focusable
                      onClick={() => dllStats.dll_path && copyToClipboard(dllStats.dll_path)}
                      onActivate={() => dllStats.dll_path && copyToClipboard(dllStats.dll_path)}
                      style={copyableValueStyle}
                    >
                      {dllStats.dll_path || t('ADVANCED_DETAILS_NOT_AVAILABLE', 'Not available')}
                    </Focusable>
                  </Field>

                  <Field label={t('ADVANCED_DETAILS_DLL_HASH', 'DLL SHA256 Hash')}>
                    <Focusable
                      onClick={() => dllStats.dll_sha256 && copyToClipboard(dllStats.dll_sha256)}
                      onActivate={() => dllStats.dll_sha256 && copyToClipboard(dllStats.dll_sha256)}
                      style={copyableValueStyle}
                    >
                      {dllStats.dll_sha256 ? formatSHA256(dllStats.dll_sha256) : t('ADVANCED_DETAILS_NOT_AVAILABLE', 'Not available')}
                    </Focusable>
                  </Field>

                  {dllStats.dll_source && (
                    <Field label={t('ADVANCED_DETAILS_DETECTION_SOURCE', 'Detection Source')}>
                      <div style={copyableValueStyle}>{dllStats.dll_source}</div>
                    </Field>
                  )}
                </div>
              )}
            </>
          )}

          {/* Launch Script Section */}
          {scriptContent && (
            <Field label={t('ADVANCED_DETAILS_LAUNCH_SCRIPT', 'Launch Script')}>
              {!scriptContent.success ? (
                <div style={copyableValueStyle}>{t('ADVANCED_DETAILS_SCRIPT_NOT_FOUND_PREFIX', 'Script not found:')} {scriptContent.error}</div>
              ) : (
                <div style={{ minWidth: 0 }}>
                  <div style={pathStyle}>
                    {t('ADVANCED_DETAILS_PATH_PREFIX', 'Path:')} {scriptContent.path}
                  </div>
                  <Focusable
                    onClick={() => scriptContent.content && copyToClipboard(scriptContent.content)}
                    onActivate={() => scriptContent.content && copyToClipboard(scriptContent.content)}
                  >
                    <pre style={codeBlockStyle}>
                      {scriptContent.content || t('ADVANCED_DETAILS_NO_CONTENT', 'No content')}
                    </pre>
                  </Focusable>
                </div>
              )}
            </Field>
          )}

          {/* Config File Section */}
          {configContent && (
            <Field label={t('ADVANCED_DETAILS_CONFIG_FILE', 'Configuration File')}>
              {!configContent.success ? (
                <div style={copyableValueStyle}>{t('ADVANCED_DETAILS_CONFIG_NOT_FOUND_PREFIX', 'Config not found:')} {configContent.error}</div>
              ) : (
                <div style={{ minWidth: 0 }}>
                  <div style={pathStyle}>
                    {t('ADVANCED_DETAILS_PATH_PREFIX', 'Path:')} {configContent.path}
                  </div>
                  <Focusable
                    onClick={() => configContent.content && copyToClipboard(configContent.content)}
                    onActivate={() => configContent.content && copyToClipboard(configContent.content)}
                  >
                    <pre style={codeBlockStyle}>
                      {configContent.content || t('ADVANCED_DETAILS_NO_CONTENT', 'No content')}
                    </pre>
                  </Focusable>
                </div>
              )}
            </Field>
          )}

          {/* Close Button */}
          <DialogControlsSection>
            <PanelSectionRow>
              <ButtonItem
                layout="below"
                onClick={closeModal}
              >
                {t('ADVANCED_DETAILS_CLOSE', 'Close')}
              </ButtonItem>
            </PanelSectionRow>
          </DialogControlsSection>
          </Focusable>
        )}
      </DialogBody>
    </ModalRoot>
  );
}
