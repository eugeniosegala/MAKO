import { useEffect, useRef, useState } from "react";
import { PanelSectionRow, DialogButton, Focusable, SliderField, ToggleField } from "@decky/ui";
import { ConfigurationData } from "../config/configSchema";
import {
  ADAPTIVE,
  ADAPTIVE_AUTO_BASE_FPS_CAP,
  ADAPTIVE_MAX_MULTIPLIER,
  ADAPTIVE_STABLE_CADENCE,
  FRAME_GENERATION_ENABLED,
  MULTIPLIER,
  PERFORMANCE_MODE,
  TARGET_FPS
} from "../config/generatedConfigSchema";
import t from "../i18n/i18n";
import { MakoSectionHeader, makoDialogButtonStyle } from "./MakoUi";

const TARGET_FPS_SAVE_DELAY_MS = 250;

interface FpsMultiplierControlProps {
  config: ConfigurationData;
  onConfigChange: (fieldName: keyof ConfigurationData, value: boolean | number | string) => Promise<void>;
}

export function FpsMultiplierControl({
  config,
  onConfigChange
}: FpsMultiplierControlProps) {
  const [focusedControl, setFocusedControl] = useState<"decrease" | "increase" | null>(null);
  const [targetFps, setTargetFps] = useState(config.target_fps);
  const pendingTargetFps = useRef<{
    value: number;
    save: FpsMultiplierControlProps["onConfigChange"];
  } | null>(null);
  const targetFpsSaveTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const adaptiveMaxMultiplier = config.adaptive_max_multiplier ?? 3;
  const automaticBaseFpsCap = Math.max(10, targetFps / 2);
  const automaticBaseFpsCapLabel = Number.isInteger(automaticBaseFpsCap)
    ? automaticBaseFpsCap.toFixed(0)
    : automaticBaseFpsCap.toFixed(1);

  useEffect(() => {
    if (pendingTargetFps.current === null) {
      setTargetFps(config.target_fps);
    }
  }, [config.target_fps]);

  useEffect(() => () => {
    if (targetFpsSaveTimer.current !== null) {
      clearTimeout(targetFpsSaveTimer.current);
    }
    if (pendingTargetFps.current !== null) {
      const pendingChange = pendingTargetFps.current;
      pendingTargetFps.current = null;
      void pendingChange.save(TARGET_FPS, pendingChange.value);
    }
  }, []);

  const handleTargetFpsChange = (value: number) => {
    setTargetFps(value);
    pendingTargetFps.current = { value, save: onConfigChange };
    if (targetFpsSaveTimer.current !== null) {
      clearTimeout(targetFpsSaveTimer.current);
    }
    targetFpsSaveTimer.current = setTimeout(() => {
      const pendingChange = pendingTargetFps.current;
      pendingTargetFps.current = null;
      targetFpsSaveTimer.current = null;
      if (pendingChange !== null) {
        void pendingChange.save(TARGET_FPS, pendingChange.value);
      }
    }, TARGET_FPS_SAVE_DELAY_MS);
  };

  const multiplierButtonStyle = (isFocused: boolean) => ({
    height: "34px",
    display: "flex",
    alignItems: "center",
    justifyContent: "center",
    padding: "2px 0px 0px",
    minWidth: "48px",
    fontSize: "22px",
    fontWeight: "bold",
    ...makoDialogButtonStyle(isFocused),
    transform: isFocused ? "scale(1.04)" : "none",
    scrollMarginTop: "28px",
    scrollMarginBottom: "28px"
  }) as const;

  return (
    <>
      <PanelSectionRow>
        <ToggleField
          label={t("FRAME_GENERATION_ENABLED", "Frame Generation (Live On/Off)")}
          description={t("FRAME_GENERATION_ENABLED_DESC", "Live on/off. Leave it on to use Fixed or Adaptive Frame Generation. When off, neither mode generates frames; your settings stay saved.")}
          checked={config.frame_generation_enabled ?? true}
          onChange={(value) => onConfigChange(FRAME_GENERATION_ENABLED, value)}
        />
      </PanelSectionRow>

      <PanelSectionRow>
        <ToggleField
          label={t("CONFIG_PERFORMANCE_MODE", "Performance Mode")}
          description={t("CONFIG_PERFORMANCE_MODE_DESC", "Can improve performance at the cost of ghosting. Start disabled; enable it only if your device struggles, then test it per game.")}
          checked={config.performance_mode}
          onChange={(value) => onConfigChange(PERFORMANCE_MODE, value)}
        />
      </PanelSectionRow>

      <PanelSectionRow>
        <ToggleField
          label={t("ADAPTIVE_TITLE", "Adaptive Frame Generation")}
          description={t("ADAPTIVE_DESC", "Adaptive settings apply live when the current swapchain has enough reserved capacity. Increasing the multiplier ceiling may require a game restart. Let timing settle before judging performance.")}
          checked={config.adaptive}
          onChange={(value) => onConfigChange(ADAPTIVE, value)}
        />
      </PanelSectionRow>

      {config.adaptive && (
        <>
          <PanelSectionRow>
            <SliderField
              label={`${t("ADAPTIVE_TARGET_FPS", "Target FPS")} (${targetFps})`}
              description={t("ADAPTIVE_TARGET_FPS_DESC", "Desired output rate. The multiplier limit may intentionally keep output below this target.")}
              value={targetFps}
              min={30}
              max={240}
              step={1}
              onChange={handleTargetFpsChange}
            />
          </PanelSectionRow>
          <PanelSectionRow>
            <ToggleField
              label={`${t("ADAPTIVE_AUTO_BASE_FPS_CAP", "Adaptive FPS Cap")} (${automaticBaseFpsCapLabel} FPS)`}
              description={t("ADAPTIVE_AUTO_BASE_FPS_CAP_DESC", "Locks the game's real FPS to half the target, giving frame generation a steadier cadence. Best for uneven frame rates; test per game. Turn it off to use the Base FPS Cap.")}
              checked={config.adaptive_auto_base_fps_cap ?? false}
              onChange={(value) => onConfigChange(ADAPTIVE_AUTO_BASE_FPS_CAP, value)}
            />
          </PanelSectionRow>
          <PanelSectionRow>
            <SliderField
              label={`${t("ADAPTIVE_MAX_MULTIPLIER", "Maximum Adaptive Multiplier")} (${adaptiveMaxMultiplier}x)`}
              description={t("ADAPTIVE_MAX_MULTIPLIER_DESC", "Interpolation ceiling. 3x is balanced; 2x usually looks best, while 4x gives more headroom to reach the target. Test per game.")}
              value={adaptiveMaxMultiplier}
              min={2}
              max={4}
              step={1}
              validValues="steps"
              minimumDpadGranularity={1}
              notchCount={3}
              notchTicksVisible
              onChange={(value) => onConfigChange(ADAPTIVE_MAX_MULTIPLIER, value)}
            />
          </PanelSectionRow>
          <PanelSectionRow>
            <ToggleField
              label={t("ADAPTIVE_SMOOTH_CADENCE", "Smooth Cadence")}
              description={t("ADAPTIVE_SMOOTH_CADENCE_DESC", "Uses a validated constant interpolation cadence. It can make displayed motion smoother, but may lower real-frame cadence and increase input lag. Enabled by default; disable it if a game feels more responsive without it.")}
              checked={config.adaptive_stable_cadence ?? true}
              onChange={(value) => onConfigChange(ADAPTIVE_STABLE_CADENCE, value)}
            />
          </PanelSectionRow>
        </>
      )}

      <MakoSectionHeader
        description={t(
          "MULTIPLIER_DESC",
          "Sets the output multiplier in Fixed mode (2x–4x). Adaptive manages the multiplier automatically."
        )}
      >
        {t("MULTIPLIER_TITLE", "Fixed FPS Multiplier")}
      </MakoSectionHeader>

      <PanelSectionRow>
        <Focusable
          style={{
            marginTop: "8px",
            marginBottom: "8px",
            display: "flex",
            justifyContent: "center",
            alignItems: "center"
          }}
          flow-children="horizontal"
          noFocusRing
        >
          <DialogButton
            style={{
              ...multiplierButtonStyle(focusedControl === "decrease"),
              marginLeft: "0px"
            }}
            onClick={() => onConfigChange(MULTIPLIER, Math.max(2, config.multiplier - 1))}
            onGamepadFocus={() => setFocusedControl("decrease")}
            onGamepadBlur={() => setFocusedControl((current) => current === "decrease" ? null : current)}
            disabled={config.adaptive || config.multiplier <= 2}
          >
            −
          </DialogButton>
          <div
            style={{
              marginLeft: "20px",
              marginRight: "20px",
              fontSize: "16px",
              fontWeight: "bold",
              color: config.adaptive ? "rgba(255, 255, 255, 0.45)" : "white",
              minWidth: "60px",
              textAlign: "center"
            }}
          >
            {config.adaptive ? t("ADAPTIVE_VALUE", "Adaptive") : `${config.multiplier}X`}
          </div>
          <DialogButton
            style={{
              ...multiplierButtonStyle(focusedControl === "increase"),
              marginLeft: "0px"
            }}
            onClick={() => onConfigChange(MULTIPLIER, Math.min(4, config.multiplier + 1))}
            onGamepadFocus={() => setFocusedControl("increase")}
            onGamepadBlur={() => setFocusedControl((current) => current === "increase" ? null : current)}
            disabled={config.adaptive || config.multiplier >= 4}
          >
            +
          </DialogButton>
        </Focusable>
      </PanelSectionRow>
    </>
  );
}
