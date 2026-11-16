import { MakoInfo } from "./MakoInfo";
import { useEffect, useState } from "react";
import { ButtonItem, PanelSectionRow, showModal } from "@decky/ui";
import { DEFAULT_STEAM_LAUNCH_OPTION, getLaunchOption } from "../api/makoApi";
import t from "../i18n/i18n";
import { MakoSectionHeader } from "./MakoUi";
import { SmartClipboardButton } from "./SmartClipboardButton";
import { LaunchOptionGamesModal } from "./LaunchOptionGamesModal";

interface UsageInstructionsProps {
  canApplyToGames?: boolean;
}

export function UsageInstructions({
  canApplyToGames = false,
}: UsageInstructionsProps = {}) {
  const [launchOption, setLaunchOption] = useState(DEFAULT_STEAM_LAUNCH_OPTION);

  useEffect(() => {
    getLaunchOption()
      .then((result) =>
        setLaunchOption(result.launch_option || DEFAULT_STEAM_LAUNCH_OPTION),
      )
      .catch(() => undefined);
  }, []);

  return (
    <>
      <MakoSectionHeader>
        {t("USAGE_TITLE", "Usage Instructions")}
      </MakoSectionHeader>

      <MakoInfo as={PanelSectionRow}>
        <div
          className="Mako_OptionDescription"
          data-mako-info="true"
          style={{
            fontSize: "12px",
            lineHeight: "1.4",
            opacity: "0.8",
            whiteSpace: "pre-wrap",
          }}
        >
          {t(
            "USAGE_DESC",
            "Add this Steam launch option to enable MAKO Frame Generation, Scaling, or both.",
          )}
        </div>
      </MakoInfo>

      <MakoInfo as={PanelSectionRow}>
        <div
          className="Mako_OptionDescription"
          data-mako-info="true"
          style={{
            fontSize: "12px",
            lineHeight: "1.4",
            opacity: "0.8",
            backgroundColor: "rgba(255, 255, 255, 0.1)",
            padding: "8px",
            borderRadius: "4px",
            fontFamily: "monospace",
            marginTop: "8px",
            marginBottom: "8px",
            textAlign: "center",
          }}
        >
          <strong>{launchOption}</strong>
        </div>
      </MakoInfo>

      <MakoInfo as={PanelSectionRow}>
        <div
          className="Mako_OptionDescription"
          data-mako-info="true"
          style={{
            fontSize: "11px",
            lineHeight: "1.3",
            opacity: "0.6",
            marginTop: "8px",
          }}
        >
          {t(
            "USAGE_MAKO_CONFIG_NOTE",
            "This command applies MAKO only to the game you launch with it.",
          )}
        </div>
      </MakoInfo>

      <MakoInfo as={PanelSectionRow}>
        <div
          className="Mako_OptionDescription"
          data-mako-info="true"
          style={{
            fontSize: "11px",
            lineHeight: "1.3",
            opacity: "0.6",
            marginTop: "4px",
          }}
        >
          {t(
            "USAGE_ISOLATION_NOTE",
            "Do not combine MAKO with another frame-generation or scaling tool for the same game.",
          )}
        </div>
      </MakoInfo>

      <SmartClipboardButton />

      {canApplyToGames && (
        <PanelSectionRow>
          <div className="Mako_BrandButton" style={{ marginTop: "8px" }}>
            <ButtonItem
              layout="below"
              bottomSeparator="none"
              onClick={() => showModal(<LaunchOptionGamesModal />)}
            >
              {t("LAUNCH_GAMES_OPEN", "Apply to Installed Games")}
            </ButtonItem>
          </div>
        </PanelSectionRow>
      )}
    </>
  );
}
