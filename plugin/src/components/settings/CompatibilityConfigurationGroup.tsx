import { Dropdown, Field, PanelSectionRow, ToggleField } from "@decky/ui";
import {
  DISABLE_HDR_EXPOSURE,
  HDR_QUALITY,
  DISABLE_STEAMDECK_MODE,
  DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS,
  DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_VALUES,
  ENABLE_ZINK,
  FORCE_ALSA_AUDIO,
  GAMESCOPE_WSI_COMPATIBILITY,
  MAILBOX_PRESENTATION_COMPATIBILITY,
  SWAPCHAIN_IMAGE_COUNT_COMPATIBILITY,
} from "../../config/configSchema";
import { dynamicCadenceRecoveryChanges } from "../../config/fractionalAdaptivePreset";
import t from "../../i18n/i18n";
import {
  MakoExperimentalSettingLabel,
  MakoInlineTip,
  MakoRestartLabel,
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
  // Keep the persisted disable flag compatible with existing profiles.
  const hdrEnabled = config.disable_hdr_exposure === false;
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
      {onToggle && (
        <CollapseControl
          containerClassName="MAKO_WorkaroundsCollapseButton_Container"
          collapsed={collapsed}
          onToggle={onToggle}
        />
      )}

      {!collapsed && (
        <>
          <PanelSectionRow>
            <ToggleField
              label={
                <MakoExperimentalSettingLabel
                  label={t("CONFIG_ENABLE_HDR", "Enable HDR (Restart)")}
                  badgeLabel={t("EXPERIMENTAL_LABEL", "Experimental")}
                />
              }
              description={t(
                "CONFIG_ENABLE_HDR_DESC",
                "For external HDR displays. HDR must also be enabled in the game. Off by default.",
              )}
              checked={hdrEnabled}
              onChange={(enabled) => onConfigChange(DISABLE_HDR_EXPOSURE, !enabled)}
            />
          </PanelSectionRow>

          {hdrEnabled && (
            <PanelSectionRow>
              <Field
                label={t("HDR_QUALITY", "HDR Quality")}
                description={t(
                  "HDR_QUALITY_DESC",
                  "Balance image quality and speed. Applies live with a brief hitch.",
                )}
                childrenLayout="below"
                childrenContainerWidth="max"
              >
                <Dropdown
                  selectedOption={config.hdr_quality}
                  rgOptions={[
                    { data: "quality", label: t("HDR_QUALITY_QUALITY", "Quality") },
                    { data: "balanced", label: t("HDR_QUALITY_BALANCED", "Balanced") },
                    { data: "performance", label: t("HDR_QUALITY_PERFORMANCE", "Performance") },
                  ]}
                  onChange={(option) => onConfigChange(HDR_QUALITY, option.data)}
                />
              </Field>
            </PanelSectionRow>
          )}

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
                      "Recovery disables Steady Base Cap and Base FPS Cap and resets Real Frame Priority to Automatic. Changing either cap or priority turns Recovery off.",
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
                      "Recovery check interval: 0.1 s may hitch often; 2 s is default; 3 s checks least often. Test per game.",
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
                      "May reduce coloured or pixelated motion artifacts through Gamescope presentation. Optional with Scaling and Frame Generation; enable only if needed.",
                    )}
                  </div>
                  <MakoInlineTip tone="warning">
                    {t(
                      "CONFIG_GAMESCOPE_WSI_COMPATIBILITY_WARNING",
                      "Only supported 64-bit host launches. Leave off unless needed; it may reduce performance.",
                    )}
                  </MakoInlineTip>
                </>
              }
              checked={config.gamescope_wsi_compatibility}
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
                      "May fix startup failures with Frame Generation by keeping the game's requested swapchain image minimum. Use only for affected games.",
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
                    "CONFIG_MAILBOX_PRESENTATION_COMPATIBILITY",
                    "Mailbox Presentation (Restart)",
                  )}
                />
              }
              description={
                <>
                  <div>
                    {t(
                      "CONFIG_MAILBOX_PRESENTATION_COMPATIBILITY_DESC",
                      "May fix games that hang or fail with Frame Generation by presenting through Mailbox instead of the FIFO queue. Use only for affected games.",
                    )}
                  </div>
                  <MakoInlineTip tone="warning">
                    {t(
                      "CONFIG_MAILBOX_PRESENTATION_COMPATIBILITY_WARNING",
                      "A newer frame can replace a generated frame before it is shown, which can reduce smoothness or make pacing uneven.",
                    )}
                  </MakoInlineTip>
                </>
              }
              checked={config.mailbox_presentation_compatibility}
              onChange={(value) =>
                onConfigChange(MAILBOX_PRESENTATION_COMPATIBILITY, value)
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
                "Runs OpenGL games through Vulkan; may crash or freeze some games.",
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
                "May help Zink compatibility, audio stutter, or sudden loud sounds. Turn off to restore default audio.",
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
