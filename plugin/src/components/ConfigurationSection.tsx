import {
  PanelSectionRow,
  ToggleField,
  SliderField,
  ButtonItem,
  TextField,
} from "@decky/ui";
import { useState, useEffect } from "react";
import { RiArrowDownSFill, RiArrowUpSFill } from "react-icons/ri";
import { ConfigurationData } from "../config/configSchema";
import {
  ACTIVE_IN,
  ALLOW_FP16,
  DISABLE_MAKO,
  DLL,
  FLOW_SCALE,
  GPU,
  BASE_FPS_CAP,
  DISABLE_STEAMDECK_MODE,
  ENABLE_ZINK,
  EXTERNAL_VULKAN_LAYER,
  FORCE_ALSA_AUDIO,
} from "../config/generatedConfigSchema";
import t from "../i18n/i18n";
import { makoDangerTextColor, MakoSectionHeader } from "./MakoUi";

interface ConfigurationSectionProps {
  config: ConfigurationData;
  onConfigChange: (
    fieldName: keyof ConfigurationData,
    value: boolean | number | string,
  ) => Promise<void>;
}

const WORKAROUNDS_COLLAPSED_KEY = "mako-workarounds-collapsed";
const CONFIG_COLLAPSED_KEY = "mako-config-collapsed";
const EXTERNAL_TOOLS_COLLAPSED_KEY = "mako-external-tools-collapsed";
const MANUAL_OVERRIDES_COLLAPSED_KEY = "mako-manual-overrides-collapsed";

export function ConfigurationSection({
  config,
  onConfigChange,
}: ConfigurationSectionProps) {
  // Initialize with localStorage value, fallback to true if not found
  const [configCollapsed, setConfigCollapsed] = useState(() => {
    try {
      const saved = localStorage.getItem(CONFIG_COLLAPSED_KEY);
      return saved !== null ? JSON.parse(saved) : false;
    } catch {
      return false;
    }
  });

  const [workaroundsCollapsed, setWorkaroundsCollapsed] = useState(() => {
    try {
      const saved = localStorage.getItem(WORKAROUNDS_COLLAPSED_KEY);
      return saved !== null ? JSON.parse(saved) : true;
    } catch {
      return true;
    }
  });

  const [manualOverridesCollapsed, setManualOverridesCollapsed] = useState(
    () => {
      try {
        const saved = localStorage.getItem(MANUAL_OVERRIDES_COLLAPSED_KEY);
        return saved !== null ? JSON.parse(saved) : true;
      } catch {
        return true;
      }
    },
  );

  const [externalToolsCollapsed, setExternalToolsCollapsed] = useState(() => {
    try {
      const saved = localStorage.getItem(EXTERNAL_TOOLS_COLLAPSED_KEY);
      return saved !== null ? JSON.parse(saved) : true;
    } catch {
      return true;
    }
  });

  // Persist workarounds collapse state to localStorage
  useEffect(() => {
    try {
      localStorage.setItem(
        CONFIG_COLLAPSED_KEY,
        JSON.stringify(configCollapsed),
      );
    } catch (error) {
      console.warn("Failed to save config collapse state:", error);
    }
  }, [configCollapsed]);

  useEffect(() => {
    try {
      localStorage.setItem(
        WORKAROUNDS_COLLAPSED_KEY,
        JSON.stringify(workaroundsCollapsed),
      );
    } catch (error) {
      console.warn("Failed to save workarounds collapse state:", error);
    }
  }, [workaroundsCollapsed]);

  useEffect(() => {
    try {
      localStorage.setItem(
        MANUAL_OVERRIDES_COLLAPSED_KEY,
        JSON.stringify(manualOverridesCollapsed),
      );
    } catch (error) {
      console.warn("Failed to save manual overrides collapse state:", error);
    }
  }, [manualOverridesCollapsed]);

  useEffect(() => {
    try {
      localStorage.setItem(
        EXTERNAL_TOOLS_COLLAPSED_KEY,
        JSON.stringify(externalToolsCollapsed),
      );
    } catch (error) {
      console.warn("Failed to save external tools collapse state:", error);
    }
  }, [externalToolsCollapsed]);

  return (
    <>
      <style>
        {`
        .MAKO_ConfigCollapseButton_Container > div > div > div > button,
        .MAKO_ConfigCollapseButton_Container > div > div > div > div > button,
        .MAKO_WorkaroundsCollapseButton_Container > div > div > div > button,
        .MAKO_ExternalToolsCollapseButton_Container > div > div > div > button,
        .MAKO_ManualOverridesCollapseButton_Container > div > div > div > button {
          height: 10px !important;
        }
        .MAKO_WorkaroundsCollapseButton_Container > div > div > div > div > button,
        .MAKO_ExternalToolsCollapseButton_Container > div > div > div > div > button,
        .MAKO_ManualOverridesCollapseButton_Container > div > div > div > div > button {
          height: 10px !important;
        }
        .MAKO_ManualOverrideFields {
          display: flex;
          flex-direction: column;
          gap: 8px;
          width: 100%;
          min-width: 0;
          margin: 10px 0 0;
        }
        .MAKO_ManualOverrideFields > * {
          margin-bottom: 0 !important;
        }
        .MAKO_ManualOverrideFields > * + * {
          margin-top: 0 !important;
        }
        .MAKO_ManualOverrideFields > *:nth-child(2),
        .MAKO_ManualOverrideFields > *:nth-child(3) {
          margin-top: 4px !important;
        }
        `}
      </style>

      <MakoSectionHeader topMargin="37px">
        {t("CONFIG_SECTION_TITLE", "Advanced Rendering Settings")}
      </MakoSectionHeader>

      <PanelSectionRow>
        <div
          className="MAKO_ConfigCollapseButton_Container"
          style={{ marginTop: "2px", marginBottom: "4px" }}
        >
          <ButtonItem
            layout="below"
            bottomSeparator="none"
            onClick={() => setConfigCollapsed(!configCollapsed)}
          >
            {configCollapsed ? (
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

      {!configCollapsed && (
        <>
          <PanelSectionRow>
            <SliderField
              label={`${t("CONFIG_FLOW_SCALE", "Flow Scale (Restart)")} (${Math.round(config.flow_scale * 100)}%)`}
              description={t(
                "CONFIG_FLOW_SCALE_DESC",
                "Controls internal motion-estimation resolution. Lower values reduce GPU work; higher values favour quality. Restart the game after changing it.",
              )}
              value={config.flow_scale}
              min={0.25}
              max={1.0}
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
              min={0}
              max={60}
              step={1}
              disabled={
                config.adaptive && (config.adaptive_auto_base_fps_cap ?? false)
              }
              onChange={(value) => onConfigChange(BASE_FPS_CAP, value)}
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
              bottomSeparator="none"
              checked={config.disable_mako}
              onChange={(value) => onConfigChange(DISABLE_MAKO, value)}
            />
          </PanelSectionRow>
        </>
      )}

      <MakoSectionHeader topMargin="26px">
        {t("CONFIG_WORKAROUNDS_TITLE", "Compatibility Settings")}
      </MakoSectionHeader>

      <PanelSectionRow>
        <div
          className="MAKO_WorkaroundsCollapseButton_Container"
          style={{ marginTop: "2px", marginBottom: "4px" }}
        >
          <ButtonItem
            layout="below"
            bottomSeparator="none"
            onClick={() => setWorkaroundsCollapsed(!workaroundsCollapsed)}
          >
            {workaroundsCollapsed ? (
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

      {!workaroundsCollapsed && (
        <>
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
                  <div
                    style={{
                      marginTop: "6px",
                      color: makoDangerTextColor,
                      fontWeight: "500",
                    }}
                  >
                    {t(
                      "CONFIG_GAMESCOPE_WSI_COMPATIBILITY_WARNING",
                      "Keep it off if not needed. It may reduce performance or interfere with frame generation.",
                    )}
                  </div>
                </>
              }
              checked={config.external_vulkan_layer === "gamescope-wsi"}
              onChange={(value) =>
                onConfigChange(
                  EXTERNAL_VULKAN_LAYER,
                  value ? "gamescope-wsi" : "",
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
          <div
            style={{
              marginTop: "6px",
              color: makoDangerTextColor,
              fontWeight: "500",
            }}
          >
            {t(
              "CONFIG_EXTERNAL_TOOLS_WARNING",
              "External tools may affect performance. Test each game carefully.",
            )}
          </div>
        </div>
      </PanelSectionRow>

      <PanelSectionRow>
        <div
          className="MAKO_ExternalToolsCollapseButton_Container"
          style={{ marginTop: "2px", marginBottom: "4px" }}
        >
          <ButtonItem
            layout="below"
            bottomSeparator="none"
            onClick={() => setExternalToolsCollapsed(!externalToolsCollapsed)}
          >
            {externalToolsCollapsed ? (
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

      {!externalToolsCollapsed && (
        <>
          <PanelSectionRow>
            <ToggleField
              label={t("CONFIG_ENABLE_MANGOHUD", "Enable MangoHud (Restart)")}
              description={t(
                "CONFIG_ENABLE_MANGOHUD_DESC",
                "Uses the host-installed MangoHud and your existing MangoHud configuration. See the expert guide for per-game environment overrides.",
              )}
              checked={config.external_vulkan_layer === "mangohud"}
              onChange={(value) =>
                onConfigChange(EXTERNAL_VULKAN_LAYER, value ? "mangohud" : "")
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
              checked={config.external_vulkan_layer === "vkbasalt"}
              onChange={(value) =>
                onConfigChange(EXTERNAL_VULKAN_LAYER, value ? "vkbasalt" : "")
              }
            />
          </PanelSectionRow>
        </>
      )}

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

      <PanelSectionRow>
        <div
          className="MAKO_ManualOverridesCollapseButton_Container"
          style={{ marginTop: "2px", marginBottom: "4px" }}
        >
          <ButtonItem
            layout="below"
            bottomSeparator="none"
            onClick={() =>
              setManualOverridesCollapsed(!manualOverridesCollapsed)
            }
          >
            {manualOverridesCollapsed ? (
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

      {!manualOverridesCollapsed && (
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
