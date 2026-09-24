import { useRef, useState } from "react";
import {
  ButtonItem,
  DialogBody,
  DialogButton,
  DialogHeader,
  Dropdown,
  Field,
  ModalRoot,
  PanelSectionRow,
  SliderField,
  ToggleField,
  showModal,
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
  VKBASALT_SHADER,
  VKBASALT_SHADER_BLEACH_BYPASS,
  VKBASALT_SHADER_CARTOON,
  VKBASALT_SHADER_CHROMATIC_ABERRATION,
  VKBASALT_SHADER_CLARITY,
  VKBASALT_SHADER_COLOURFULNESS,
  VKBASALT_SHADER_CURVES,
  VKBASALT_SHADER_DEBAND,
  VKBASALT_SHADER_DPX,
  VKBASALT_SHADER_FILM_GRAIN,
  VKBASALT_SHADER_HDR_LOOK,
  VKBASALT_SHADER_LEVELS_PLUS,
  VKBASALT_SHADER_MONOCHROME,
  VKBASALT_SHADER_NOIR,
  VKBASALT_SHADER_NONE,
  VKBASALT_SHADER_NOSTALGIA,
  VKBASALT_SHADER_SEPIA,
  VKBASALT_SHADER_TECHNICOLOR,
  VKBASALT_SHADER_TECHNICOLOR2,
  VKBASALT_SHADER_VIBRANCE,
  VKBASALT_SHADER_VIGNETTE,
  VKBASALT_STRENGTH_MAX,
  VKBASALT_STRENGTH_MIN,
} from "../../config/configSchema";
import t from "../../i18n/i18n";
import {
  MakoExperimentalSettingLabel,
  MakoFocusable,
  MakoInlineTip,
} from "../MakoUi";
import type { ConfigurationControlProps } from "./types";

interface ShadersConfigurationGroupProps extends ConfigurationControlProps {
  isDefaultProfile: boolean;
  vkBasaltConfigPath: string;
}

interface EffectOption {
  data: string;
  label: string;
}

function EffectsPickerModal({
  options,
  initialSelection,
  onChange,
  closeModal,
}: {
  options: EffectOption[];
  initialSelection: string[];
  onChange: (value: string) => Promise<void>;
  closeModal?: () => void;
}) {
  const [selected, setSelected] = useState(initialSelection);
  const pendingSave = useRef(Promise.resolve());
  const updateSelection = (next: string[]) => {
    setSelected(next);
    const value = next.join(":") || VKBASALT_SHADER_NONE;
    pendingSave.current = pendingSave.current.catch(() => {}).then(() => onChange(value));
  };

  return (
    <ModalRoot closeModal={closeModal}>
      <DialogHeader>{t("CONFIG_VKBASALT_SHADER", "Effects")}</DialogHeader>
      <DialogBody>
        <div style={{ padding: "8px 12px", maxHeight: "58vh", overflowY: "auto" }}>
          <MakoFocusable flow-children="column">
            {options.filter((option) => option.data !== VKBASALT_SHADER_NONE).map((option) => (
              <ToggleField
                key={option.data}
                label={selected.includes(option.data)
                  ? `${selected.indexOf(option.data) + 1}. ${option.label}`
                  : option.label}
                checked={selected.includes(option.data)}
                onChange={(enabled) =>
                  updateSelection(
                    enabled
                      ? [...selected, option.data]
                      : selected.filter((effect) => effect !== option.data),
                  )
                }
              />
            ))}
          </MakoFocusable>
        </div>
      </DialogBody>
      <MakoFocusable
        flow-children="row"
        style={{ display: "flex", justifyContent: "flex-end", gap: "8px", padding: "12px" }}
      >
        <DialogButton onClick={() => updateSelection([])}>
          {t("CONFIG_VKBASALT_EFFECTS_CLEAR", "Clear all")}
        </DialogButton>
        <DialogButton onClick={closeModal}>
          {t("CONFIG_VKBASALT_EFFECTS_DONE", "Done")}
        </DialogButton>
      </MakoFocusable>
    </ModalRoot>
  );
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
  const selectedEffects =
    config.vkbasalt_shader === VKBASALT_SHADER_NONE
      ? []
      : config.vkbasalt_shader.split(":");
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
  const shaderOptions = [
    {
      data: VKBASALT_SHADER_NONE,
      label: t("CONFIG_VKBASALT_EFFECT_NONE", "Off"),
    },
    {
      data: VKBASALT_SHADER_HDR_LOOK,
      label: t("CONFIG_VKBASALT_SHADER_HDR_LOOK", "HDR Look (SDR)"),
    },
    {
      data: VKBASALT_SHADER_CLARITY,
      label: t("CONFIG_VKBASALT_SHADER_CLARITY", "Clarity"),
    },
    {
      data: VKBASALT_SHADER_LEVELS_PLUS,
      label: t("CONFIG_VKBASALT_SHADER_LEVELS_PLUS", "Levels Plus"),
    },
    {
      data: VKBASALT_SHADER_VIBRANCE,
      label: t("CONFIG_VKBASALT_SHADER_VIBRANCE", "Vibrance"),
    },
    {
      data: VKBASALT_SHADER_COLOURFULNESS,
      label: t("CONFIG_VKBASALT_SHADER_COLOURFULNESS", "Colourfulness"),
    },
    {
      data: VKBASALT_SHADER_CURVES,
      label: t("CONFIG_VKBASALT_SHADER_CURVES", "Curves"),
    },
    {
      data: VKBASALT_SHADER_DEBAND,
      label: t("CONFIG_VKBASALT_SHADER_DEBAND", "Deband"),
    },
    {
      data: VKBASALT_SHADER_TECHNICOLOR2,
      label: t("CONFIG_VKBASALT_SHADER_TECHNICOLOR2", "Technicolor 2"),
    },
    {
      data: VKBASALT_SHADER_DPX,
      label: t("CONFIG_VKBASALT_SHADER_DPX", "DPX / Cineon"),
    },
    {
      data: VKBASALT_SHADER_BLEACH_BYPASS,
      label: t("CONFIG_VKBASALT_SHADER_BLEACH_BYPASS", "Bleach Bypass"),
    },
    {
      data: VKBASALT_SHADER_NOIR,
      label: t("CONFIG_VKBASALT_SHADER_NOIR", "Noir"),
    },
    {
      data: VKBASALT_SHADER_TECHNICOLOR,
      label: t("CONFIG_VKBASALT_SHADER_TECHNICOLOR", "Technicolor"),
    },
    {
      data: VKBASALT_SHADER_MONOCHROME,
      label: t("CONFIG_VKBASALT_SHADER_MONOCHROME", "Monochrome"),
    },
    {
      data: VKBASALT_SHADER_SEPIA,
      label: t("CONFIG_VKBASALT_SHADER_SEPIA", "Sepia"),
    },
    {
      data: VKBASALT_SHADER_FILM_GRAIN,
      label: t("CONFIG_VKBASALT_SHADER_FILM_GRAIN", "Film Grain"),
    },
    {
      data: VKBASALT_SHADER_VIGNETTE,
      label: t("CONFIG_VKBASALT_SHADER_VIGNETTE", "Vignette"),
    },
    {
      data: VKBASALT_SHADER_CARTOON,
      label: t("CONFIG_VKBASALT_SHADER_CARTOON", "Cartoon"),
    },
    {
      data: VKBASALT_SHADER_NOSTALGIA,
      label: t("CONFIG_VKBASALT_SHADER_NOSTALGIA", "Nostalgia"),
    },
    {
      data: VKBASALT_SHADER_CHROMATIC_ABERRATION,
      label: t(
        "CONFIG_VKBASALT_SHADER_CHROMATIC_ABERRATION",
        "Chromatic Aberration",
      ),
    },
  ];

  return (
    <>
      <PanelSectionRow>
        <ToggleField
          label={
            <MakoExperimentalSettingLabel
              label={t("CONFIG_ENABLE_VKBASALT", "Enable Shaders (Restart)")}
              badgeLabel={t("EXPERIMENTAL_LABEL", "Experimental")}
            />
          }
          description={t(
            "CONFIG_ENABLE_VKBASALT_DESC",
            "Enable before starting the game. Applies MAKO's bundled sharpening, anti-aliasing, and shader effects. No separate installation is needed. If effects are not visible, switch between Windowed and Fullscreen.",
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
              label={t("CONFIG_VKBASALT_SHADER", "Effects")}
              description={t(
                "CONFIG_VKBASALT_SHADER_DESC",
                "Check any effects to combine them. They run in selection order; uncheck and recheck to move one to the end. HDR Look is an SDR visual effect, not HDR output.",
              )}
              childrenLayout="below"
              childrenContainerWidth="max"
            >
              <ButtonItem
                layout="below"
                bottomSeparator="none"
                onClick={() =>
                  showModal(
                    <EffectsPickerModal
                      options={shaderOptions}
                      initialSelection={selectedEffects}
                      onChange={(value) => onConfigChange(VKBASALT_SHADER, value)}
                    />,
                  )
                }
              >
                {t("CONFIG_VKBASALT_EFFECTS_SELECTED", "Choose effects ({value} selected)", {
                  value: selectedEffects.length,
                })}
              </ButtonItem>
            </Field>
          </PanelSectionRow>

          <PanelSectionRow>
            <Field
              label={t("CONFIG_VKBASALT_SHARPENING", "Sharpening")}
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
                label={t("CONFIG_VKBASALT_SHARPNESS", "Sharpness ({value}%)", {
                  value: Math.round(config.vkbasalt_sharpness * 100),
                })}
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
                label={t(
                  "CONFIG_VKBASALT_DLS_DENOISE",
                  "DLS Denoise ({value}%)",
                  { value: Math.round(config.vkbasalt_dls_denoise * 100) },
                )}
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
              label={t("CONFIG_VKBASALT_ANTIALIASING", "Anti-aliasing")}
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
                    "Advanced options can be edited in {path}. MAKO merges only the controls above and preserves every other setting. Manual advanced changes apply on the next launch. The Default profile uses this global file.",
                    { path: displayedConfigPath },
                  )
                : t(
                    "CONFIG_VKBASALT_ADVANCED_PROFILE_NOTE",
                    "Advanced options can be edited in {path}. MAKO merges only the controls above and preserves every other setting. Manual advanced changes apply on the next launch. This file belongs to the selected profile and is removed when that profile is deleted.",
                    { path: displayedConfigPath },
                  )}
            </MakoInlineTip>
          </PanelSectionRow>
        </>
      )}
    </>
  );
}
