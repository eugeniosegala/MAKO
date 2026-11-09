import {
  DropdownItem,
  PanelSectionRow,
  SliderField,
  ToggleField,
} from "@decky/ui";
import { MdBolt } from "react-icons/md";
import {
  ALLOW_FP16,
  GPU_HEADROOM_PERCENT_VALUES,
  DISABLE_STEAM_OVERLAY,
  FLOW_SCALE_MAX,
  FLOW_SCALE_MIN,
  FLOW_SCALE,
} from "../../config/configSchema";
import {
  ultraPerformanceChanges,
  ultraPerformanceFlowScale,
} from "../../config/ultraPerformancePreset";
import t from "../../i18n/i18n";
import { MakoRestartLabel } from "../MakoUi";
import type { ConfigurationEditorProps } from "./types";

export function PerformanceConfigurationGroup({
  config,
  onConfigChange,
  onConfigUpdate,
}: ConfigurationEditorProps) {
  return (
    <>
      <PanelSectionRow>
        <ToggleField
          label={
            <MakoRestartLabel
              label={t(
                "CONFIG_DISABLE_STEAM_OVERLAY",
                "Disable Steam Overlay (Restart)",
              )}
            />
          }
          description={t(
            "CONFIG_DISABLE_STEAM_OVERLAY_DESC",
            "May improve performance and reduce stutter. Disables Steam's overlay, chat, browser and FPS counter in Desktop Mode/Big Picture; may affect Steam Input. Gaming Mode menus and performance overlay remain available.",
          )}
          checked={config.disable_steam_overlay}
          onChange={(value) => onConfigChange(DISABLE_STEAM_OVERLAY, value)}
        />
      </PanelSectionRow>

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
          description={t(
            "CONFIG_ULTRA_PERFORMANCE_DESC",
            "Cuts GPU and memory use on low-power devices at some cost to image quality. Locks Flow Scale at 60% for MAKO FG and 70% for LSFG, switches LSFG to LSFG Performance, and uses MAKO Scaler when Scaling is enabled.",
          )}
          checked={config.ultra_performance}
          onChange={(value) => onConfigUpdate(ultraPerformanceChanges(value, config))}
        />
      </PanelSectionRow>

      <PanelSectionRow>
        <SliderField
          label={`${t("CONFIG_FLOW_SCALE", "Flow Scale")} (${Math.round((config.ultra_performance ? ultraPerformanceFlowScale(config) : config.flow_scale) * 100)}%)`}
          description={t(
            "CONFIG_FLOW_SCALE_DESC",
            "Frame Generation motion-estimation resolution. Lower saves GPU work; higher favors quality.",
          )}
          value={
            config.ultra_performance
              ? ultraPerformanceFlowScale(config)
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
          label={t("CONFIG_GPU_HEADROOM", "GPU Headroom")}
          description={t(
            "CONFIG_GPU_HEADROOM_DESC",
            "Reduces real FPS during Frame Generation to free GPU capacity. Requires AMD GPU readings. May reduce stutter, but add input lag or uneven pacing.",
          )}
          checked={config.gpu_headroom_percent > 0}
          onChange={(enabled) =>
            onConfigUpdate({
              gpu_headroom_percent: enabled ? 10 : 0,
              ...(enabled ? { dynamic_cadence_recovery: false } : {}),
            })
          }
        />
      </PanelSectionRow>
      {config.gpu_headroom_percent > 0 && (
        <PanelSectionRow>
          <DropdownItem
            label={t("CONFIG_GPU_HEADROOM_RESERVE", "Reserved GPU capacity")}
            description={t(
              "CONFIG_GPU_HEADROOM_TRADEOFF",
              "10% targets about 90% GPU usage. Adaptive can add generated frames; Fixed may lower output FPS. Disables Dynamic Cadence Recovery.",
            )}
            rgOptions={GPU_HEADROOM_PERCENT_VALUES.filter(
              (value) => value > 0,
            ).map((value) => ({
              data: value,
              label: `${value}%`,
            }))}
            selectedOption={config.gpu_headroom_percent}
            onChange={(option) =>
              onConfigUpdate({
                gpu_headroom_percent: Number(option.data),
                dynamic_cadence_recovery: false,
              })
            }
          />
        </PanelSectionRow>
      )}

      <PanelSectionRow>
        <ToggleField
          label={
            <MakoRestartLabel
              label={t("CONFIG_ALLOW_FP16", "Allow FP16 (Restart)")}
            />
          }
          description={t(
            "CONFIG_ALLOW_FP16_DESC",
            "Global precision setting for all profiles: on requests FP16; off uses FP32. Applies to Frame Generation and MAKO Scaler. LS1 always uses FP32. Unsupported FP16 models show a warning. Restart the game after changing it.",
          )}
          checked={config.allow_fp16}
          onChange={(value) => onConfigChange(ALLOW_FP16, value)}
          bottomSeparator="none"
        />
      </PanelSectionRow>
    </>
  );
}
