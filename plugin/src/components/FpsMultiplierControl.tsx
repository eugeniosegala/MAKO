import { useState } from "react";
import {
  PanelSectionRow,
  DialogButton,
  Focusable,
  SliderField,
  ToggleField,
} from "@decky/ui";
import {
  ADAPTIVE_AUTO_BASE_FPS_CAP,
  ADAPTIVE_MAX_MULTIPLIER_MAX,
  ADAPTIVE_MAX_MULTIPLIER_MIN,
  ADAPTIVE_MAX_MULTIPLIER,
  ADAPTIVE_MINIMUM_BASE_FPS,
  ADAPTIVE_STABLE_CADENCE,
  FIXED_MULTIPLIER_UI_MAX,
  FIXED_MULTIPLIER_UI_MIN,
  FRAME_GENERATION_ENABLED,
  getDefaults,
  MULTIPLIER,
  PERFORMANCE_MODE,
  TARGET_FPS_MAX,
  TARGET_FPS_MIN,
  type ConfigurationData,
} from "../config/configSchema";
import {
  adaptiveModeChanges,
  fractionalAdaptivePresetChanges,
  isFractionalAdaptivePresetEnabled,
} from "../config/fractionalAdaptivePreset";
import t from "../i18n/i18n";
import { useDeferredTargetFps } from "../hooks/useDeferredTargetFps";
import {
  MakoSectionHeader,
  makoDangerTextColor,
  makoDialogButtonStyle,
} from "./MakoUi";

const DEFAULT_CONFIGURATION = getDefaults();

interface FpsMultiplierControlProps {
  config: ConfigurationData;
  onConfigChange: (
    fieldName: keyof ConfigurationData,
    value: boolean | number | string,
  ) => Promise<void>;
  onConfigUpdate: (changes: Partial<ConfigurationData>) => Promise<void>;
}

export function FpsMultiplierControl({
  config,
  onConfigChange,
  onConfigUpdate,
}: FpsMultiplierControlProps) {
  const [focusedControl, setFocusedControl] = useState<
    "decrease" | "increase" | null
  >(null);
  const { targetFps, changeTargetFps } = useDeferredTargetFps(
    config.target_fps,
    onConfigChange,
  );
  const adaptiveMaxMultiplier =
    config.adaptive_max_multiplier ??
    DEFAULT_CONFIGURATION.adaptive_max_multiplier;
  const automaticBaseFpsCap = Math.max(
    ADAPTIVE_MINIMUM_BASE_FPS,
    targetFps / 2,
  );
  const automaticBaseFpsCapLabel = Number.isInteger(automaticBaseFpsCap)
    ? automaticBaseFpsCap.toFixed(0)
    : automaticBaseFpsCap.toFixed(1);

  const multiplierButtonStyle = (isFocused: boolean) =>
    ({
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
      scrollMarginBottom: "28px",
    }) as const;

  return (
    <>
      <PanelSectionRow>
        <ToggleField
          label={t(
            "FRAME_GENERATION_ENABLED",
            "Frame Generation (Live On/Off)",
          )}
          description={
            <>
              <div>
                {t(
                  "FRAME_GENERATION_ENABLED_DESC",
                  "Live on/off. Leave it on to use Fixed or Adaptive Frame Generation. When off, neither mode generates frames; your settings stay saved.",
                )}
              </div>
              <div
                style={{
                  marginTop: "6px",
                  color: makoDangerTextColor,
                  fontWeight: "500",
                }}
              >
                {t(
                  "FRAME_GENERATION_ENABLED_WARNING",
                  "Keep this on if you want frame generation.",
                )}
              </div>
            </>
          }
          checked={
            config.frame_generation_enabled ??
            DEFAULT_CONFIGURATION.frame_generation_enabled
          }
          onChange={(value) => onConfigChange(FRAME_GENERATION_ENABLED, value)}
        />
      </PanelSectionRow>

      <PanelSectionRow>
        <ToggleField
          label={t("CONFIG_PERFORMANCE_MODE", "Performance Mode (Restart)")}
          description={t(
            "CONFIG_PERFORMANCE_MODE_DESC",
            "Uses a lighter model that may improve performance at the cost of more ghosting. Restart the game after changing it. Start disabled and test per game.",
          )}
          checked={config.performance_mode}
          onChange={(value) => onConfigChange(PERFORMANCE_MODE, value)}
        />
      </PanelSectionRow>

      <PanelSectionRow>
        <ToggleField
          label={t("ADAPTIVE_TITLE", "Adaptive Frame Generation")}
          description={t(
            "ADAPTIVE_DESC",
            "Adjusts frame generation to reach Target FPS. The steady base cap is the default for smoother pacing. Enable Fractional Adaptive below to keep more real frames, but test it per game. Raising the multiplier limit may require a restart.",
          )}
          checked={config.adaptive}
          onChange={(value) => onConfigUpdate(adaptiveModeChanges(value))}
        />
      </PanelSectionRow>

      {config.adaptive && (
        <>
          <PanelSectionRow>
            <ToggleField
              label={t(
                "FRACTIONAL_ADAPTIVE_PRESET",
                "Fractional Adaptive (Preset)",
              )}
              description={t(
                "FRACTIONAL_ADAPTIVE_PRESET_DESC",
                "Mixes generation ratios to reach targets such as 60 real FPS > 90 displayed FPS. Pros: keeps more real frames and may reduce input lag. It can feel choppy in some games, but especially smooth and responsive in others. Off uses the steady base cap. Incompatible setting changes turn this preset off automatically.",
              )}
              checked={isFractionalAdaptivePresetEnabled(config)}
              onChange={(value) =>
                onConfigUpdate(fractionalAdaptivePresetChanges(value))
              }
            />
          </PanelSectionRow>
          <PanelSectionRow>
            <SliderField
              label={`${t("ADAPTIVE_TARGET_FPS", "Target FPS")} (${targetFps})`}
              description={t(
                "ADAPTIVE_TARGET_FPS_DESC",
                "Desired displayed FPS. Fractional Adaptive may mix ratios to reach it; the steady base cap limits real FPS to half the target.",
              )}
              value={targetFps}
              min={TARGET_FPS_MIN}
              max={TARGET_FPS_MAX}
              step={1}
              onChange={changeTargetFps}
            />
          </PanelSectionRow>
          <PanelSectionRow>
            <ToggleField
              label={`${t("ADAPTIVE_AUTO_BASE_FPS_CAP", "Steady Base Cap")} (${automaticBaseFpsCapLabel} FPS)`}
              description={t(
                "ADAPTIVE_AUTO_BASE_FPS_CAP_DESC",
                "The default Adaptive mode. Caps real FPS at half the target for an even cadence. Pros: usually smoother pacing. Cons: fewer real frames and potentially more input lag.",
              )}
              checked={
                config.adaptive_auto_base_fps_cap ??
                DEFAULT_CONFIGURATION.adaptive_auto_base_fps_cap
              }
              onChange={(value) =>
                onConfigChange(ADAPTIVE_AUTO_BASE_FPS_CAP, value)
              }
            />
          </PanelSectionRow>
          <PanelSectionRow>
            <SliderField
              label={`${t("ADAPTIVE_MAX_MULTIPLIER", "Maximum Adaptive Multiplier")} (${adaptiveMaxMultiplier}x)`}
              description={t(
                "ADAPTIVE_MAX_MULTIPLIER_DESC",
                "Interpolation ceiling. 3x is balanced; 2x usually looks best, while 4x gives more headroom to reach the target. Test per game.",
              )}
              value={adaptiveMaxMultiplier}
              min={ADAPTIVE_MAX_MULTIPLIER_MIN}
              max={ADAPTIVE_MAX_MULTIPLIER_MAX}
              step={1}
              validValues="steps"
              minimumDpadGranularity={1}
              notchCount={
                ADAPTIVE_MAX_MULTIPLIER_MAX -
                ADAPTIVE_MAX_MULTIPLIER_MIN +
                1
              }
              notchTicksVisible
              onChange={(value) =>
                onConfigChange(ADAPTIVE_MAX_MULTIPLIER, value)
              }
            />
          </PanelSectionRow>
          <PanelSectionRow>
            <ToggleField
              label={t("ADAPTIVE_SMOOTH_CADENCE", "Smooth Cadence")}
              description={t(
                "ADAPTIVE_SMOOTH_CADENCE_DESC",
                "Uses a validated constant interpolation cadence. It can make displayed motion smoother, but may lower real-frame cadence and increase input lag. Enabled by default; disable it if a game feels more responsive without it.",
              )}
              bottomSeparator="none"
              checked={
                config.adaptive_stable_cadence ??
                DEFAULT_CONFIGURATION.adaptive_stable_cadence
              }
              onChange={(value) =>
                onConfigChange(ADAPTIVE_STABLE_CADENCE, value)
              }
            />
          </PanelSectionRow>
        </>
      )}

      <MakoSectionHeader
        topMargin="26px"
        description={t(
          "MULTIPLIER_DESC",
          "Sets Fixed mode to 2x–4x. An increase beyond the running game's reserved capacity applies after restart. Adaptive manages the multiplier automatically.",
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
            alignItems: "center",
          }}
          flow-children="horizontal"
          noFocusRing
        >
          <DialogButton
            style={{
              ...multiplierButtonStyle(focusedControl === "decrease"),
              marginLeft: "0px",
            }}
            onClick={() =>
              onConfigChange(
                MULTIPLIER,
                Math.max(FIXED_MULTIPLIER_UI_MIN, config.multiplier - 1),
              )
            }
            onGamepadFocus={() => setFocusedControl("decrease")}
            onGamepadBlur={() =>
              setFocusedControl((current) =>
                current === "decrease" ? null : current,
              )
            }
            disabled={
              config.adaptive || config.multiplier <= FIXED_MULTIPLIER_UI_MIN
            }
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
              textAlign: "center",
            }}
          >
            {config.adaptive
              ? t("ADAPTIVE_VALUE", "Adaptive")
              : `${config.multiplier}X`}
          </div>
          <DialogButton
            style={{
              ...multiplierButtonStyle(focusedControl === "increase"),
              marginLeft: "0px",
            }}
            onClick={() =>
              onConfigChange(
                MULTIPLIER,
                Math.min(FIXED_MULTIPLIER_UI_MAX, config.multiplier + 1),
              )
            }
            onGamepadFocus={() => setFocusedControl("increase")}
            onGamepadBlur={() =>
              setFocusedControl((current) =>
                current === "increase" ? null : current,
              )
            }
            disabled={
              config.adaptive || config.multiplier >= FIXED_MULTIPLIER_UI_MAX
            }
          >
            +
          </DialogButton>
        </Focusable>
      </PanelSectionRow>
    </>
  );
}
