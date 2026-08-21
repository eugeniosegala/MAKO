import { PanelSectionRow, ButtonItem } from "@decky/ui";
import { FaClipboard, FaCheck } from "react-icons/fa";
import { DEFAULT_STEAM_LAUNCH_OPTION, getLaunchOption } from "../api/makoApi";
import { useClipboardFeedback } from "../hooks/useClipboardFeedback";
import t from "../i18n/i18n";

export function SmartClipboardButton() {
  const getLaunchOptionText = async (): Promise<string> => {
    try {
      const result = await getLaunchOption();
      return result.launch_option || DEFAULT_STEAM_LAUNCH_OPTION;
    } catch (error) {
      return DEFAULT_STEAM_LAUNCH_OPTION;
    }
  };
  const { isLoading, showSuccess, copyToClipboard } =
    useClipboardFeedback(getLaunchOptionText);

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
                  : t("CLIPBOARD_COPY_LAUNCH", "Copy Launch Option")}
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
