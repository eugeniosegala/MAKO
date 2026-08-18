import { useEffect, useState } from "react";
import { PanelSectionRow } from "@decky/ui";
import { DEFAULT_STEAM_LAUNCH_OPTION, getLaunchOption } from "../api/makoApi";
import t from "../i18n/i18n";
import { MakoSectionHeader } from "./MakoUi";

export function UsageInstructions() {
  const [launchOption, setLaunchOption] = useState(DEFAULT_STEAM_LAUNCH_OPTION);

  useEffect(() => {
    getLaunchOption()
      .then((result) => setLaunchOption(
        result.launch_option || DEFAULT_STEAM_LAUNCH_OPTION
      ))
      .catch(() => undefined);
  }, []);

  return (
    <>
      <MakoSectionHeader>
        {t("USAGE_TITLE", "Usage Instructions")}
      </MakoSectionHeader>

      <PanelSectionRow>
        <div
          style={{
            fontSize: "12px",
            lineHeight: "1.4",
            opacity: "0.8",
            whiteSpace: "pre-wrap"
          }}
        >
          {t("USAGE_DESC", "Click \"Copy Launch Option\" button, then paste it into your Steam game's launch options to enable frame generation.")}
        </div>
      </PanelSectionRow>

      <PanelSectionRow>
        <div
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
        textAlign: "center"
          }}
        >
          <strong>{launchOption}</strong>
        </div>
      </PanelSectionRow>

      <PanelSectionRow>
        <div
          style={{
            fontSize: "11px",
            lineHeight: "1.3",
            opacity: "0.6",
            marginTop: "8px"
          }}
        >
          {t('USAGE_MAKO_CONFIG_NOTE', 'MAKO uses its own Vulkan layer and configuration at ~/.config/mako-render/conf.toml. It is selected only for games launched through this command.')}
        </div>
      </PanelSectionRow>

      <PanelSectionRow>
        <div
          style={{
            fontSize: "11px",
            lineHeight: "1.3",
            opacity: "0.6",
            marginTop: "4px"
          }}
        >
          {t('USAGE_ISOLATION_NOTE', 'MAKO Renderer stays private while normal implicit Vulkan layers, including Gamescope WSI, remain discoverable. Use only one Lossless Scaling wrapper per game.')}
        </div>
      </PanelSectionRow>
    </>
  );
}
