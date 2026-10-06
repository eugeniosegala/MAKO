import { useState, type CSSProperties } from "react";
import { FileSelectionType, openFilePicker } from "@decky/api";
import type { CustomShaderEffect } from "../../api/makoApi";
import {
  DialogButton,
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
import { MakoInfo } from "../MakoInfo";
import {
  MakoExperimentalSettingLabel,
  MakoFocusable,
  MakoInlineTip,
  makoDialogButtonStyle,
} from "../MakoUi";
import type { ConfigurationControlProps } from "./types";
import { EffectsChecklist } from "./shaders/EffectsChecklist";

interface ShadersConfigurationGroupProps extends ConfigurationControlProps {
  isDefaultProfile: boolean;
  profileName: string;
  vkBasaltConfigPath: string;
  customShaderEffects?: CustomShaderEffect[];
  onAddShader?: (path: string) => Promise<void>;
  onRefreshShaders?: () => Promise<void>;
  onDeleteShaders?: (shaderIds: string[]) => Promise<void>;
}

const customShaderButtonStyle: CSSProperties = {
  flex: "1 1 0",
  minWidth: 0,
  minHeight: "34px",
  height: "auto",
  boxSizing: "border-box",
  display: "flex",
  alignItems: "center",
  justifyContent: "center",
  padding: "4px 8px",
  margin: 0,
  fontSize: "12px",
  lineHeight: 1.2,
  whiteSpace: "normal",
  overflowWrap: "anywhere",
};

export function ShadersConfigurationGroup({
  config,
  isDefaultProfile,
  profileName,
  vkBasaltConfigPath,
  customShaderEffects = [],
  onAddShader,
  onRefreshShaders,
  onDeleteShaders,
  onConfigChange,
}: ShadersConfigurationGroupProps) {
  const [busy, setBusy] = useState(false);
  const [shaderError, setShaderError] = useState("");
  const [focusedAction, setFocusedAction] = useState<"delete" | null>(null);
  const runShaderAction = async (action: () => Promise<void>) => {
    setBusy(true);
    setShaderError("");
    try {
      await action();
    } catch (error) {
      setShaderError(String(error));
    } finally {
      setBusy(false);
    }
  };
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
  const shaderOptions: { data: string; label: string }[] = [
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

  const customIds = new Set(customShaderEffects.map((effect) => effect.id));
  shaderOptions.push(
    ...customShaderEffects.map((effect) => ({
      data: effect.id,
      label: t("CONFIG_VKBASALT_CUSTOM_LABEL", "Custom: {name}", {
        name: effect.name,
      }),
    })),
  );
  shaderOptions.push(
    ...selectedEffects
      .filter((id) => id.startsWith("custom/") && !customIds.has(id))
      .map((id) => ({
        data: id,
        label: t(
          "CONFIG_VKBASALT_CUSTOM_MISSING",
          "Custom: {name} (definition missing)",
          { name: id.slice(7) },
        ),
      })),
  );

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
            "Enable before launch for bundled sharpening, anti-aliasing, and shaders. No separate install. If effects are invisible, try Windowed or Borderless Fullscreen mode.",
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
              childrenLayout="below"
              childrenContainerWidth="max"
            >
              <MakoFocusable
                flow-children="column"
                noFocusRing
                style={{ width: "100%", minWidth: 0, paddingBottom: "6px" }}
              >
                <EffectsChecklist
                  key={profileName}
                  options={shaderOptions}
                  initialSelection={selectedEffects}
                  disabled={busy}
                  onChange={(value) => onConfigChange(VKBASALT_SHADER, value)}
                />
                <MakoInfo
                  className="Mako_OptionDescription"
                  data-mako-info="true"
                  style={{
                    marginTop: "8px",
                    color: "#acb2b8",
                    fontSize: "10px",
                    lineHeight: "14px",
                  }}
                >
                  {t(
                    "CONFIG_VKBASALT_SHADER_DESC",
                    "Effects run in selection order; uncheck and recheck to move one last. Stacking increases GPU load, especially with heavier effects such as HDR Look. Test per game and monitor GPU usage.",
                  )}
                </MakoInfo>

                {(onAddShader || onRefreshShaders) && (
                  <MakoFocusable
                    flow-children="row"
                    noFocusRing
                    style={{
                      display: "flex",
                      alignItems: "stretch",
                      gap: "12px",
                      width: "100%",
                      minWidth: 0,
                      boxSizing: "border-box",
                      padding: "18px 4px 8px",
                    }}
                  >
                    {onAddShader && (
                      <DialogButton
                        style={customShaderButtonStyle}
                        disabled={busy}
                        onClick={() =>
                          runShaderAction(async () => {
                            const file = await openFilePicker(
                              FileSelectionType.FILE,
                              "/home",
                              true,
                              true,
                              undefined,
                              ["fx"],
                              false,
                              false,
                            ).catch(() => null);
                            if (file?.realpath || file?.path)
                              await onAddShader(file.realpath || file.path);
                          })
                        }
                      >
                        {t("CONFIG_VKBASALT_CUSTOM_ADD", "Add Custom Shader")}
                      </DialogButton>
                    )}
                    {onRefreshShaders && (
                      <DialogButton
                        style={customShaderButtonStyle}
                        disabled={busy}
                        onClick={() => runShaderAction(onRefreshShaders)}
                      >
                        {t("CONFIG_VKBASALT_CUSTOM_REFRESH", "Refresh")}
                      </DialogButton>
                    )}
                  </MakoFocusable>
                )}
                <MakoInfo style={{ width: "100%", paddingBottom: "8px" }}>
                  <MakoInlineTip tone="info">
                    {t(
                      "CONFIG_VKBASALT_CUSTOM_HELP",
                      "Add a vkBasalt-compatible ReShade .fx file, then select it in Effects. Keep its includes and textures accessible at their original paths. Refresh to discover entries added to this profile’s file. Custom shader changes require a game restart.",
                    )}
                  </MakoInlineTip>
                </MakoInfo>
                {onDeleteShaders && (
                  <div
                    style={{
                      width: "100%",
                      boxSizing: "border-box",
                      padding: "4px 4px 8px",
                    }}
                  >
                    <DialogButton
                      className="Mako_DialogButton Mako_DialogButton--danger"
                      style={{
                        ...customShaderButtonStyle,
                        width: "100%",
                        ...makoDialogButtonStyle(
                          focusedAction === "delete",
                          "danger",
                        ),
                      }}
                      onGamepadFocus={() => setFocusedAction("delete")}
                      onGamepadBlur={() => setFocusedAction(null)}
                      disabled={
                        busy ||
                        !selectedEffects.some((id) => id.startsWith("custom/"))
                      }
                      onClick={() =>
                        runShaderAction(() =>
                          onDeleteShaders(
                            selectedEffects.filter((id) =>
                              id.startsWith("custom/"),
                            ),
                          ),
                        )
                      }
                    >
                      {t(
                        "CONFIG_VKBASALT_CUSTOM_DELETE",
                        "Delete selected custom shaders",
                      )}
                    </DialogButton>
                  </div>
                )}
                {shaderError && (
                  <MakoInlineTip tone="warning" alwaysVisible>
                    {shaderError}
                  </MakoInlineTip>
                )}
              </MakoFocusable>
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
