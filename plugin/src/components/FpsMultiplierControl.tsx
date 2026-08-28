import { useState } from "react";
import {
  PanelSectionRow,
  DialogButton,
  Field,
  SliderField,
  ToggleField,
} from "@decky/ui";
import {
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
  TARGET_FPS,
  TARGET_FPS_MAX,
  TARGET_FPS_MIN,
  type ConfigurationData,
} from "../config/configSchema";
import {
  adaptiveModeChanges,
  fractionalAdaptivePresetChanges,
  isFractionalAdaptivePresetEnabled,
  steadyBaseCapChanges,
} from "../config/fractionalAdaptivePreset";
import t from "../i18n/i18n";
import {
  MakoInlineTip,
  MakoFocusable,
  MakoSettingRelationship,
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
  const targetFps = config.target_fps;
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
          label={t("FRAME_GENERATION_ENABLED", "Frame Generation")}
          description={
            <>
              <div>
                {t(
                  "FRAME_GENERATION_ENABLED_DESC",
                  "Leave it on to use Fixed or Adaptive Frame Generation. When off, neither mode generates frames; your settings stay saved.",
                )}
              </div>
              <MakoInlineTip tone="info">
                {t(
                  "FRAME_GENERATION_ENABLED_WARNING",
                  "Keep this on if you want frame generation.",
                )}
              </MakoInlineTip>
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
          label={t("ADAPTIVE_TITLE", "Adaptive Frame Generation")}
          description={t(
            "ADAPTIVE_DESC",
            "Adjusts frame generation to reach Target FPS. The steady base cap is the default for smoother pacing. Enable Fractional Adaptive to keep more real frames, but test it per game. MAKO briefly asks the game to rebuild its swapchain only when more generated-frame capacity is needed.",
          )}
          checked={config.adaptive}
          onChange={(value) => onConfigUpdate(adaptiveModeChanges(value))}
        />
      </PanelSectionRow>

      {config.adaptive && (
        <>
          <PanelSectionRow>
            <ToggleField
              label={t("FRACTIONAL_ADAPTIVE_PRESET", "Fractional Adaptive")}
              description={
                <>
                  <div>
                    {t(
                      "FRACTIONAL_ADAPTIVE_PRESET_DESC",
                      "Mixes generation ratios to reach targets such as 60 real FPS → 90 displayed FPS. It keeps more real frames and may reduce input lag and ghosting, but can feel less smooth in some games.",
                    )}
                  </div>
                  <MakoSettingRelationship>
                    {t(
                      "FRACTIONAL_ADAPTIVE_PRESET_RELATION",
                      "Cannot be combined with Steady Base Cap. Changing it also turns Dynamic Cadence Recovery off.",
                    )}
                  </MakoSettingRelationship>
                </>
              }
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
              onChange={(value) => onConfigChange(TARGET_FPS, value)}
            />
          </PanelSectionRow>
          <PanelSectionRow>
            <ToggleField
              label={`${t("ADAPTIVE_AUTO_BASE_FPS_CAP", "Steady Base Cap")} (${automaticBaseFpsCapLabel} FPS)`}
              description={
                <>
                  <div>
                    {t(
                      "ADAPTIVE_AUTO_BASE_FPS_CAP_DESC",
                      "The default Adaptive mode. Caps real FPS at half the target for an even cadence. Pros: usually smoother pacing. Cons: fewer real frames and potentially more input lag and ghosting.",
                    )}
                  </div>
                  <MakoSettingRelationship>
                    {t(
                      "ADAPTIVE_AUTO_BASE_FPS_CAP_RELATION",
                      "Overrides Base FPS Cap. Cannot be combined with Fractional Adaptive or Dynamic Cadence Recovery.",
                    )}
                  </MakoSettingRelationship>
                </>
              }
              checked={
                config.adaptive_auto_base_fps_cap ??
                DEFAULT_CONFIGURATION.adaptive_auto_base_fps_cap
              }
              onChange={(value) => onConfigUpdate(steadyBaseCapChanges(value))}
            />
          </PanelSectionRow>
          <PanelSectionRow>
            <SliderField
              label={`${t("ADAPTIVE_MAX_MULTIPLIER", "Maximum Adaptive Multiplier")} (${adaptiveMaxMultiplier}x)`}
              description={t(
                "ADAPTIVE_MAX_MULTIPLIER_DESC",
                "Interpolation ceiling. 3x is balanced; 2x usually looks best, 4x gives more headroom, and 5x is for high-refresh displays with substantial GPU and memory headroom. Test per game.",
              )}
              value={adaptiveMaxMultiplier}
              min={ADAPTIVE_MAX_MULTIPLIER_MIN}
              max={ADAPTIVE_MAX_MULTIPLIER_MAX}
              step={1}
              validValues="steps"
              minimumDpadGranularity={1}
              notchCount={
                ADAPTIVE_MAX_MULTIPLIER_MAX - ADAPTIVE_MAX_MULTIPLIER_MIN + 1
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

      <PanelSectionRow>
        <Field
          label={t("MULTIPLIER_TITLE", "Fixed FPS Multiplier")}
          bottomSeparator="none"
          description={
            <>
              <span style={{ display: "block", paddingTop: "8px" }}>
                {t(
                  "MULTIPLIER_DESC",
                  "Sets Fixed mode to 2x–5x. Fixed may perform better than Adaptive in some games, especially when frame pacing is uneven or unstable. 5x is a high-cost option for high-refresh displays. Test both per game. With Dynamic Cadence Recovery, this is a ceiling against confirmed Gamescope refresh; Adaptive manages its own multiplier. MAKO briefly asks the game to rebuild its swapchain only when more generated-frame capacity is needed.",
                )}
              </span>
              {config.adaptive && (
                <MakoSettingRelationship>
                  {t(
                    "MULTIPLIER_ADAPTIVE_RELATION",
                    "Unavailable while Adaptive Frame Generation is enabled.",
                  )}
                </MakoSettingRelationship>
              )}
            </>
          }
          childrenLayout="below"
        >
          <MakoFocusable
            style={{
              width: "100%",
              boxSizing: "border-box",
              marginTop: "6px",
              display: "flex",
              justifyContent: "center",
              alignItems: "center",
            }}
            flow-children="row"
            noFocusRing
          >
            <DialogButton
              className="Mako_DialogButton"
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
              className="Mako_DialogButton"
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
          </MakoFocusable>
        </Field>
      </PanelSectionRow>
    </>
  );
}
