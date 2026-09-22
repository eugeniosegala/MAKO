import type { ConfigurationEditorProps } from "./settings/types";
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
  FIXED_MULTIPLIER_UI_MIN,
  FRAME_GENERATION_ENABLED,
  FRAME_GENERATION_PROVISIONED,
  getDefaults,
  PERFORMANCE_MODE,
  TARGET_FPS,
  TARGET_FPS_MAX,
  TARGET_FPS_MIN,
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
  MakoRestartLabel,
  MakoSettingRelationship,
  makoDialogButtonStyle,
} from "./MakoUi";

const DEFAULT_CONFIGURATION = getDefaults();

export function FpsMultiplierControl({
  config,
  onConfigChange,
  onConfigUpdate,
}: ConfigurationEditorProps) {
  const [focusedControl, setFocusedControl] = useState<string | null>(null);
  const targetFps = config.target_fps;
  const adaptiveMaxMultiplier =
    config.adaptive_max_multiplier ??
    DEFAULT_CONFIGURATION.adaptive_max_multiplier;
  const frameGenerationEnabled =
    config.frame_generation_enabled ??
    DEFAULT_CONFIGURATION.frame_generation_enabled;
  const frameGenerationProvisioned =
    config.frame_generation_provisioned ??
    DEFAULT_CONFIGURATION.frame_generation_provisioned;
  const automaticBaseFpsCap = Math.max(
    ADAPTIVE_MINIMUM_BASE_FPS,
    targetFps / 2,
  );
  const automaticBaseFpsCapLabel = Number.isInteger(automaticBaseFpsCap)
    ? automaticBaseFpsCap.toFixed(0)
    : automaticBaseFpsCap.toFixed(1);

  const multiplierButtonStyle = (isFocused: boolean, isSelected: boolean) => {
    const baseStyle = makoDialogButtonStyle(isFocused);
    return {
      ...baseStyle,
      height: "34px",
      display: "flex",
      alignItems: "center",
      justifyContent: "center",
      padding: "2px 6px 0px",
      minWidth: config.adaptive ? "104px" : "46px",
      fontSize: config.adaptive ? "14px" : "16px",
      fontWeight: "bold",
      border: isSelected
        ? "1px solid rgba(101, 219, 236, 0.96)"
        : baseStyle.border,
      boxShadow: isSelected
        ? "inset 0 1px 0 rgba(255, 255, 255, 0.16), 0 0 10px rgba(43, 162, 184, 0.38)"
        : baseStyle.boxShadow,
      opacity: isSelected ? 1 : 0.78,
      transform: isFocused ? "scale(1.04)" : "none",
      scrollMarginTop: "28px",
      scrollMarginBottom: "28px",
    } as const;
  };

  const factorChoices = config.adaptive
    ? [
        { id: "off", label: "0x", enabled: false, multiplier: null },
        {
          id: "adaptive",
          label: t("ADAPTIVE_VALUE", "Adaptive"),
          enabled: true,
          multiplier: null,
        },
      ]
    : [0, 2, 3, 4, 5].map((value) => ({
        id: `fixed-${value}`,
        label: `${value}x`,
        enabled: value !== 0,
        multiplier: value === 0 ? null : value,
      }));

  return (
    <>
      <PanelSectionRow>
        <ToggleField
          label={
            <MakoRestartLabel
              label={t(
                "FRAME_GENERATION_PROVISIONED",
                "Frame Generation (Restart)",
              )}
            />
          }
          description={
            <>
              <div>
                {t(
                  "FRAME_GENERATION_PROVISIONED_DESC",
                  "Loads and provisions MAKO Frame Generation when the game starts. Turn it off when you only want Scaling or Shaders.",
                )}
              </div>
              {frameGenerationProvisioned && (
                <MakoInlineTip tone="info">
                  {t(
                    "FRAME_GENERATION_PROVISIONED_NOTE",
                    "Use 0x below to pause or resume Frame Generation live without unloading its resources.",
                  )}
                </MakoInlineTip>
              )}
            </>
          }
          checked={frameGenerationProvisioned}
          bottomSeparator={frameGenerationProvisioned ? undefined : "none"}
          onChange={(value) =>
            onConfigChange(FRAME_GENERATION_PROVISIONED, value)
          }
        />
      </PanelSectionRow>

      {frameGenerationProvisioned && (
        <>
          <PanelSectionRow>
            <ToggleField
              label={t("ADAPTIVE_TITLE", "Adaptive Frame Generation")}
              description={t(
                "ADAPTIVE_DESC",
                "Adjusts frame generation to reach Target FPS. The steady base cap is the default for smoother pacing. Enable Fractional Adaptive to keep more real frames, but test it per game.",
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
                    "Desired displayed FPS. Fractional Adaptive may mix ratios to reach it; Steady Base Cap starts at half the target and can align a validated lower integer rung.",
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
                          "The default Adaptive mode. Starts at half the target; with Smooth Cadence it can align a validated 3x–5x rung. Pros: usually smoother pacing. Cons: fewer real frames and potentially more input lag and ghosting.",
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
                  onChange={(value) =>
                    onConfigUpdate(steadyBaseCapChanges(value))
                  }
                />
              </PanelSectionRow>
              <PanelSectionRow>
                <SliderField
                  label={`${t("ADAPTIVE_MAX_MULTIPLIER", "Maximum Adaptive Multiplier")} (${adaptiveMaxMultiplier}x)`}
                  description={
                    <span style={{ display: "block", paddingBottom: "2px" }}>
                      {t(
                        "ADAPTIVE_MAX_MULTIPLIER_DESC",
                        "Interpolation ceiling. 3x is balanced; 2x usually looks best, 4x gives more headroom, and 5x is for high-refresh displays with substantial GPU and memory headroom. Test per game.",
                      )}
                    </span>
                  }
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
            </>
          )}

          <PanelSectionRow>
            <Field
              label={t("FRAME_GENERATION_FACTOR", "Frame Generation Factor")}
              description={
                <>
                  <span style={{ display: "block", paddingTop: "8px" }}>
                    {t(
                      "FRAME_GENERATION_FACTOR_DESC",
                      "0x pauses generation live. Fixed mode uses 2x–5x; 5x is a high-cost option for high-refresh displays. Adaptive manages its own multiplier while retaining the same live 0x pause.",
                    )}
                  </span>
                  {config.adaptive && (
                    <MakoSettingRelationship>
                      {t(
                        "FRAME_GENERATION_FACTOR_ADAPTIVE_RELATION",
                        "The 2x–5x Fixed factors are unavailable in Adaptive mode; 0x can still pause it live.",
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
                {factorChoices.map((choice, index) => {
                  const selected = choice.enabled
                    ? frameGenerationEnabled &&
                      (config.adaptive ||
                        choice.multiplier === config.multiplier)
                    : !frameGenerationEnabled;
                  return (
                    <DialogButton
                      key={choice.id}
                      className="Mako_DialogButton"
                      style={{
                        ...multiplierButtonStyle(
                          focusedControl === choice.id,
                          selected,
                        ),
                        marginLeft: index === 0 ? "0px" : "7px",
                      }}
                      onClick={() => {
                        if (!choice.enabled) {
                          void onConfigChange(FRAME_GENERATION_ENABLED, false);
                          return;
                        }
                        if (config.adaptive) {
                          void onConfigChange(FRAME_GENERATION_ENABLED, true);
                          return;
                        }
                        void onConfigUpdate({
                          frame_generation_enabled: true,
                          multiplier:
                            choice.multiplier ?? FIXED_MULTIPLIER_UI_MIN,
                        });
                      }}
                      onGamepadFocus={() => setFocusedControl(choice.id)}
                      onGamepadBlur={() =>
                        setFocusedControl((current) =>
                          current === choice.id ? null : current,
                        )
                      }
                    >
                      {choice.label}
                    </DialogButton>
                  );
                })}
              </MakoFocusable>
            </Field>
          </PanelSectionRow>

          <PanelSectionRow>
            <ToggleField
              label={t("ADAPTIVE_SMOOTH_CADENCE", "Smooth Cadence")}
              description={t(
                "ADAPTIVE_SMOOTH_CADENCE_DESC",
                "Uses validated ordered Gamescope presentation for steadier pacing. Fractional Adaptive keeps real frames; Fixed and Steady Base Cap can favor even output. It may reduce real FPS and responsiveness. On by default; turn it off per game if preferred.",
              )}
              checked={
                config.adaptive_stable_cadence ??
                DEFAULT_CONFIGURATION.adaptive_stable_cadence
              }
              onChange={(value) =>
                onConfigChange(ADAPTIVE_STABLE_CADENCE, value)
              }
            />
          </PanelSectionRow>

          <PanelSectionRow>
            <ToggleField
              label={t("CONFIG_PERFORMANCE_MODE", "Lighter FG Model")}
              description={t(
                "CONFIG_PERFORMANCE_MODE_DESC",
                "Reduces GPU work by using a lighter frame-generation model at the cost of more ghosting. Ultra Performance locks this on.",
              )}
              checked={config.ultra_performance || config.performance_mode}
              disabled={config.ultra_performance}
              onChange={(value) => onConfigChange(PERFORMANCE_MODE, value)}
              bottomSeparator="none"
            />
          </PanelSectionRow>
        </>
      )}
    </>
  );
}
