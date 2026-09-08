import { PanelSectionRow, ToggleField } from "@decky/ui";
import {
  EXTERNAL_VULKAN_LAYER_MANGOHUD,
  EXTERNAL_VULKAN_LAYER_NONE,
  EXTERNAL_VULKAN_LAYER_VKBASALT,
  EXTERNAL_VULKAN_LAYER,
} from "../../config/configSchema";
import t from "../../i18n/i18n";
import {
  MakoExperimentalSettingLabel,
  MakoRestartLabel,
  MakoSectionHeader,
} from "../MakoUi";
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
            />
          </PanelSectionRow>

          <PanelSectionRow>
            <ToggleField
              label={
                <MakoExperimentalSettingLabel
                  label={t(
                    "CONFIG_ENABLE_VKBASALT",
                    "Enable vkBasalt (Restart)",
                  )}
                  badgeLabel={t("EXPERIMENTAL_LABEL", "Experimental")}
                />
              }
              description={t(
                "CONFIG_ENABLE_VKBASALT_DESC",
                "Keep it off unless you are testing vkBasalt with this game. Uses a host-installed vkBasalt layer for this profile. The initial test lane is limited to 64-bit native Vulkan or Proton games launched directly by Steam on SteamOS.",
              )}
              bottomSeparator="none"
              checked={
                config.external_vulkan_layer === EXTERNAL_VULKAN_LAYER_VKBASALT
              }
              onChange={(value) =>
                onConfigChange(
                  EXTERNAL_VULKAN_LAYER,
                  value
                    ? EXTERNAL_VULKAN_LAYER_VKBASALT
                    : EXTERNAL_VULKAN_LAYER_NONE,
                )
              }
            />
          </PanelSectionRow>
        </>
      )}
    </>
  );
}
