import { PanelSectionRow, ToggleField } from "@decky/ui";
import {
  EXTERNAL_VULKAN_LAYER_MANGOHUD,
  EXTERNAL_VULKAN_LAYER_NONE,
  EXTERNAL_VULKAN_LAYER,
  MANGOHUD_COUNT_REAL_FRAMES,
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

      {onToggle && (
        <CollapseControl
          containerClassName="MAKO_ExternalToolsCollapseButton_Container"
          collapsed={collapsed}
          onToggle={onToggle}
        />
      )}

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
                "Uses installed MangoHud. To share a layout across enabled profiles, create ~/.config/mako-render/mangohud.conf with MangoHud options and restart the game.",
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

          {config.external_vulkan_layer === EXTERNAL_VULKAN_LAYER_MANGOHUD && (
            <PanelSectionRow>
              <ToggleField
                label={
                  <MakoRestartLabel
                    label={t(
                      "CONFIG_MANGOHUD_COUNT_REAL_FRAMES",
                      "Count Real Frames Only (Restart)",
                    )}
                  />
                }
                description={t(
                  "CONFIG_MANGOHUD_COUNT_REAL_FRAMES_DESC",
                  "Off counts every output frame, including generated ones. On shows the game's real FPS instead, but the HUD then passes through Frame Generation and Scaling.",
                )}
                checked={config.mangohud_count_real_frames}
                onChange={(value) =>
                  onConfigChange(MANGOHUD_COUNT_REAL_FRAMES, value)
                }
                bottomSeparator="none"
              />
            </PanelSectionRow>
          )}
        </>
      )}
    </>
  );
}
