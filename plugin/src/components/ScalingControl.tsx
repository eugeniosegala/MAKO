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
  SCALING_SHARPNESS,
  SCALING_SHARPNESS_MAX,
  SCALING_SHARPNESS_MIN,
  type ConfigurationData,
} from "../config/configSchema";
import t from "../i18n/i18n";

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
  const scalingMethodOptions = [
    {
      data: SCALING_METHOD_MAKO,
      label: t("SCALING_METHOD_MAKO", "MAKO (Open)"),
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
          label={t("SCALING_ENABLED", "Enable Scaling (Live)")}
          description={t(
            "SCALING_ENABLED_DESC",
            "To activate, select an in-game resolution below the display resolution, enable Scaling, and choose a method. Changes apply live through one game-owned swapchain recreation; a brief flicker is normal. Scaling can run alone or before Frame Generation.",
          )}
          checked={config.scaling_enabled}
          disabled={disabled}
          onChange={(value) => onConfigChange(SCALING_ENABLED, value)}
        />
      </PanelSectionRow>

      {config.scaling_enabled && (
        <>
          <PanelSectionRow>
            <Field
              label={t("SCALING_METHOD", "Scaling Method (Live)")}
              description={t(
                "SCALING_METHOD_DESC",
                "MAKO is the built-in open single-pass scaler and does not need Lossless.dll. LS1 Quality uses Lossless Scaling's proprietary full neural network; LS1 Performance uses its lower-cost network. If LS1 cannot start, MAKO takes over for that swapchain. Method changes apply live through swapchain recreation.",
              )}
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
              label={`${t("SCALING_FACTOR", "Scale Factor (Live)")} (${config.scaling_factor.toFixed(1)}x)`}
              description={t(
                "SCALING_FACTOR_DESC",
                "Sets the output-to-input size ratio. Higher values upscale from a smaller source image. Changes apply live through swapchain recreation.",
              )}
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

          <PanelSectionRow>
            <SliderField
              label={`${t("SCALING_SHARPNESS", "Scaling Sharpness (Live)")} (${Math.round(config.scaling_sharpness * 100)}%)`}
              description={t(
                "SCALING_SHARPNESS_DESC",
                "For MAKO, controls continuous edge sharpening. For LS1, selects one of five learned sharpness variants. Changes apply live through swapchain recreation.",
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
        </>
      )}
    </>
  );
}
