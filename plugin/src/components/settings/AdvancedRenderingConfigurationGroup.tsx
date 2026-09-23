import { PanelSectionRow, SliderField, ToggleField } from "@decky/ui";
import {
  ADAPTIVE_MINIMUM_BASE_FPS,
  BASE_FPS_CAP_MIN,
  BASE_FPS_CAP_UI_MAX,
  DISABLE_MAKO,
  FRAME_GENERATION_REFRESH_THRESHOLD,
  FRAME_GENERATION_REFRESH_THRESHOLD_MAX,
  FRAME_GENERATION_REFRESH_THRESHOLD_PRESET,
  FRAME_GENERATION_REFRESH_THRESHOLD_UI_MIN,
} from "../../config/configSchema";
import { baseFpsCapChanges } from "../../config/fractionalAdaptivePreset";
import t from "../../i18n/i18n";
import { MakoSectionHeader, MakoSettingRelationship } from "../MakoUi";
import type { ConfigurationUpdateGroupProps } from "./types";
import { CollapseControl } from "./CollapseControl";

export function AdvancedRenderingConfigurationGroup({
  config,
  onConfigChange,
  onConfigUpdate,
  collapsed,
  onToggle,
}: ConfigurationUpdateGroupProps) {
  const steadyBaseFpsCap = Math.max(
    ADAPTIVE_MINIMUM_BASE_FPS,
    config.target_fps / 2,
  );
  const steadyBaseFpsCapLabel = Number.isInteger(steadyBaseFpsCap)
    ? steadyBaseFpsCap.toFixed(0)
    : steadyBaseFpsCap.toFixed(1);

  return (
    <>
      <MakoSectionHeader>
        {t("CONFIG_SECTION_TITLE", "Advanced Rendering Settings")}
      </MakoSectionHeader>

      <CollapseControl
        containerClassName="MAKO_ConfigCollapseButton_Container"
        collapsed={collapsed}
        onToggle={onToggle}
      />

      {!collapsed && (
        <>
          <PanelSectionRow>
            <SliderField
              label={`${t("CONFIG_BASE_FPS_CAP", "Base FPS Cap")}${config.base_fps_cap > 0 ? ` (${config.base_fps_cap} FPS)` : ` (${t("CONFIG_BASE_FPS_CAP_OFF", "Off")})`}`}
              description={
                <>
                  <div>
                    {t(
                      "CONFIG_BASE_FPS_CAP_DESC",
                      "Caps real application frames before frame generation. Works with DirectX, OpenGL through Zink, and Vulkan.",
                    )}
                  </div>
                  {config.adaptive && config.adaptive_auto_base_fps_cap ? (
                    <MakoSettingRelationship>
                      {t(
                        "CONFIG_BASE_FPS_CAP_STEADY_RELATION",
                        "Controlled by Steady Base Cap ({fps} FPS). Your manual value remains saved.",
                        { fps: steadyBaseFpsCapLabel },
                      )}
                    </MakoSettingRelationship>
                  ) : config.dynamic_cadence_recovery ? (
                    <MakoSettingRelationship>
                      {t(
                        "CONFIG_BASE_FPS_CAP_RECOVERY_RELATION",
                        "Changing this cap turns Dynamic Cadence Recovery off.",
                      )}
                    </MakoSettingRelationship>
                  ) : null}
                </>
              }
              value={config.base_fps_cap}
              min={BASE_FPS_CAP_MIN}
              max={BASE_FPS_CAP_UI_MAX}
              step={1}
              disabled={
                !config.frame_generation_enabled ||
                (config.adaptive && config.adaptive_auto_base_fps_cap)
              }
              onChange={(value) => onConfigUpdate(baseFpsCapChanges(value))}
            />
          </PanelSectionRow>

          <PanelSectionRow>
            <ToggleField
              label={t(
                "CONFIG_DISABLE_MAKO_NEXT_LAUNCH",
                "Disable MAKO Renderer on Next Launch",
              )}
              description={t(
                "CONFIG_DISABLE_MAKO_NEXT_LAUNCH_DESC",
                "Troubleshooting only. Stops MAKO Renderer loading the next time the game starts. Use Frame Generation above to switch synthesis on or off.",
              )}
              checked={config.disable_mako}
              onChange={(value) => onConfigChange(DISABLE_MAKO, value)}
            />
          </PanelSectionRow>

          <PanelSectionRow>
            <ToggleField
              label={t(
                "CONFIG_FRAME_GENERATION_REFRESH_GUARD",
                "Auto-disable Frame Generation by Refresh Rate",
              )}
              description={t(
                "CONFIG_FRAME_GENERATION_REFRESH_GUARD_DESC",
                "Pauses frame generation when Gamescope confirms the current display is at or below the threshold, then resumes your selected mode above it. Does nothing when refresh feedback is unavailable.",
              )}
              bottomSeparator={
                config.frame_generation_refresh_threshold > 0
                  ? undefined
                  : "none"
              }
              checked={config.frame_generation_refresh_threshold > 0}
              onChange={(value) =>
                onConfigChange(
                  FRAME_GENERATION_REFRESH_THRESHOLD,
                  value ? FRAME_GENERATION_REFRESH_THRESHOLD_PRESET : 0,
                )
              }
            />
          </PanelSectionRow>

          {config.frame_generation_refresh_threshold > 0 && (
            <PanelSectionRow>
              <SliderField
                label={`${t(
                  "CONFIG_FRAME_GENERATION_REFRESH_THRESHOLD",
                  "Refresh Rate Threshold",
                )} (${config.frame_generation_refresh_threshold} Hz)`}
                description={t(
                  "CONFIG_FRAME_GENERATION_REFRESH_THRESHOLD_DESC",
                  "Choose the highest refresh rate where frame generation should remain paused.",
                )}
                value={config.frame_generation_refresh_threshold}
                min={FRAME_GENERATION_REFRESH_THRESHOLD_UI_MIN}
                max={FRAME_GENERATION_REFRESH_THRESHOLD_MAX}
                step={1}
                onChange={(value) =>
                  onConfigChange(FRAME_GENERATION_REFRESH_THRESHOLD, value)
                }
              />
            </PanelSectionRow>
          )}
        </>
      )}
    </>
  );
}
