import {
  Dropdown,
  Field,
  PanelSectionRow,
  SliderField,
  ToggleField,
} from "@decky/ui";
import {
  SCALING_ENABLED,
  SCALING_FACTOR,
  SCALING_FACTOR_MAX,
  SCALING_FACTOR_MIN,
  SCALING_METHOD,
  SCALING_METHOD_LS1,
  SCALING_METHOD_LS1_PERFORMANCE,
  SCALING_METHOD_MAKO,
  SCALING_METHOD_NATIVE,
  SCALING_SHARPNESS,
  SCALING_SHARPNESS_MAX,
  SCALING_SHARPNESS_MIN,
  type ConfigurationData,
} from "../config/configSchema";
import t from "../i18n/i18n";
import { MakoInlineTip, MakoRestartLabel } from "./MakoUi";

interface ScalingControlProps {
  config: ConfigurationData;
  disabled?: boolean;
  onConfigChange: (
    fieldName: keyof ConfigurationData,
    value: boolean | number | string,
  ) => Promise<void>;
}

export function ScalingControl({
  config,
  disabled = false,
  onConfigChange,
}: ScalingControlProps) {
  const scalerActive = config.scaling_method !== SCALING_METHOD_NATIVE;
  const scalingMethodOptions = [
    {
      data: SCALING_METHOD_NATIVE,
      label: t("SCALING_METHOD_NATIVE", "Native Resolution"),
    },
    {
      data: SCALING_METHOD_MAKO,
      label: t("SCALING_METHOD_MAKO", "MAKO Scaler"),
    },
    {
      data: SCALING_METHOD_LS1,
      label: t("SCALING_METHOD_LS1", "LS1 Quality"),
    },
    {
      data: SCALING_METHOD_LS1_PERFORMANCE,
      label: t("SCALING_METHOD_LS1_PERFORMANCE", "LS1 Performance"),
    },
  ];

  return (
    <>
      <PanelSectionRow>
        <ToggleField
          label={
            <MakoRestartLabel
              label={t("SCALING_ENABLED", "Enable Scaling (Restart)")}
            />
          }
          description={
            <>
              <div>
                {t(
                  "SCALING_ENABLED_DESC",
                  "Enable before starting the game. When off, scaling is fully disabled. Supports Lossless Scaling models and MAKO's own scaler.",
                )}
              </div>
              <MakoInlineTip tone="warning">
                {t(
                  "SCALING_ENABLED_WARNING",
                  "Leave scaling off when you do not need it, as it consumes resources.",
                )}
              </MakoInlineTip>
            </>
          }
          checked={config.scaling_enabled}
          disabled={disabled}
          onChange={(value) => onConfigChange(SCALING_ENABLED, value)}
        />
      </PanelSectionRow>

      {config.scaling_enabled && (
        <>
          <PanelSectionRow>
            <Field
              label={t("SCALING_METHOD", "Scaling Method")}
              description={
                <>
                  <div>
                    {t(
                      "SCALING_METHOD_DESC",
                      "Choose the scaling model. You can change it while the game is running.",
                    )}
                  </div>
                  <MakoInlineTip tone="info">
                    <span style={{ whiteSpace: "pre-line" }}>
                      {t(
                        "SCALING_METHOD_COMPARISON_TIP",
                        "How scaling works:\n1. In Steam, set Game Resolution to your display's maximum resolution (Steam Deck: 1280 × 800; Steam Machine: 3840 × 2160 / 4K).\n2. In the game, choose a lower resolution, such as 480p or 720p.\n3. Use a Scale Factor to enlarge the image. 2x doubles your resolution.\n\nReducing the resolution of the game and scaling it back can substantially increase performance, with an image-quality trade-off. If your display supports it, use MAKO to scale from 2K to 4K.",
                      )}
                    </span>
                  </MakoInlineTip>
                </>
              }
              childrenLayout="below"
              childrenContainerWidth="max"
            >
              <Dropdown
                rgOptions={scalingMethodOptions}
                selectedOption={config.scaling_method}
                disabled={disabled}
                onChange={(option) =>
                  onConfigChange(SCALING_METHOD, String(option.data))
                }
              />
            </Field>
          </PanelSectionRow>

          <PanelSectionRow>
            <SliderField
              label={`${t("SCALING_FACTOR", "Scale Factor")} (${config.scaling_factor.toFixed(1)}x)`}
              description={
                <span style={{ display: "block", paddingTop: "3px" }}>
                  {t(
                    "SCALING_FACTOR_DESC",
                    "Sets the output-to-input size ratio for every method, including Native Resolution. Higher values render fewer source pixels. After the control settles, MAKO requests one guarded game-owned recreation when supported; otherwise it applies on the next natural resolution change or restart.",
                  )}
                </span>
              }
              value={config.scaling_factor}
              min={SCALING_FACTOR_MIN}
              max={SCALING_FACTOR_MAX}
              step={0.1}
              validValues="steps"
              minimumDpadGranularity={0.1}
              notchCount={11}
              notchTicksVisible
              disabled={disabled}
              onChange={(value) =>
                onConfigChange(SCALING_FACTOR, Number(value.toFixed(1)))
              }
            />
          </PanelSectionRow>

          {scalerActive && (
            <PanelSectionRow>
              <SliderField
                label={`${t("SCALING_SHARPNESS", "Scaling Sharpness")} (${Math.round(config.scaling_sharpness * 100)}%)`}
                description={t(
                  "SCALING_SHARPNESS_DESC",
                  "For MAKO, applies this 0–100% multiplier to its 2x sharpening baseline. For LS1, selects one of five learned sharpness variants. Applies through a private scaler rebuild.",
                )}
                value={config.scaling_sharpness}
                min={SCALING_SHARPNESS_MIN}
                max={SCALING_SHARPNESS_MAX}
                step={0.01}
                disabled={disabled}
                bottomSeparator="none"
                onChange={(value) =>
                  onConfigChange(SCALING_SHARPNESS, Number(value.toFixed(2)))
                }
              />
            </PanelSectionRow>
          )}
        </>
      )}
    </>
  );
}
