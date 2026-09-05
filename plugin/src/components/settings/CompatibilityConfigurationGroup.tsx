import { Dropdown, Field, PanelSectionRow, ToggleField } from "@decky/ui";
import {
  DISABLE_STEAMDECK_MODE,
  DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS,
  DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_VALUES,
  ENABLE_ZINK,
  FORCE_ALSA_AUDIO,
  GAMESCOPE_WSI_COMPATIBILITY,
  SWAPCHAIN_IMAGE_COUNT_COMPATIBILITY,
} from "../../config/configSchema";
import { dynamicCadenceRecoveryChanges } from "../../config/fractionalAdaptivePreset";
import t from "../../i18n/i18n";
import {
  MakoInlineTip,
  MakoRestartLabel,
  MakoSectionHeader,
  MakoSettingRelationship,
} from "../MakoUi";
import type { ConfigurationUpdateGroupProps } from "./types";
import { CollapseControl } from "./CollapseControl";

export function CompatibilityConfigurationGroup({
  config,
  onConfigChange,
  onConfigUpdate,
  collapsed,
  onToggle,
}: ConfigurationUpdateGroupProps) {
  const cadenceProbeInterval = config.dynamic_cadence_probe_interval_seconds;
  const cadenceProbeIntervalValues =
    DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_VALUES.includes(
      cadenceProbeInterval as (typeof DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_VALUES)[number],
    )
      ? [...DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_VALUES]
      : [
          ...DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_VALUES,
          cadenceProbeInterval,
        ].sort((left, right) => left - right);
  const cadenceProbeIntervalOptions = cadenceProbeIntervalValues.map(
    (value) => ({ data: value, label: `${value}s` }),
  );

  return (
    <>
      <MakoSectionHeader>
        {t("CONFIG_WORKAROUNDS_TITLE", "Compatibility Settings")}
      </MakoSectionHeader>

      <CollapseControl
        containerClassName="MAKO_WorkaroundsCollapseButton_Container"
        collapsed={collapsed}
        onToggle={onToggle}
      />

      {!collapsed && (
        <>
          <PanelSectionRow>
            <ToggleField
              label={t("CONFIG_DISABLE_HDR_EXPOSURE", "Disable HDR")}
              description={t(
                "CONFIG_DISABLE_HDR_EXPOSURE_DESC",
                "HDR is unavailable in this release. This required setting keeps the stable SDR path active.",
              )}
              checked={true}
              disabled={true}
              onChange={() => undefined}
            />
          </PanelSectionRow>

          <PanelSectionRow>
            <ToggleField
              label={t("DYNAMIC_CADENCE_RECOVERY", "Dynamic Cadence Recovery")}
              description={
                <>
                  <div>
                    {t(
                      "DYNAMIC_CADENCE_RECOVERY_DESC",
                      "Helps games and emulators that switch native rates, such as 30 FPS gameplay and 60 FPS menus. It periodically checks for a rate change and recovers the correct cadence, but each check can briefly affect pacing. Enable it only for affected games.",
                    )}
                  </div>
                  <MakoSettingRelationship>
                    {t(
                      "DYNAMIC_CADENCE_RECOVERY_RELATION",
                      "Turning this on disables Steady Base Cap and Base FPS Cap. Changing either cap later turns Recovery off.",
                    )}
                  </MakoSettingRelationship>
                </>
              }
              checked={config.dynamic_cadence_recovery}
              onChange={(value) =>
                onConfigUpdate(dynamicCadenceRecoveryChanges(value))
              }
            />
          </PanelSectionRow>

          {config.dynamic_cadence_recovery && (
            <PanelSectionRow>
              <Field
                label={t(
                  "DYNAMIC_CADENCE_PROBE_INTERVAL",
                  "Cadence Probe Interval",
                )}
                description={
                  <span style={{ display: "block", paddingBottom: "6px" }}>
                    {t(
                      "DYNAMIC_CADENCE_PROBE_INTERVAL_DESC",
                      "How often Recovery tests the native frame rate. 0.1 seconds is aggressive and may cause frequent brief pacing hitches; 2 seconds is the default, while 3 seconds checks least often. Test per game.",
                    )}
                  </span>
                }
                childrenLayout="below"
                childrenContainerWidth="max"
              >
                <Dropdown
                  rgOptions={cadenceProbeIntervalOptions}
                  selectedOption={cadenceProbeInterval}
                  onChange={(option) =>
                    onConfigChange(
                      DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS,
                      Number(option.data),
                    )
                  }
                />
              </Field>
            </PanelSectionRow>
          )}

          <PanelSectionRow>
            <ToggleField
              label={
                <MakoRestartLabel
                  label={t(
                    "CONFIG_GAMESCOPE_WSI_COMPATIBILITY",
                    "Gamescope WSI (Restart)",
                  )}
                />
              }
              description={
                <>
                  <div>
                    {t(
                      "CONFIG_GAMESCOPE_WSI_COMPATIBILITY_DESC",
                      "May reduce coloured or pixelated motion artifacts in some games by using Gamescope's presentation path. Scaling enables it automatically. For FG-only profiles, enable it only for affected games.",
                    )}
                  </div>
                  {!config.scaling_enabled && (
                    <MakoInlineTip tone="warning">
                      {t(
                        "CONFIG_GAMESCOPE_WSI_COMPATIBILITY_WARNING",
                        "This compatibility path is limited to supported 64-bit host launches. Leave it off when the game does not need it, as it may impact performance.",
                      )}
                    </MakoInlineTip>
                  )}
                </>
              }
              checked={
                config.scaling_enabled || config.gamescope_wsi_compatibility
              }
              disabled={config.scaling_enabled}
              onChange={(value) =>
                onConfigChange(GAMESCOPE_WSI_COMPATIBILITY, value)
              }
            />
          </PanelSectionRow>

          <PanelSectionRow>
            <ToggleField
              label={
                <MakoRestartLabel
                  label={t(
                    "CONFIG_SWAPCHAIN_IMAGE_COUNT_COMPATIBILITY",
                    "Game Swapchain Images (Restart)",
                  )}
                />
              }
              description={
                <>
                  <div>
                    {t(
                      "CONFIG_SWAPCHAIN_IMAGE_COUNT_COMPATIBILITY_DESC",
                      "Can fix games that fail to start with Frame Generation by preserving the game's requested swapchain image minimum. Enable it only for affected games.",
                    )}
                  </div>
                  <MakoInlineTip tone="warning">
                    {t(
                      "CONFIG_SWAPCHAIN_IMAGE_COUNT_COMPATIBILITY_WARNING",
                      "Generated frames may be skipped when the compositor has no spare image, which can reduce smoothness or performance under pressure.",
                    )}
                  </MakoInlineTip>
                </>
              }
              checked={config.swapchain_image_count_compatibility}
              onChange={(value) =>
                onConfigChange(SWAPCHAIN_IMAGE_COUNT_COMPATIBILITY, value)
              }
            />
          </PanelSectionRow>

          <PanelSectionRow>
            <ToggleField
              label={
                <MakoRestartLabel
                  label={t(
                    "CONFIG_DISABLE_STEAMDECK_MODE",
                    "Disable Steam Deck Mode (Restart)",
                  )}
                />
              }
              description={t(
                "CONFIG_DISABLE_STEAMDECK_MODE_DESC",
                "Disables Steam Deck mode. Unlocks hidden settings in some games.",
              )}
              checked={config.disable_steamdeck_mode}
              onChange={(value) =>
                onConfigChange(DISABLE_STEAMDECK_MODE, value)
              }
            />
          </PanelSectionRow>

          <PanelSectionRow>
            <ToggleField
              label={
                <MakoRestartLabel
                  label={t(
                    "CONFIG_ENABLE_ZINK",
                    "Enable Zink for OpenGL Games (Restart)",
                  )}
                />
              }
              description={t(
                "CONFIG_ENABLE_ZINK_DESC",
                "Uses the Vulkan-based OpenGL implementation for OpenGL games. May cause crashes or freezes in some games.",
              )}
              checked={config.enable_zink}
              onChange={(value) => onConfigChange(ENABLE_ZINK, value)}
            />
          </PanelSectionRow>

          <PanelSectionRow>
            <ToggleField
              label={
                <MakoRestartLabel
                  label={t(
                    "CONFIG_FORCE_ALSA_AUDIO",
                    "Force ALSA Audio (Restart)",
                  )}
                />
              }
              description={t(
                "CONFIG_FORCE_ALSA_AUDIO_DESC",
                "May improve compatibility with modes such as Zink and reduce audio stuttering or sudden loud sounds. Disable to restore normal audio defaults.",
              )}
              bottomSeparator="none"
              checked={config.force_alsa_audio}
              onChange={(value) => onConfigChange(FORCE_ALSA_AUDIO, value)}
            />
          </PanelSectionRow>
        </>
      )}
    </>
  );
}
