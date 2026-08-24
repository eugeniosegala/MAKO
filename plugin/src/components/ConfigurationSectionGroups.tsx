import {
  ButtonItem,
  PanelSectionRow,
  SliderField,
  TextField,
  ToggleField,
} from "@decky/ui";
import { RiArrowDownSFill, RiArrowUpSFill } from "react-icons/ri";
import {
  ACTIVE_IN,
  ALLOW_FP16,
  BASE_FPS_CAP_MIN,
  BASE_FPS_CAP_UI_MAX,
  DISABLE_MAKO,
  DISABLE_STEAMDECK_MODE,
  DLL,
  ENABLE_ZINK,
  EXTERNAL_VULKAN_LAYER_GAMESCOPE_WSI,
  EXTERNAL_VULKAN_LAYER_MANGOHUD,
  EXTERNAL_VULKAN_LAYER_NONE,
  EXTERNAL_VULKAN_LAYER_VKBASALT,
  EXTERNAL_VULKAN_LAYER,
  FLOW_SCALE_MAX,
  FLOW_SCALE_MIN,
  FLOW_SCALE,
  FORCE_ALSA_AUDIO,
  FRAME_GENERATION_REFRESH_THRESHOLD,
  FRAME_GENERATION_REFRESH_THRESHOLD_MAX,
  FRAME_GENERATION_REFRESH_THRESHOLD_PRESET,
  FRAME_GENERATION_REFRESH_THRESHOLD_UI_MIN,
  GPU,
  type ConfigurationData,
} from "../config/configSchema";
import {
  baseFpsCapChanges,
  dynamicCadenceRecoveryChanges,
} from "../config/fractionalAdaptivePreset";
import t from "../i18n/i18n";
import { MakoInlineWarning, MakoSectionHeader } from "./MakoUi";

type SaveConfigurationField = (
  fieldName: keyof ConfigurationData,
  value: boolean | number | string,
) => Promise<void>;

interface ConfigurationGroupProps {
  config: ConfigurationData;
  onConfigChange: SaveConfigurationField;
  collapsed: boolean;
  onToggle: () => void;
}

interface ConfigurationUpdateGroupProps extends ConfigurationGroupProps {
  onConfigUpdate: (changes: Partial<ConfigurationData>) => Promise<void>;
}

function CollapseControl({
  containerClassName,
  collapsed,
  onToggle,
}: {
  containerClassName: string;
  collapsed: boolean;
  onToggle: () => void;
}) {
  return (
    <PanelSectionRow>
      <div
        className={containerClassName}
        style={{ marginTop: "2px", marginBottom: "4px" }}
      >
        <ButtonItem layout="below" bottomSeparator="none" onClick={onToggle}>
          {collapsed ? (
            <RiArrowDownSFill
              style={{ transform: "translate(0, -13px)", fontSize: "1.5em" }}
            />
          ) : (
            <RiArrowUpSFill
              style={{ transform: "translate(0, -12px)", fontSize: "1.5em" }}
            />
          )}
        </ButtonItem>
      </div>
    </PanelSectionRow>
  );
}

export function AdvancedRenderingConfigurationGroup({
  config,
  onConfigChange,
  onConfigUpdate,
  collapsed,
  onToggle,
}: ConfigurationUpdateGroupProps) {
  return (
    <>
      <MakoSectionHeader topMargin="26px">
        {t("CONFIG_SECTION_TITLE", "Advanced Rendering Settings")}
      </MakoSectionHeader>

      <CollapseControl
        containerClassName="MAKO_ConfigCollapseButton_Container"
        collapsed={collapsed}
        onToggle={onToggle}
      />

      {!collapsed && (
        <>
          <PanelSectionRow>
            <SliderField
              label={`${t("CONFIG_FLOW_SCALE", "Flow Scale (Restart)")} (${Math.round(config.flow_scale * 100)}%)`}
              description={t(
                "CONFIG_FLOW_SCALE_DESC",
                "Controls internal motion-estimation resolution. Lower values reduce GPU work; higher values favour quality. Restart the game after changing it.",
              )}
              value={config.flow_scale}
              min={FLOW_SCALE_MIN}
              max={FLOW_SCALE_MAX}
              step={0.01}
              onChange={(value) => onConfigChange(FLOW_SCALE, value)}
            />
          </PanelSectionRow>

          <PanelSectionRow>
            <SliderField
              label={`${t("CONFIG_BASE_FPS_CAP", "Base FPS Cap")}${config.base_fps_cap > 0 ? ` (${config.base_fps_cap} FPS)` : ` (${t("CONFIG_BASE_FPS_CAP_OFF", "Off")})`}`}
              description={t(
                "CONFIG_BASE_FPS_CAP_DESC",
                "Caps real application frames before frame generation. Works with DirectX, OpenGL through Zink, and Vulkan; changes apply live.",
              )}
              value={config.base_fps_cap}
              min={BASE_FPS_CAP_MIN}
              max={BASE_FPS_CAP_UI_MAX}
              step={1}
              disabled={config.adaptive && config.adaptive_auto_base_fps_cap}
              onChange={(value) => onConfigUpdate(baseFpsCapChanges(value))}
            />
          </PanelSectionRow>

          <PanelSectionRow>
            <ToggleField
              label={t("CONFIG_ALLOW_FP16", "Allow FP16")}
              description={t(
                "CONFIG_ALLOW_FP16_DESC",
                "Improves performance on AMD; disable for older NVIDIA GPUs.",
              )}
              checked={config.allow_fp16}
              onChange={(value) => onConfigChange(ALLOW_FP16, value)}
            />
          </PanelSectionRow>

          <PanelSectionRow>
            <ToggleField
              label={t(
                "CONFIG_DISABLE_MAKO_NEXT_LAUNCH",
                "Disable MAKO Renderer on Next Launch",
              )}
              description={t(
                "CONFIG_DISABLE_MAKO_NEXT_LAUNCH_DESC",
                "Troubleshooting only. Stops MAKO Renderer loading after restart. Use Frame Generation above for live on/off.",
              )}
              checked={config.disable_mako}
              onChange={(value) => onConfigChange(DISABLE_MAKO, value)}
            />
          </PanelSectionRow>

          <PanelSectionRow>
            <ToggleField
              label={t(
                "CONFIG_FRAME_GENERATION_REFRESH_GUARD",
                "Auto-disable Frame Generation by Refresh Rate",
              )}
              description={t(
                "CONFIG_FRAME_GENERATION_REFRESH_GUARD_DESC",
                "Pauses frame generation when Gamescope confirms the current display is at or below the threshold, then resumes your selected mode above it. Does nothing when refresh feedback is unavailable.",
              )}
              bottomSeparator={
                config.frame_generation_refresh_threshold > 0
                  ? undefined
                  : "none"
              }
              checked={config.frame_generation_refresh_threshold > 0}
              onChange={(value) =>
                onConfigChange(
                  FRAME_GENERATION_REFRESH_THRESHOLD,
                  value ? FRAME_GENERATION_REFRESH_THRESHOLD_PRESET : 0,
                )
              }
            />
          </PanelSectionRow>

          {config.frame_generation_refresh_threshold > 0 && (
            <PanelSectionRow>
              <SliderField
                label={`${t(
                  "CONFIG_FRAME_GENERATION_REFRESH_THRESHOLD",
                  "Refresh Rate Threshold",
                )} (${config.frame_generation_refresh_threshold} Hz)`}
                description={t(
                  "CONFIG_FRAME_GENERATION_REFRESH_THRESHOLD_DESC",
                  "Choose the highest refresh rate where frame generation should remain paused. Changes apply live.",
                )}
                value={config.frame_generation_refresh_threshold}
                min={FRAME_GENERATION_REFRESH_THRESHOLD_UI_MIN}
                max={FRAME_GENERATION_REFRESH_THRESHOLD_MAX}
                step={1}
                onChange={(value) =>
                  onConfigChange(FRAME_GENERATION_REFRESH_THRESHOLD, value)
                }
              />
            </PanelSectionRow>
          )}
        </>
      )}
    </>
  );
}

export function CompatibilityConfigurationGroup({
  config,
  onConfigChange,
  onConfigUpdate,
  collapsed,
  onToggle,
}: ConfigurationUpdateGroupProps) {
  return (
    <>
      <MakoSectionHeader topMargin="26px">
        {t("CONFIG_WORKAROUNDS_TITLE", "Compatibility Settings")}
      </MakoSectionHeader>

      <CollapseControl
        containerClassName="MAKO_WorkaroundsCollapseButton_Container"
        collapsed={collapsed}
        onToggle={onToggle}
      />

      {!collapsed && (
        <>
          <PanelSectionRow>
            <ToggleField
              label={t("DYNAMIC_CADENCE_RECOVERY", "Dynamic Cadence Recovery")}
              description={t(
                "DYNAMIC_CADENCE_RECOVERY_DESC",
                "Recovers when a game or emulator changes native frame rate. Adaptive recalibrates; Fixed follows confirmed Gamescope refresh up to its multiplier. Enabling it briefly checks pacing, disables both base caps, and selects Fractional Adaptive. Changing Fractional or either cap turns Recovery off.",
              )}
              checked={config.dynamic_cadence_recovery}
              onChange={(value) =>
                onConfigUpdate(dynamicCadenceRecoveryChanges(value))
              }
            />
          </PanelSectionRow>

          <PanelSectionRow>
            <ToggleField
              label={t("CONFIG_DISABLE_HDR_EXPOSURE", "Disable HDR (Restart)")}
              description={t(
                "CONFIG_DISABLE_HDR_EXPOSURE_DESC",
                "HDR is unavailable in this release. This required setting keeps the stable SDR path active.",
              )}
              checked={true}
              disabled={true}
              onChange={() => undefined}
            />
          </PanelSectionRow>

          <PanelSectionRow>
            <ToggleField
              label={t(
                "CONFIG_GAMESCOPE_WSI_COMPATIBILITY",
                "Experimental Gamescope WSI (Restart)",
              )}
              description={
                <>
                  <div>
                    {t(
                      "CONFIG_GAMESCOPE_WSI_COMPATIBILITY_DESC",
                      "May reduce coloured or pixelated motion artifacts in affected games. Tested only with 64-bit native Vulkan or Proton games launched through Steam.",
                    )}
                  </div>
                  <MakoInlineWarning>
                    {t(
                      "CONFIG_GAMESCOPE_WSI_COMPATIBILITY_WARNING",
                      "Keep it off if not needed. It may reduce performance or interfere with frame generation.",
                    )}
                  </MakoInlineWarning>
                </>
              }
              checked={
                config.external_vulkan_layer ===
                EXTERNAL_VULKAN_LAYER_GAMESCOPE_WSI
              }
              onChange={(value) =>
                onConfigChange(
                  EXTERNAL_VULKAN_LAYER,
                  value
                    ? EXTERNAL_VULKAN_LAYER_GAMESCOPE_WSI
                    : EXTERNAL_VULKAN_LAYER_NONE,
                )
              }
            />
          </PanelSectionRow>

          <PanelSectionRow>
            <ToggleField
              label={t(
                "CONFIG_DISABLE_STEAMDECK_MODE",
                "Disable Steam Deck Mode",
              )}
              description={t(
                "CONFIG_DISABLE_STEAMDECK_MODE_DESC",
                "Disables Steam Deck mode. Unlocks hidden settings in some games.",
              )}
              checked={config.disable_steamdeck_mode}
              onChange={(value) =>
                onConfigChange(DISABLE_STEAMDECK_MODE, value)
              }
            />
          </PanelSectionRow>

          <PanelSectionRow>
            <ToggleField
              label={t("CONFIG_ENABLE_ZINK", "Enable Zink for OpenGL Games")}
              description={t(
                "CONFIG_ENABLE_ZINK_DESC",
                "Uses the Vulkan-based OpenGL implementation for OpenGL games. May cause crashes or freezes in some games.",
              )}
              checked={config.enable_zink}
              onChange={(value) => onConfigChange(ENABLE_ZINK, value)}
            />
          </PanelSectionRow>

          <PanelSectionRow>
            <ToggleField
              label={t("CONFIG_FORCE_ALSA_AUDIO", "Force ALSA Audio (Restart)")}
              description={t(
                "CONFIG_FORCE_ALSA_AUDIO_DESC",
                "May improve compatibility with modes such as Zink and reduce audio stuttering or sudden loud sounds. Disable to restore normal audio defaults.",
              )}
              bottomSeparator="none"
              checked={config.force_alsa_audio}
              onChange={(value) => onConfigChange(FORCE_ALSA_AUDIO, value)}
            />
          </PanelSectionRow>
        </>
      )}
    </>
  );
}

export function ExternalToolsConfigurationGroup({
  config,
  onConfigChange,
  collapsed,
  onToggle,
}: ConfigurationGroupProps) {
  return (
    <>
      <MakoSectionHeader topMargin="26px">
        {t("CONFIG_EXTERNAL_TOOLS_TITLE", "External Tools")}
      </MakoSectionHeader>

      <PanelSectionRow>
        <div
          style={{
            fontSize: "11px",
            lineHeight: "1.35",
            color: "#b8c5d6",
            marginBottom: "4px",
          }}
        >
          <div>
            {t(
              "CONFIG_EXTERNAL_TOOLS_DESC",
              "Optional and per profile. Enable a tool in Default for games without a saved profile, or save a game profile first to limit it to that title. Gamescope WSI, MangoHud, and vkBasalt are mutually exclusive. Restart the game after changing the selection.",
            )}
          </div>
          <MakoInlineWarning>
            {t(
              "CONFIG_EXTERNAL_TOOLS_WARNING",
              "External tools may affect performance. Test each game carefully.",
            )}
          </MakoInlineWarning>
        </div>
      </PanelSectionRow>

      <CollapseControl
        containerClassName="MAKO_ExternalToolsCollapseButton_Container"
        collapsed={collapsed}
        onToggle={onToggle}
      />

      {!collapsed && (
        <>
          <PanelSectionRow>
            <ToggleField
              label={t("CONFIG_ENABLE_MANGOHUD", "Enable MangoHud (Restart)")}
              description={t(
                "CONFIG_ENABLE_MANGOHUD_DESC",
                "Uses the host-installed MangoHud and your existing MangoHud configuration. See the expert guide for per-game environment overrides.",
              )}
              checked={
                config.external_vulkan_layer === EXTERNAL_VULKAN_LAYER_MANGOHUD
              }
              onChange={(value) =>
                onConfigChange(
                  EXTERNAL_VULKAN_LAYER,
                  value
                    ? EXTERNAL_VULKAN_LAYER_MANGOHUD
                    : EXTERNAL_VULKAN_LAYER_NONE,
                )
              }
            />
          </PanelSectionRow>

          <PanelSectionRow>
            <ToggleField
              label={t(
                "CONFIG_ENABLE_VKBASALT",
                "Enable vkBasalt (Experimental, Restart)",
              )}
              description={t(
                "CONFIG_ENABLE_VKBASALT_DESC",
                "Uses a host-installed vkBasalt layer for this profile. The initial test lane is limited to 64-bit native Vulkan or Proton games launched directly by Steam on SteamOS.",
              )}
              bottomSeparator="none"
              checked={
                config.external_vulkan_layer === EXTERNAL_VULKAN_LAYER_VKBASALT
              }
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
        </>
      )}
    </>
  );
}

export function ManualOverridesConfigurationGroup({
  config,
  onConfigChange,
  collapsed,
  onToggle,
}: ConfigurationGroupProps) {
  return (
    <>
      <MakoSectionHeader topMargin="26px">
        {t("CONFIG_MANUAL_OVERRIDES_TITLE", "Manual Overrides")}
      </MakoSectionHeader>

      <PanelSectionRow>
        <div
          style={{
            fontSize: "11px",
            lineHeight: "1.35",
            color: "#b8c5d6",
            marginBottom: "4px",
          }}
        >
          {t(
            "CONFIG_MANUAL_OVERRIDES_DESC",
            "Optional. MAKO detects these automatically; change them only for custom setups, launchers, or emulators.",
          )}
        </div>
      </PanelSectionRow>

      <CollapseControl
        containerClassName="MAKO_ManualOverridesCollapseButton_Container"
        collapsed={collapsed}
        onToggle={onToggle}
      />

      {!collapsed && (
        <PanelSectionRow>
          <div className="MAKO_ManualOverrideFields">
            <TextField
              label={t("CONFIG_DLL_PATH", "Lossless.dll Path")}
              description={t(
                "CONFIG_DLL_PATH_DESC",
                "Optional full path to Lossless.dll. Leave blank to use MAKO Renderer automatic discovery.",
              )}
              value={config.dll}
              onChange={(event) =>
                onConfigChange(DLL, event.currentTarget.value)
              }
            />

            <TextField
              label={t("CONFIG_GPU", "GPU (Restart)")}
              description={t(
                "CONFIG_GPU_DESC",
                "Optional GPU name, vendor:device ID, or PCI bus ID. Restart the game after changing it.",
              )}
              value={config.gpu}
              onChange={(event) =>
                onConfigChange(GPU, event.currentTarget.value)
              }
            />

            <TextField
              label={t("CONFIG_ACTIVE_IN", "Matched Processes")}
              description={t(
                "CONFIG_ACTIVE_IN_DESC",
                "Executable or process names separated by commas. Running-game capture fills these automatically; edit them only when a launcher or emulator needs an additional process alias.",
              )}
              value={config.active_in}
              onChange={(event) =>
                onConfigChange(ACTIVE_IN, event.currentTarget.value)
              }
            />
          </div>
        </PanelSectionRow>
      )}
    </>
  );
}
