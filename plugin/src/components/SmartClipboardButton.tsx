import { useState, useEffect } from "react";
import { PanelSectionRow, ButtonItem } from "@decky/ui";
import { FaClipboard, FaCheck } from "react-icons/fa";
import { DEFAULT_STEAM_LAUNCH_OPTION, getLaunchOption } from "../api/makoApi";
import { showClipboardErrorToast } from "../utils/toastUtils";
import { copyWithVerification } from "../utils/clipboardUtils";
import t from '../i18n/i18n';

export function SmartClipboardButton() {
  const [isLoading, setIsLoading] = useState(false);
  const [showSuccess, setShowSuccess] = useState(false);

  useEffect(() => {
    if (showSuccess) {
      const timer = setTimeout(() => {
        setShowSuccess(false);
      }, 3000);
      return () => clearTimeout(timer);
    }
    return undefined;
  }, [showSuccess]);

  const getLaunchOptionText = async (): Promise<string> => {
    try {
      const result = await getLaunchOption();
      return result.launch_option || DEFAULT_STEAM_LAUNCH_OPTION;
    } catch (error) {
      return DEFAULT_STEAM_LAUNCH_OPTION;
    }
  };

  const copyToClipboard = async () => {
    if (isLoading || showSuccess) return;

    setIsLoading(true);
    try {
      const text = await getLaunchOptionText();
      const { success, verified } = await copyWithVerification(text);

      if (success) {
        setShowSuccess(true);
        if (!verified) {
          console.log('Copy verification failed but copy likely worked');
        }
      } else {
        showClipboardErrorToast();
      }

    } catch (error) {
      showClipboardErrorToast();
    } finally {
      setIsLoading(false);
    }
  };

  return (
    <PanelSectionRow>
      <div className="Mako_BrandButton" style={{ marginTop: "16px" }}>
        <ButtonItem
          layout="below"
          bottomSeparator="none"
          onClick={copyToClipboard}
          disabled={isLoading || showSuccess}
        >
          <div style={{ display: "flex", alignItems: "center", gap: "8px" }}>
            {showSuccess ? (
              <FaCheck style={{ color: "#7dffac" }} />
            ) : isLoading ? (
              <FaClipboard style={{
                animation: "pulse 1s ease-in-out infinite",
                opacity: 0.7
              }} />
            ) : (
              <FaClipboard />
            )}
            <div style={{
              color: showSuccess ? "#7dffac" : "inherit",
              fontWeight: showSuccess ? "bold" : "normal"
            }}>
              {showSuccess ? t('CLIPBOARD_COPIED', 'Copied to clipboard') : isLoading ? t('CLIPBOARD_COPYING', 'Copying...') : t('CLIPBOARD_COPY_LAUNCH', 'Copy Launch Option')}
            </div>
          </div>
        </ButtonItem>
      </div>
      <style>{`
        @keyframes pulse {
          0% { opacity: 0.7; }
          50% { opacity: 1; }
          100% { opacity: 0.7; }
        }
      `}</style>
    </PanelSectionRow>
  );
}
