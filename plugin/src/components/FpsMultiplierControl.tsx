import { useState } from "react";
import { PanelSectionRow, DialogButton, Focusable, SliderField, ToggleField } from "@decky/ui";
import { ConfigurationData } from "../config/configSchema";
import {
  ADAPTIVE,
  ADAPTIVE_MAX_MULTIPLIER,
  ADAPTIVE_STABLE_CADENCE,
  FRAME_GENERATION_ENABLED,
  MULTIPLIER,
  TARGET_FPS
} from "../config/generatedConfigSchema";
import t from "../i18n/i18n";

interface FpsMultiplierControlProps {
  config: ConfigurationData;
  onConfigChange: (fieldName: keyof ConfigurationData, value: boolean | number | string) => Promise<void>;
}

export function FpsMultiplierControl({
  config,
  onConfigChange
}: FpsMultiplierControlProps) {
  const [focusedControl, setFocusedControl] = useState<"decrease" | "increase" | null>(null);
  const adaptiveMaxMultiplier = config.adaptive_max_multiplier ?? 3;

  const multiplierButtonStyle = (isFocused: boolean) => ({
    height: "34px",
    display: "flex",
    alignItems: "center",
    justifyContent: "center",
    padding: "2px 0px 0px",
    minWidth: "48px",
    fontSize: "22px",
    fontWeight: "bold",
    color: "#fff7ed",
    background: "linear-gradient(135deg, #a94900 0%, #e87516 55%, #ffae52 100%)",
    border: "1px solid rgba(255, 206, 143, 0.9)",
    borderRadius: "4px",
    outline: isFocused ? "3px solid #ffffff" : "none",
    outlineOffset: "3px",
    boxShadow: isFocused ? "0 0 0 5px rgba(255, 170, 74, 0.45), 0 0 16px rgba(255, 170, 74, 0.95)" : "none",
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
          label={t("ADAPTIVE_TITLE", "Adaptive Frame Generation")}
          description={t("ADAPTIVE_DESC", "Experimental. Adaptive settings apply live. Restart the game after switching between Fixed and Adaptive so its swapchain has the correct capacity, then let timing settle before judging performance.")}
          checked={config.adaptive}
          onChange={(value) => onConfigChange(ADAPTIVE, value)}
        />
      </PanelSectionRow>

      {config.adaptive && (
        <>
          <PanelSectionRow>
            <SliderField
              label={`${t("ADAPTIVE_TARGET_FPS", "Target FPS")} (${config.target_fps})`}
              description={t("ADAPTIVE_TARGET_FPS_DESC", "Desired output rate. The multiplier limit may intentionally keep output below this target.")}
              value={config.target_fps}
              min={30}
              max={240}
              step={1}
              onChange={(value) => onConfigChange(TARGET_FPS, value)}
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

      <PanelSectionRow>
        <div
          style={{
            fontSize: "14px",
            fontWeight: "bold",
            marginTop: config.adaptive ? "24px" : "8px",
            marginBottom: "8px",
            color: "white"
          }}
        >
          {t("MULTIPLIER_TITLE", "FPS Multiplier")}
        </div>
      </PanelSectionRow>

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
