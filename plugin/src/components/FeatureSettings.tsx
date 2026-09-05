import type { ConfigurationEditorProps } from "./settings/types";
import type { RuntimeScalingUiState } from "../utils/runtimeScalingUtils";
import t from "../i18n/i18n";
import { FpsMultiplierControl } from "./FpsMultiplierControl";
import { ScalingControl } from "./ScalingControl";
import { PerformanceConfigurationGroup } from "./settings/PerformanceConfigurationGroup";
import { FrameGenerationConfigurationSection } from "./ConfigurationSection";
import { MakoSectionHeader } from "./MakoUi";

interface FeatureSettingsProps extends ConfigurationEditorProps {
  disabled?: boolean;
  runtimeState: RuntimeScalingUiState;
  scalingModelCompatible?: boolean | null;
}

export function FeatureSettings({
  config,
  disabled = false,
  runtimeState,
  scalingModelCompatible = null,
  onConfigChange,
  onConfigUpdate,
}: FeatureSettingsProps) {
  return (
    <>
      <MakoSectionHeader>
        {t("CONTENT_FPS_MULTIPLIER", "Frame Generation")}
      </MakoSectionHeader>
      <FpsMultiplierControl
        config={config}
        onConfigChange={onConfigChange}
        onConfigUpdate={onConfigUpdate}
      />
      <MakoSectionHeader>
        {t("CONTENT_SCALING", "Spatial Settings")}
      </MakoSectionHeader>
      <ScalingControl
        config={config}
        disabled={disabled}
        runtimeActivationSupported={runtimeState.scalingActivationSupported}
        runtimeInactiveReason={runtimeState.inactiveReason}
        runtimeFactorCeiling={runtimeState.nonSupersamplingFactorCeiling}
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
