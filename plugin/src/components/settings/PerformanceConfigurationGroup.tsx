import { PanelSectionRow, SliderField, ToggleField } from "@decky/ui";
import { MdBolt } from "react-icons/md";
import {
  ALLOW_FP16,
  FLOW_SCALE_MAX,
  FLOW_SCALE_MIN,
  FLOW_SCALE,
  ULTRA_PERFORMANCE_FLOW_SCALE,
} from "../../config/configSchema";
import { ultraPerformanceChanges } from "../../config/ultraPerformancePreset";
import t from "../../i18n/i18n";
import { MakoInlineTip, MakoRestartLabel, MakoSectionHeader } from "../MakoUi";
import type { ConfigurationEditorProps } from "./types";

export function PerformanceConfigurationGroup({
  config,
  onConfigChange,
  onConfigUpdate,
}: ConfigurationEditorProps) {
  return (
    <>
      <MakoSectionHeader>
        {t("CONTENT_PERFORMANCE_SETTINGS", "Performance Settings")}
      </MakoSectionHeader>

      <PanelSectionRow>
        <ToggleField
          label={
            <span
              style={{
                display: "inline-flex",
                alignItems: "center",
                gap: "5px",
              }}
            >
              <MdBolt aria-hidden="true" size={16} color="#f4a259" />
              <MakoRestartLabel
                label={t(
                  "CONFIG_ULTRA_PERFORMANCE",
                  "Ultra Performance (Restart)",
                )}
              />
            </span>
          }
          description={
            <>
              <div>
                {t(
                  "CONFIG_ULTRA_PERFORMANCE_DESC",
                  "Reduces MAKO's GPU workload on low-power devices. Uses 75% Flow Scale, the Lighter FG Model, FP16 when supported, and LS1 Performance when Scaling is enabled. Trades image quality for performance across the active MAKO features.",
                )}
              </div>
              <MakoInlineTip tone="info">
                {t(
                  "CONFIG_ULTRA_PERFORMANCE_WARNING",
                  "Turning Ultra Performance on or off requires a game restart. Other compatible profile controls remain available after startup.",
                )}
              </MakoInlineTip>
            </>
          }
          checked={config.ultra_performance}
          onChange={(value) => onConfigUpdate(ultraPerformanceChanges(value))}
        />
      </PanelSectionRow>

      <PanelSectionRow>
        <SliderField
          label={`${t("CONFIG_FLOW_SCALE", "Flow Scale")} (${Math.round((config.ultra_performance ? ULTRA_PERFORMANCE_FLOW_SCALE : config.flow_scale) * 100)}%)`}
          description={t(
            "CONFIG_FLOW_SCALE_DESC",
            "Controls the internal motion-estimation resolution used only for Frame Generation. Lower values reduce GPU work; higher values favour quality.",
          )}
          value={
            config.ultra_performance
              ? ULTRA_PERFORMANCE_FLOW_SCALE
              : config.flow_scale
          }
          min={FLOW_SCALE_MIN}
          max={FLOW_SCALE_MAX}
          step={0.01}
          disabled={config.ultra_performance}
          onChange={(value) => onConfigChange(FLOW_SCALE, value)}
        />
      </PanelSectionRow>

      <PanelSectionRow>
        <ToggleField
          label={
            <MakoRestartLabel
              label={t("CONFIG_ALLOW_FP16", "Allow FP16 (Restart)")}
            />
          }
          description={t(
            "CONFIG_ALLOW_FP16_DESC",
            "Global renderer setting: applies to all profiles and cannot be changed per game. Improves performance on AMD; disable for older NVIDIA GPUs. Restart the game after changing it.",
          )}
          checked={config.ultra_performance || config.allow_fp16}
          disabled={config.ultra_performance}
          onChange={(value) => onConfigChange(ALLOW_FP16, value)}
          bottomSeparator="none"
        />
      </PanelSectionRow>
    </>
  );
}
