import { useState, useEffect } from "react";
import { PanelSectionRow, ButtonItem } from "@decky/ui";
import { FaClipboard, FaCheck } from "react-icons/fa";
import {
  checkFgmodDirectory,
  DEFAULT_STEAM_LAUNCH_OPTION,
  getLaunchOption,
} from "../api/makoApi";
import { useClipboardFeedback } from "../hooks/useClipboardFeedback";
import t from "../i18n/i18n";

export function FgmodClipboardButton() {
  const [fgmodExists, setFgmodExists] = useState(false);
  const [checkingFgmod, setCheckingFgmod] = useState(true);

  // Check for fgmod directory on component mount
  useEffect(() => {
    const checkFgmod = async () => {
      try {
        const result = await checkFgmodDirectory();
        setFgmodExists(result.exists);
      } catch (error) {
        console.error("Error checking fgmod directory:", error);
        setFgmodExists(false);
      } finally {
        setCheckingFgmod(false);
      }
    };

    checkFgmod();
  }, []);

  const getFgmodLaunchOptionText = async () => {
    const launchOption = await getLaunchOption();
    return `~/fgmod/fgmod ${launchOption.launch_option || DEFAULT_STEAM_LAUNCH_OPTION}`;
  };
  const { isLoading, showSuccess, copyToClipboard } = useClipboardFeedback(
    getFgmodLaunchOptionText,
  );

  // Don't render if fgmod directory doesn't exist or we're still checking
  if (checkingFgmod || !fgmodExists) {
    return null;
  }

  return (
    <PanelSectionRow>
      <div className="Mako_BrandButton">
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
              <FaClipboard
                style={{
                  animation: "pulse 1s ease-in-out infinite",
                  opacity: 0.7,
                }}
              />
            ) : (
              <FaClipboard />
            )}
            <div
              style={{
                color: showSuccess ? "#7dffac" : "inherit",
                fontWeight: showSuccess ? "bold" : "normal",
              }}
            >
              {showSuccess
                ? t("CLIPBOARD_COPIED", "Copied to clipboard")
                : isLoading
                  ? t("CLIPBOARD_COPYING", "Copying...")
                  : t("CLIPBOARD_MAKO_FGMOD", "MAKO + DeckyFG")}
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
