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
          label={t("SCALING_ENABLED", "Enable Scaling Engine (Restart)")}
          description={t(
            "SCALING_ENABLED_DESC",
            "Enables MAKO's Gamescope-backed scaling path for the next game launch. Once running, model and sharpness changes rebuild only MAKO's private scaler and do not recreate the game's swapchain. Scale Factor applies after the game's next natural resolution change. The engine can run alone or before Frame Generation.",
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
              label={t("SCALING_METHOD", "Scaling Method")}
              description={t(
                "SCALING_METHOD_DESC",
                "Native Resolution uses a fast model-free linear baseline at the selected scale factor. MAKO Scaler is the open single-pass option. LS1 Quality and Performance use the licensed Lossless Scaling models; if LS1 cannot start, MAKO Scaler takes over. Model changes apply inside MAKO without recreating the game swapchain.",
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
              label={`${t("SCALING_FACTOR", "Scale Factor")} (${config.scaling_factor.toFixed(1)}x)`}
              description={t(
                "SCALING_FACTOR_DESC",
                "Sets the output-to-input size ratio for every method, including Native Resolution. Higher values render fewer source pixels. Applies on the game's next natural resolution change or restart.",
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

          {scalerActive && (
              <PanelSectionRow>
                <SliderField
                  label={`${t("SCALING_SHARPNESS", "Scaling Sharpness")} (${Math.round(config.scaling_sharpness * 100)}%)`}
                  description={t(
                    "SCALING_SHARPNESS_DESC",
                    "For MAKO, controls continuous edge sharpening. For LS1, selects one of five learned sharpness variants. Applies through a private scaler rebuild.",
                  )}
                  value={config.scaling_sharpness}
                  min={SCALING_SHARPNESS_MIN}
                  max={SCALING_SHARPNESS_MAX}
                  step={0.01}
                  disabled={disabled}
                  bottomSeparator="none"
                  onChange={(value) =>
                    onConfigChange(
                      SCALING_SHARPNESS,
                      Number(value.toFixed(2)),
                    )
                  }
                />
              </PanelSectionRow>
          )}
        </>
      )}
    </>
  );
}
