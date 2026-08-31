import type { ConfigurationData } from "../config/configSchema";
import type { RuntimeScalingUiState } from "../utils/runtimeScalingUtils";
import t from "../i18n/i18n";
import { FpsMultiplierControl } from "./FpsMultiplierControl";
import { ScalingControl } from "./ScalingControl";
import { PerformanceConfigurationGroup } from "./ConfigurationSectionGroups";
import { FrameGenerationConfigurationSection } from "./ConfigurationSection";
import { MakoSectionHeader } from "./MakoUi";

interface FeatureSettingsProps {
  config: ConfigurationData;
  disabled?: boolean;
  runtimeState: RuntimeScalingUiState;
  onConfigChange: (
    fieldName: keyof ConfigurationData,
    value: boolean | number | string,
  ) => Promise<void>;
  onConfigUpdate: (changes: Partial<ConfigurationData>) => Promise<void>;
}

export function FeatureSettings({
  config,
  disabled = false,
  runtimeState,
  onConfigChange,
  onConfigUpdate,
}: FeatureSettingsProps) {
  return (
    <>
      <MakoSectionHeader topMargin="18px">
        {t("CONTENT_FPS_MULTIPLIER", "Frame Generation")}
      </MakoSectionHeader>
      <FpsMultiplierControl
        config={config}
        onConfigChange={onConfigChange}
        onConfigUpdate={onConfigUpdate}
      />
      <MakoSectionHeader topMargin="26px">
        {t("CONTENT_SCALING", "Spatial Settings")}
      </MakoSectionHeader>
      <ScalingControl
        config={config}
        disabled={disabled}
        runtimeInactiveReason={runtimeState.inactiveReason}
        runtimeFactorCeiling={runtimeState.nonSupersamplingFactorCeiling}
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
