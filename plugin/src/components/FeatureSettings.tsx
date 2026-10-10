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
import { FrameGenerationConfigurationSection } from "./ConfigurationSection";
import { MakoInlineTip, MakoSectionHeader } from "./MakoUi";
import { DEFAULT_PROFILE_NAME } from "../config/configSchema";
import { ModalityTabs, type ModalityId } from "./ModalityTabs";

interface FeatureSettingsProps extends ConfigurationEditorProps {
  runtimeState: RuntimeScalingUiState;
  profileName: string;
  vkBasaltConfigPath: string;
  customShaderEffects?: CustomShaderEffect[];
  onAddShader?: (path: string) => Promise<void>;
  onRefreshShaders?: () => Promise<void>;
  onDeleteShaders?: (shaderIds: string[]) => Promise<void>;
}

export function FeatureSettings({
  config,
  runtimeState,
  profileName,
  vkBasaltConfigPath,
  customShaderEffects,
  onAddShader,
  onRefreshShaders,
  onDeleteShaders,
  onConfigChange,
  onConfigUpdate,
}: FeatureSettingsProps) {
  const [activeModality, setActiveModality] =
    useState<ModalityId>("frame-generation");
  const activeModalityLabel =
    activeModality === "frame-generation"
      ? t("CONTENT_FPS_MULTIPLIER", "Frame Generation")
      : activeModality === "spatial"
        ? t("CONTENT_SCALING", "Spatial Settings")
        : t("CONTENT_SHADERS", "Shaders");

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
        onModalityChange={setActiveModality}
      />

      <div
        key={activeModality}
        id={`mako-modality-panel-${activeModality}`}
        role="tabpanel"
        aria-label={activeModalityLabel}
        data-mako-modality-panel={activeModality}
        style={{
          marginTop: "4px",
          animation: "mako-modality-enter 150ms ease-out",
        }}
      >
        {activeModality === "frame-generation" && (
          <FpsMultiplierControl
            config={config}
            onConfigChange={onConfigChange}
            onConfigUpdate={onConfigUpdate}
          />
        )}

        {activeModality === "spatial" && (
          <ScalingControl
            config={config}
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
            onDeleteShaders={onDeleteShaders}
            onConfigChange={onConfigChange}
          />
        )}
      </div>

      <style>{`
        @keyframes mako-modality-enter {
          from { opacity: 0; transform: translateY(3px); }
          to { opacity: 1; transform: translateY(0); }
        }
      `}</style>

      <PerformanceConfigurationGroup
        config={config}
        onConfigChange={onConfigChange}
        onConfigUpdate={onConfigUpdate}
      />

      <FrameGenerationConfigurationSection
        config={config}
        onConfigChange={onConfigChange}
        onConfigUpdate={onConfigUpdate}
      />
    </>
  );
}
