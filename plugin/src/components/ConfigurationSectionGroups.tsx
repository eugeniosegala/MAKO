import {
  ButtonItem,
  Dropdown,
  Field,
  PanelSectionRow,
  SliderField,
  TextField,
  ToggleField,
} from "@decky/ui";
import { RiArrowDownSFill, RiArrowUpSFill } from "react-icons/ri";
import { MdBolt } from "react-icons/md";
import {
  ACTIVE_IN,
  ADAPTIVE_MINIMUM_BASE_FPS,
  ALLOW_FP16,
  BASE_FPS_CAP_MIN,
  BASE_FPS_CAP_UI_MAX,
  DISABLE_MAKO,
  DISABLE_STEAMDECK_MODE,
  DLL,
  DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS,
  DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_VALUES,
  ENABLE_ZINK,
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
  GAMESCOPE_WSI_COMPATIBILITY,
  GPU,
  ULTRA_PERFORMANCE_FLOW_SCALE,
  type ConfigurationData,
} from "../config/configSchema";
import {
  baseFpsCapChanges,
  dynamicCadenceRecoveryChanges,
} from "../config/fractionalAdaptivePreset";
import { ultraPerformanceChanges } from "../config/ultraPerformancePreset";
import t from "../i18n/i18n";
import {
  MakoExperimentalSettingLabel,
  MakoInlineTip,
  MakoRestartLabel,
  MakoSectionHeader,
  MakoSettingRelationship,
} from "./MakoUi";

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

interface PerformanceConfigurationGroupProps {
  config: ConfigurationData;
  onConfigChange: SaveConfigurationField;
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

export function PerformanceConfigurationGroup({
  config,
  onConfigChange,
  onConfigUpdate,
}: PerformanceConfigurationGroupProps) {
  return (
    <>
      <MakoSectionHeader>
        {t("CONTENT_PERFORMANCE_SETTINGS", "Performance Settings")}
      </MakoSectionHeader>

      <PanelSectionRow>
        <ToggleField
          label={
            <span
              style={{
                display: "inline-flex",
                alignItems: "center",
                gap: "5px",
              }}
            >
              <MdBolt aria-hidden="true" size={16} color="#f4a259" />
              <MakoRestartLabel
                label={t(
                  "CONFIG_ULTRA_PERFORMANCE",
                  "Ultra Performance (Restart)",
                )}
              />
            </span>
          }
          description={
            <>
              <div>
                {t(
                  "CONFIG_ULTRA_PERFORMANCE_DESC",
                  "Reduces MAKO's GPU workload on low-power devices. Uses 75% Flow Scale, the Lighter FG Model, FP16 when supported, and LS1 Performance when Scaling is enabled. Trades image quality for performance across the active MAKO features.",
                )}
              </div>
              <MakoInlineTip tone="info">
                {t(
                  "CONFIG_ULTRA_PERFORMANCE_WARNING",
                  "Turning Ultra Performance on or off requires a game restart. Other compatible profile controls remain available after startup.",
                )}
              </MakoInlineTip>
            </>
          }
          checked={config.ultra_performance}
          onChange={(value) => onConfigUpdate(ultraPerformanceChanges(value))}
        />
      </PanelSectionRow>

      <PanelSectionRow>
        <SliderField
          label={`${t("CONFIG_FLOW_SCALE", "Flow Scale")} (${Math.round((config.ultra_performance ? ULTRA_PERFORMANCE_FLOW_SCALE : config.flow_scale) * 100)}%)`}
          description={t(
            "CONFIG_FLOW_SCALE_DESC",
            "Controls the internal motion-estimation resolution used only for Frame Generation. Lower values reduce GPU work; higher values favour quality.",
          )}
          value={
            config.ultra_performance
              ? ULTRA_PERFORMANCE_FLOW_SCALE
              : config.flow_scale
          }
          min={FLOW_SCALE_MIN}
          max={FLOW_SCALE_MAX}
          step={0.01}
          disabled={config.ultra_performance}
          onChange={(value) => onConfigChange(FLOW_SCALE, value)}
        />
      </PanelSectionRow>

      <PanelSectionRow>
        <ToggleField
          label={
            <MakoRestartLabel
              label={t("CONFIG_ALLOW_FP16", "Allow FP16 (Restart)")}
            />
          }
          description={t(
            "CONFIG_ALLOW_FP16_DESC",
            "Global renderer setting: applies to all profiles and cannot be changed per game. Improves performance on AMD; disable for older NVIDIA GPUs. Restart the game after changing it.",
          )}
          checked={config.ultra_performance || config.allow_fp16}
          disabled={config.ultra_performance}
          onChange={(value) => onConfigChange(ALLOW_FP16, value)}
          bottomSeparator="none"
        />
      </PanelSectionRow>
    </>
  );
}

export function AdvancedRenderingConfigurationGroup({
  config,
  onConfigChange,
  onConfigUpdate,
  collapsed,
  onToggle,
}: ConfigurationUpdateGroupProps) {
  const steadyBaseFpsCap = Math.max(
    ADAPTIVE_MINIMUM_BASE_FPS,
    config.target_fps / 2,
  );
  const steadyBaseFpsCapLabel = Number.isInteger(steadyBaseFpsCap)
    ? steadyBaseFpsCap.toFixed(0)
    : steadyBaseFpsCap.toFixed(1);

  return (
    <>
      <MakoSectionHeader>
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
              label={`${t("CONFIG_BASE_FPS_CAP", "Base FPS Cap")}${config.base_fps_cap > 0 ? ` (${config.base_fps_cap} FPS)` : ` (${t("CONFIG_BASE_FPS_CAP_OFF", "Off")})`}`}
              description={
                <>
                  <div>
                    {t(
                      "CONFIG_BASE_FPS_CAP_DESC",
                      "Caps real application frames before frame generation. Works with DirectX, OpenGL through Zink, and Vulkan.",
                    )}
                  </div>
                  {config.adaptive && config.adaptive_auto_base_fps_cap ? (
                    <MakoSettingRelationship>
                      {t(
                        "CONFIG_BASE_FPS_CAP_STEADY_RELATION",
                        "Controlled by Steady Base Cap ({fps} FPS). Your manual value remains saved.",
                        { fps: steadyBaseFpsCapLabel },
                      )}
                    </MakoSettingRelationship>
                  ) : config.dynamic_cadence_recovery ? (
                    <MakoSettingRelationship>
                      {t(
                        "CONFIG_BASE_FPS_CAP_RECOVERY_RELATION",
                        "Changing this cap turns Dynamic Cadence Recovery off.",
                      )}
                    </MakoSettingRelationship>
                  ) : null}
                </>
              }
              value={config.base_fps_cap}
              min={BASE_FPS_CAP_MIN}
              max={BASE_FPS_CAP_UI_MAX}
              step={1}
              disabled={
                !config.frame_generation_enabled ||
                (config.adaptive && config.adaptive_auto_base_fps_cap)
              }
              onChange={(value) => onConfigUpdate(baseFpsCapChanges(value))}
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
                "Troubleshooting only. Stops MAKO Renderer loading the next time the game starts. Use Frame Generation above to switch synthesis on or off.",
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
                  "Choose the highest refresh rate where frame generation should remain paused.",
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
  const cadenceProbeInterval = config.dynamic_cadence_probe_interval_seconds;
  const cadenceProbeIntervalValues =
    DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_VALUES.includes(
      cadenceProbeInterval as (typeof DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_VALUES)[number],
    )
      ? [...DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_VALUES]
      : [
          ...DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_VALUES,
          cadenceProbeInterval,
        ].sort((left, right) => left - right);
  const cadenceProbeIntervalOptions = cadenceProbeIntervalValues.map(
    (value) => ({ data: value, label: `${value}s` }),
  );

  return (
    <>
      <MakoSectionHeader>
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
              label={t("CONFIG_DISABLE_HDR_EXPOSURE", "Disable HDR")}
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
              label={t("DYNAMIC_CADENCE_RECOVERY", "Dynamic Cadence Recovery")}
              description={
                <>
                  <div>
                    {t(
                      "DYNAMIC_CADENCE_RECOVERY_DESC",
                      "Helps games and emulators that switch native rates, such as 30 FPS gameplay and 60 FPS menus. It periodically checks for a rate change and recovers the correct cadence, but each check can briefly affect pacing. Enable it only for affected games.",
                    )}
                  </div>
                  <MakoSettingRelationship>
                    {t(
                      "DYNAMIC_CADENCE_RECOVERY_RELATION",
                      "Turning this on disables Steady Base Cap and Base FPS Cap. Changing either cap later turns Recovery off.",
                    )}
                  </MakoSettingRelationship>
                </>
              }
              checked={config.dynamic_cadence_recovery}
              onChange={(value) =>
                onConfigUpdate(dynamicCadenceRecoveryChanges(value))
              }
            />
          </PanelSectionRow>

          {config.dynamic_cadence_recovery && (
            <PanelSectionRow>
              <Field
                label={t(
                  "DYNAMIC_CADENCE_PROBE_INTERVAL",
                  "Cadence Probe Interval",
                )}
                description={
                  <span style={{ display: "block", paddingBottom: "6px" }}>
                    {t(
                      "DYNAMIC_CADENCE_PROBE_INTERVAL_DESC",
                      "How often Recovery tests the native frame rate. 0.1 seconds is aggressive and may cause frequent brief pacing hitches; 2 seconds is the default, while 3 seconds checks least often. Test per game.",
                    )}
                  </span>
                }
                childrenLayout="below"
                childrenContainerWidth="max"
              >
                <Dropdown
                  rgOptions={cadenceProbeIntervalOptions}
                  selectedOption={cadenceProbeInterval}
                  onChange={(option) =>
                    onConfigChange(
                      DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS,
                      Number(option.data),
                    )
                  }
                />
              </Field>
            </PanelSectionRow>
          )}

          <PanelSectionRow>
            <ToggleField
              label={
                <MakoRestartLabel
                  label={t(
                    "CONFIG_GAMESCOPE_WSI_COMPATIBILITY",
                    "Gamescope WSI (Restart)",
                  )}
                />
              }
              description={
                <>
                  <div>
                    {t(
                      "CONFIG_GAMESCOPE_WSI_COMPATIBILITY_DESC",
                      "May reduce coloured or pixelated motion artifacts in some games by using Gamescope's presentation path. Scaling enables it automatically. For FG-only profiles, enable it only for affected games.",
                    )}
                  </div>
                  {!config.scaling_enabled && (
                    <MakoInlineTip tone="warning">
                      {t(
                        "CONFIG_GAMESCOPE_WSI_COMPATIBILITY_WARNING",
                        "This compatibility path is limited to supported 64-bit host launches. Leave it off when the game does not need it, as it may impact performance.",
                      )}
                    </MakoInlineTip>
                  )}
                </>
              }
              checked={
                config.scaling_enabled || config.gamescope_wsi_compatibility
              }
              disabled={config.scaling_enabled}
              onChange={(value) =>
                onConfigChange(GAMESCOPE_WSI_COMPATIBILITY, value)
              }
            />
          </PanelSectionRow>

          <PanelSectionRow>
            <ToggleField
              label={
                <MakoRestartLabel
                  label={t(
                    "CONFIG_DISABLE_STEAMDECK_MODE",
                    "Disable Steam Deck Mode (Restart)",
                  )}
                />
              }
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
              label={
                <MakoRestartLabel
                  label={t(
                    "CONFIG_ENABLE_ZINK",
                    "Enable Zink for OpenGL Games (Restart)",
                  )}
                />
              }
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
              label={
                <MakoRestartLabel
                  label={t(
                    "CONFIG_FORCE_ALSA_AUDIO",
                    "Force ALSA Audio (Restart)",
                  )}
                />
              }
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
      <MakoSectionHeader>
        {t("CONFIG_EXTERNAL_TOOLS_TITLE", "External Tools")}
      </MakoSectionHeader>

      <CollapseControl
        containerClassName="MAKO_ExternalToolsCollapseButton_Container"
        collapsed={collapsed}
        onToggle={onToggle}
      />

      {!collapsed && (
        <>
          <PanelSectionRow>
            <ToggleField
              label={
                <MakoRestartLabel
                  label={t(
                    "CONFIG_ENABLE_MANGOHUD",
                    "Enable MangoHud (Restart)",
                  )}
                />
              }
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
              label={
                <MakoExperimentalSettingLabel
                  label={t(
                    "CONFIG_ENABLE_VKBASALT",
                    "Enable vkBasalt (Restart)",
                  )}
                  badgeLabel={t("EXPERIMENTAL_LABEL", "Experimental")}
                />
              }
              description={t(
                "CONFIG_ENABLE_VKBASALT_DESC",
                "Keep it off unless you are testing vkBasalt with this game. Uses a host-installed vkBasalt layer for this profile. The initial test lane is limited to 64-bit native Vulkan or Proton games launched directly by Steam on SteamOS.",
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
      <MakoSectionHeader>
        {t("CONFIG_MANUAL_OVERRIDES_TITLE", "Manual Overrides")}
      </MakoSectionHeader>

      <CollapseControl
        containerClassName="MAKO_ManualOverridesCollapseButton_Container"
        collapsed={collapsed}
        onToggle={onToggle}
      />

      {!collapsed && (
        <PanelSectionRow>
          <div className="MAKO_ManualOverrideFields">
            <TextField
              label={
                <MakoRestartLabel
                  label={t("CONFIG_DLL_PATH", "Lossless.dll Path (Restart)")}
                />
              }
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
              label={
                <MakoRestartLabel label={t("CONFIG_GPU", "GPU (Restart)")} />
              }
              description={
                <span className="MAKO_GpuDescription">
                  {t(
                    "CONFIG_GPU_DESC",
                    "Optional GPU name, vendor:device ID, or PCI bus ID. Restart the game after changing it.",
                  )}
                </span>
              }
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
