import { useState } from "react";
import type { ConfigurationEditorProps } from "./settings/types";
import type { RuntimeScalingUiState } from "../utils/runtimeScalingUtils";
import t from "../i18n/i18n";
import { FpsMultiplierControl } from "./FpsMultiplierControl";
import { ScalingControl } from "./ScalingControl";
import { PerformanceConfigurationGroup } from "./settings/PerformanceConfigurationGroup";
import { ShadersConfigurationGroup } from "./settings/ShadersConfigurationGroup";
import { FrameGenerationConfigurationSection } from "./ConfigurationSection";
import { MakoSectionHeader } from "./MakoUi";
import { DEFAULT_PROFILE_NAME } from "../config/configSchema";
import { ModalityTabs, type ModalityId } from "./ModalityTabs";

interface FeatureSettingsProps extends ConfigurationEditorProps {
  disabled?: boolean;
  runtimeState: RuntimeScalingUiState;
  scalingModelCompatible?: boolean | null;
  profileName: string;
  vkBasaltConfigPath: string;
}

export function FeatureSettings({
  config,
  disabled = false,
  runtimeState,
  scalingModelCompatible = null,
  profileName,
  vkBasaltConfigPath,
  onConfigChange,
  onConfigUpdate,
}: FeatureSettingsProps) {
  const [activeModality, setActiveModality] = useState<ModalityId>(
    "frame-generation",
  );
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
            disabled={disabled}
            runtimeActivationSupported={
              runtimeState.scalingActivationSupported
            }
            runtimeInactiveReason={runtimeState.inactiveReason}
            runtimeFactorCeiling={
              runtimeState.nonSupersamplingFactorCeiling
            }
            modelCompatible={scalingModelCompatible}
            runtimeRequestedMethod={runtimeState.requestedMethod}
            runtimeActiveMethod={
              runtimeState.scalingActive ? runtimeState.activeMethod : null
            }
            runtimeMakoFallback={
              runtimeState.scalingActive &&
              runtimeState.activeMethod === "mako" &&
              Boolean(runtimeState.fallbackReason)
            }
            onConfigChange={onConfigChange}
          />
        )}

        {activeModality === "shaders" && (
          <ShadersConfigurationGroup
            config={config}
            isDefaultProfile={profileName === DEFAULT_PROFILE_NAME}
            vkBasaltConfigPath={vkBasaltConfigPath}
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
