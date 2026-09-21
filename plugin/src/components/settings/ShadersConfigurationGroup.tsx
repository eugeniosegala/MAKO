import {
  Dropdown,
  Field,
  PanelSectionRow,
  SliderField,
  ToggleField,
} from "@decky/ui";
import {
  EXTERNAL_VULKAN_LAYER_NONE,
  EXTERNAL_VULKAN_LAYER_VKBASALT,
  EXTERNAL_VULKAN_LAYER,
  VKBASALT_ANTIALIASING,
  VKBASALT_ANTIALIASING_FXAA,
  VKBASALT_ANTIALIASING_NONE,
  VKBASALT_ANTIALIASING_SMAA,
  VKBASALT_DLS_DENOISE,
  VKBASALT_SHARPENING,
  VKBASALT_SHARPENING_CAS,
  VKBASALT_SHARPENING_DLS,
  VKBASALT_SHARPENING_NONE,
  VKBASALT_SHARPNESS,
  VKBASALT_STRENGTH_MAX,
  VKBASALT_STRENGTH_MIN,
} from "../../config/configSchema";
import t from "../../i18n/i18n";
import {
  MakoExperimentalSettingLabel,
  MakoInlineTip,
  MakoRestartLabel,
} from "../MakoUi";
import type { ConfigurationControlProps } from "./types";

interface ShadersConfigurationGroupProps extends ConfigurationControlProps {
  isDefaultProfile: boolean;
  vkBasaltConfigPath: string;
}

export function ShadersConfigurationGroup({
  config,
  isDefaultProfile,
  vkBasaltConfigPath,
  onConfigChange,
}: ShadersConfigurationGroupProps) {
  const vkBasaltEnabled =
    config.external_vulkan_layer === EXTERNAL_VULKAN_LAYER_VKBASALT;
  const sharpeningEnabled =
    config.vkbasalt_sharpening !== VKBASALT_SHARPENING_NONE;
  const displayedConfigPath =
    vkBasaltConfigPath ||
    (isDefaultProfile
      ? "~/.config/vkBasalt/vkBasalt.conf"
      : "~/.config/mako-render/vkbasalt/<profile>.conf");
  const sharpeningOptions = [
    {
      data: VKBASALT_SHARPENING_NONE,
      label: t("CONFIG_VKBASALT_EFFECT_NONE", "Off"),
    },
    {
      data: VKBASALT_SHARPENING_CAS,
      label: t("CONFIG_VKBASALT_SHARPENING_CAS", "CAS"),
    },
    {
      data: VKBASALT_SHARPENING_DLS,
      label: t("CONFIG_VKBASALT_SHARPENING_DLS", "DLS"),
    },
  ];
  const antialiasingOptions = [
    {
      data: VKBASALT_ANTIALIASING_NONE,
      label: t("CONFIG_VKBASALT_EFFECT_NONE", "Off"),
    },
    {
      data: VKBASALT_ANTIALIASING_FXAA,
      label: t("CONFIG_VKBASALT_ANTIALIASING_FXAA", "FXAA"),
    },
    {
      data: VKBASALT_ANTIALIASING_SMAA,
      label: t("CONFIG_VKBASALT_ANTIALIASING_SMAA", "SMAA"),
    },
  ];

  return (
    <>
      <PanelSectionRow>
        <ToggleField
          label={
            <MakoExperimentalSettingLabel
              label={t("CONFIG_ENABLE_VKBASALT", "Enable vkBasalt (Restart)")}
              badgeLabel={t("EXPERIMENTAL_LABEL", "Experimental")}
            />
          }
          description={t(
            "CONFIG_ENABLE_VKBASALT_DESC",
            "Applies sharpening, anti-aliasing, and other configured effects using only MAKO's private bundled vkBasalt. A separate installation is neither needed nor used. If effects are not visible, switch between Windowed and Fullscreen.",
          )}
          bottomSeparator={vkBasaltEnabled ? undefined : "none"}
          checked={vkBasaltEnabled}
          onChange={(value) =>
            onConfigChange(
              EXTERNAL_VULKAN_LAYER,
              value
                ? EXTERNAL_VULKAN_LAYER_VKBASALT
                : EXTERNAL_VULKAN_LAYER_NONE,
            )
          }
        />
      </PanelSectionRow>

      {vkBasaltEnabled && (
        <>
          <PanelSectionRow>
            <Field
              label={
                <MakoRestartLabel
                  label={t(
                    "CONFIG_VKBASALT_SHARPENING",
                    "Sharpening (Restart)",
                  )}
                />
              }
              description={t(
                "CONFIG_VKBASALT_SHARPENING_DESC",
                "CAS is a crisp general-purpose sharpener. DLS can preserve noisy or grainy detail better when paired with denoise.",
              )}
              childrenLayout="below"
              childrenContainerWidth="max"
            >
              <Dropdown
                rgOptions={sharpeningOptions}
                selectedOption={config.vkbasalt_sharpening}
                onChange={(option) =>
                  onConfigChange(VKBASALT_SHARPENING, String(option.data))
                }
              />
            </Field>
          </PanelSectionRow>

          {sharpeningEnabled && (
            <PanelSectionRow>
              <SliderField
                label={
                  <MakoRestartLabel
                    label={t(
                      "CONFIG_VKBASALT_SHARPNESS",
                      "Sharpness ({value}%) (Restart)",
                      { value: Math.round(config.vkbasalt_sharpness * 100) },
                    )}
                  />
                }
                description={t(
                  "CONFIG_VKBASALT_SHARPNESS_DESC",
                  "Higher values produce a stronger effect but can exaggerate grain and create halos around high-contrast edges.",
                )}
                value={config.vkbasalt_sharpness}
                min={VKBASALT_STRENGTH_MIN}
                max={VKBASALT_STRENGTH_MAX}
                step={0.01}
                onChange={(value) => onConfigChange(VKBASALT_SHARPNESS, value)}
              />
            </PanelSectionRow>
          )}

          {config.vkbasalt_sharpening === VKBASALT_SHARPENING_DLS && (
            <PanelSectionRow>
              <SliderField
                label={
                  <MakoRestartLabel
                    label={t(
                      "CONFIG_VKBASALT_DLS_DENOISE",
                      "DLS Denoise ({value}%) (Restart)",
                      { value: Math.round(config.vkbasalt_dls_denoise * 100) },
                    )}
                  />
                }
                description={t(
                  "CONFIG_VKBASALT_DLS_DENOISE_DESC",
                  "Limits how strongly DLS sharpens film grain and fine noise.",
                )}
                value={config.vkbasalt_dls_denoise}
                min={VKBASALT_STRENGTH_MIN}
                max={VKBASALT_STRENGTH_MAX}
                step={0.01}
                onChange={(value) =>
                  onConfigChange(VKBASALT_DLS_DENOISE, value)
                }
              />
            </PanelSectionRow>
          )}

          <PanelSectionRow>
            <Field
              label={
                <MakoRestartLabel
                  label={t(
                    "CONFIG_VKBASALT_ANTIALIASING",
                    "Anti-aliasing (Restart)",
                  )}
                />
              }
              description={t(
                "CONFIG_VKBASALT_ANTIALIASING_DESC",
                "Optionally smooth jagged edges before sharpening. FXAA is lighter and softer; SMAA is more selective and may cost more GPU time.",
              )}
              childrenLayout="below"
              childrenContainerWidth="max"
            >
              <Dropdown
                rgOptions={antialiasingOptions}
                selectedOption={config.vkbasalt_antialiasing}
                onChange={(option) =>
                  onConfigChange(VKBASALT_ANTIALIASING, String(option.data))
                }
              />
            </Field>
          </PanelSectionRow>

          <PanelSectionRow>
            <MakoInlineTip tone="info">
              {isDefaultProfile
                ? t(
                    "CONFIG_VKBASALT_ADVANCED_GLOBAL_NOTE",
                    "Advanced options can be edited in {path}. MAKO merges only the controls above and preserves every other setting. The Default profile uses this global file.",
                    { path: displayedConfigPath },
                  )
                : t(
                    "CONFIG_VKBASALT_ADVANCED_PROFILE_NOTE",
                    "Advanced options can be edited in {path}. MAKO merges only the controls above and preserves every other setting. This file belongs to the selected profile and is removed when that profile is deleted.",
                    { path: displayedConfigPath },
                  )}
            </MakoInlineTip>
          </PanelSectionRow>
        </>
      )}
    </>
  );
}
