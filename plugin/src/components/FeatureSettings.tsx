import type { CustomShaderEffect } from "../api/makoApi";
import { PanelSectionRow } from "@decky/ui";
import { useState } from "react";
import type { ConfigurationEditorProps } from "./settings/types";
import type { RuntimeScalingUiState } from "../utils/runtimeScalingUtils";
import t from "../i18n/i18n";
import { FpsMultiplierControl } from "./FpsMultiplierControl";
import { ScalingControl } from "./ScalingControl";
import { PerformanceConfigurationGroup } from "./settings/PerformanceConfigurationGroup";
import { ShadersConfigurationGroup } from "./settings/ShadersConfigurationGroup";
import { ConfigurationSection } from "./ConfigurationSection";
import { MakoInlineTip, MakoSectionHeader } from "./MakoUi";
import { DEFAULT_PROFILE_NAME } from "../config/configSchema";
import { ModalityTabs, type ModalityId } from "./ModalityTabs";

const ACTIVE_MODALITY_STORAGE_KEY = "mako-active-modality";

interface FeatureSettingsProps extends ConfigurationEditorProps {
  losslessFrameGenerationAvailable?: boolean;
  losslessScalingAvailable?: boolean;
  runtimeState: RuntimeScalingUiState;
  profileName: string;
  vkBasaltConfigPath: string;
  customShaderEffects?: CustomShaderEffect[];
  onAddShader?: (path: string) => Promise<void>;
  onRefreshShaders?: () => Promise<void>;
  onCustomizeShaders?: () => Promise<void>;
  onDeleteShaders?: (shaderIds: string[]) => Promise<void>;
}

export function FeatureSettings({
  config,
  losslessFrameGenerationAvailable,
  losslessScalingAvailable,
  runtimeState,
  profileName,
  vkBasaltConfigPath,
  customShaderEffects,
  onAddShader,
  onRefreshShaders,
  onCustomizeShaders,
  onDeleteShaders,
  onConfigChange,
  onConfigUpdate,
}: FeatureSettingsProps) {
  const modalityLabels: Record<ModalityId, string> = {
    performance: t("CONTENT_PERFORMANCE_SETTINGS", "Performance Settings"),
    "frame-generation": t("CONTENT_FPS_MULTIPLIER", "Frame Generation"),
    spatial: t("CONTENT_SCALING", "Spatial Settings"),
    shaders: t("CONTENT_SHADERS", "Shaders"),
    advanced: t("CONFIG_SECTION_TITLE", "Advanced Rendering Settings"),
    compatibility: t("CONFIG_WORKAROUNDS_TITLE", "Compatibility Settings"),
  };
  const [activeModality, setActiveModality] = useState<ModalityId>(() => {
    try {
      const saved = localStorage.getItem(ACTIVE_MODALITY_STORAGE_KEY);
      if (
        saved &&
        Object.prototype.hasOwnProperty.call(modalityLabels, saved)
      ) {
        return saved as ModalityId;
      }
    } catch {
      // Unavailable storage must not prevent opening the settings.
    }
    return "frame-generation";
  });

  const selectModality = (modality: ModalityId) => {
    setActiveModality(modality);
    try {
      localStorage.setItem(ACTIVE_MODALITY_STORAGE_KEY, modality);
    } catch (error) {
      console.warn("Failed to save active settings tab:", error);
    }
  };

  return (
    <>
      <MakoSectionHeader>
        {t("CONTENT_IMAGE_PROCESSING", "Image Processing")}
      </MakoSectionHeader>
      <PanelSectionRow>
        <div data-mako-image-processing-tip="true" style={{ marginTop: "3px" }}>
          <MakoInlineTip tone="info">
            {t(
              "IMAGE_PROCESSING_PERFORMANCE_INFO",
              "Combining Frame Generation, Scaling, and Shaders may cost performance. Disable unused features; changing display mode can change input resolution and GPU cost.",
            )}
          </MakoInlineTip>
        </div>
      </PanelSectionRow>
      <ModalityTabs
        activeModality={activeModality}
        onModalityChange={selectModality}
      />

      <div
        key={activeModality}
        id={`mako-modality-panel-${activeModality}`}
        role="tabpanel"
        aria-label={modalityLabels[activeModality]}
        data-mako-modality-panel={activeModality}
        style={{
          marginTop: "4px",
          animation: "mako-modality-enter 150ms ease-out",
        }}
      >
        {activeModality === "frame-generation" && (
          <FpsMultiplierControl
            config={config}
            losslessAvailable={losslessFrameGenerationAvailable}
            onConfigChange={onConfigChange}
            onConfigUpdate={onConfigUpdate}
          />
        )}

        {activeModality === "spatial" && (
          <ScalingControl
            config={config}
            losslessAvailable={losslessScalingAvailable}
            runtimeActivationSupported={runtimeState.scalingActivationSupported}
            runtimeInactiveReason={runtimeState.inactiveReason}
            runtimeFactorCeiling={runtimeState.nonSupersamplingFactorCeiling}
            onConfigChange={onConfigChange}
          />
        )}

        {activeModality === "shaders" && (
          <ShadersConfigurationGroup
            key={profileName}
            config={config}
            isDefaultProfile={profileName === DEFAULT_PROFILE_NAME}
            profileName={profileName}
            vkBasaltConfigPath={vkBasaltConfigPath}
            customShaderEffects={customShaderEffects}
            onAddShader={onAddShader}
            onRefreshShaders={onRefreshShaders}
            onCustomizeShaders={onCustomizeShaders}
            onDeleteShaders={onDeleteShaders}
            onConfigChange={onConfigChange}
          />
        )}

        {activeModality === "performance" && (
          <PerformanceConfigurationGroup
            config={config}
            onConfigChange={onConfigChange}
            onConfigUpdate={onConfigUpdate}
          />
        )}

        {(activeModality === "advanced" ||
          activeModality === "compatibility") && (
          <ConfigurationSection
            groups={[activeModality]}
            collapsible={false}
            config={config}
            onConfigChange={onConfigChange}
            onConfigUpdate={onConfigUpdate}
          />
        )}
      </div>

      <style>{`
        @keyframes mako-modality-enter {
          from { opacity: 0; transform: translateY(3px); }
          to { opacity: 1; transform: translateY(0); }
        }
      `}</style>
    </>
  );
}
