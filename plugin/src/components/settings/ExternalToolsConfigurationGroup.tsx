import { PanelSectionRow, ToggleField } from "@decky/ui";
import {
  EXTERNAL_VULKAN_LAYER_MANGOHUD,
  EXTERNAL_VULKAN_LAYER_NONE,
  EXTERNAL_VULKAN_LAYER,
} from "../../config/configSchema";
import t from "../../i18n/i18n";
import { MakoRestartLabel, MakoSectionHeader } from "../MakoUi";
import type { ConfigurationGroupProps } from "./types";
import { CollapseControl } from "./CollapseControl";

export function ExternalToolsConfigurationGroup({
  config,
  onConfigChange,
  collapsed,
  onToggle,
}: ConfigurationGroupProps) {
  return (
    <>
      <MakoSectionHeader>
        {t("CONFIG_EXTERNAL_TOOLS_TITLE", "External Tools")}
      </MakoSectionHeader>

      <CollapseControl
        containerClassName="MAKO_ExternalToolsCollapseButton_Container"
        collapsed={collapsed}
        onToggle={onToggle}
      />

      {!collapsed && (
        <>
          <PanelSectionRow>
            <ToggleField
              label={
                <MakoRestartLabel
                  label={t(
                    "CONFIG_ENABLE_MANGOHUD",
                    "Enable MangoHud (Restart)",
                  )}
                />
              }
              description={t(
                "CONFIG_ENABLE_MANGOHUD_DESC",
                "Uses the host-installed MangoHud and your existing MangoHud configuration. See the expert guide for per-game environment overrides.",
              )}
              checked={
                config.external_vulkan_layer === EXTERNAL_VULKAN_LAYER_MANGOHUD
              }
              onChange={(value) =>
                onConfigChange(
                  EXTERNAL_VULKAN_LAYER,
                  value
                    ? EXTERNAL_VULKAN_LAYER_MANGOHUD
                    : EXTERNAL_VULKAN_LAYER_NONE,
                )
              }
              bottomSeparator="none"
            />
          </PanelSectionRow>
        </>
      )}
    </>
  );
}
