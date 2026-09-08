import { PanelSectionRow, TextField } from "@decky/ui";
import { ACTIVE_IN, DLL, GPU } from "../../config/configSchema";
import t from "../../i18n/i18n";
import { MakoRestartLabel, MakoSectionHeader } from "../MakoUi";
import type { ConfigurationGroupProps } from "./types";
import { CollapseControl } from "./CollapseControl";

export function ManualOverridesConfigurationGroup({
  config,
  onConfigChange,
  collapsed,
  onToggle,
}: ConfigurationGroupProps) {
  return (
    <>
      <MakoSectionHeader>
        {t("CONFIG_MANUAL_OVERRIDES_TITLE", "Manual Overrides")}
      </MakoSectionHeader>

      <CollapseControl
        containerClassName="MAKO_ManualOverridesCollapseButton_Container"
        collapsed={collapsed}
        onToggle={onToggle}
      />

      {!collapsed && (
        <PanelSectionRow>
          <div className="MAKO_ManualOverrideFields">
            <TextField
              label={
                <MakoRestartLabel
                  label={t("CONFIG_DLL_PATH", "Lossless.dll Path (Restart)")}
                />
              }
              description={t(
                "CONFIG_DLL_PATH_DESC",
                "Optional full path to Lossless.dll. Leave blank to use MAKO Renderer automatic discovery.",
              )}
              value={config.dll}
              onChange={(event) =>
                onConfigChange(DLL, event.currentTarget.value)
              }
            />

            <TextField
              label={
                <MakoRestartLabel label={t("CONFIG_GPU", "GPU (Restart)")} />
              }
              description={
                <span className="MAKO_GpuDescription">
                  {t(
                    "CONFIG_GPU_DESC",
                    "Optional GPU name, vendor:device ID, or PCI bus ID. Restart the game after changing it.",
                  )}
                </span>
              }
              value={config.gpu}
              onChange={(event) =>
                onConfigChange(GPU, event.currentTarget.value)
              }
            />

            <TextField
              label={t("CONFIG_ACTIVE_IN", "Matched Processes")}
              description={t(
                "CONFIG_ACTIVE_IN_DESC",
                "Executable or process names separated by commas. Running-game capture fills these automatically; edit them only when a launcher or emulator needs an additional process alias.",
              )}
              value={config.active_in}
              onChange={(event) =>
                onConfigChange(ACTIVE_IN, event.currentTarget.value)
              }
            />
          </div>
        </PanelSectionRow>
      )}
    </>
  );
}
