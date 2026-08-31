import type { ConfigurationData } from "../config/configSchema";
import { usePersistentCollapseState } from "../hooks/usePersistentCollapseState";
import {
  AdvancedRenderingConfigurationGroup,
  CompatibilityConfigurationGroup,
  ExternalToolsConfigurationGroup,
  ManualOverridesConfigurationGroup,
} from "./ConfigurationSectionGroups";

interface ConfigurationSectionProps {
  config: ConfigurationData;
  onConfigChange: (
    fieldName: keyof ConfigurationData,
    value: boolean | number | string,
  ) => Promise<void>;
  onConfigUpdate: (changes: Partial<ConfigurationData>) => Promise<void>;
  includeAdvancedRendering?: boolean;
}

interface FrameGenerationConfigurationSectionProps
  extends ConfigurationSectionProps {
  initiallyCollapsed?: boolean;
}

const WORKAROUNDS_COLLAPSED_KEY = "mako-workarounds-collapsed";
const CONFIG_COLLAPSED_KEY = "mako-config-collapsed";
const EXTERNAL_TOOLS_COLLAPSED_KEY = "mako-external-tools-collapsed";
const MANUAL_OVERRIDES_COLLAPSED_KEY = "mako-manual-overrides-collapsed";

const collapseControlStyles = `
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
`;

export function FrameGenerationConfigurationSection({
  config,
  onConfigChange,
  onConfigUpdate,
  initiallyCollapsed = true,
}: FrameGenerationConfigurationSectionProps) {
  const [configCollapsed, setConfigCollapsed] = usePersistentCollapseState(
    CONFIG_COLLAPSED_KEY,
    initiallyCollapsed,
    "frame generation advanced settings",
  );

  return (
    <>
      <style>{collapseControlStyles}</style>
      <AdvancedRenderingConfigurationGroup
        config={config}
        onConfigChange={onConfigChange}
        onConfigUpdate={onConfigUpdate}
        collapsed={configCollapsed}
        onToggle={() => setConfigCollapsed(!configCollapsed)}
      />
    </>
  );
}

export function ConfigurationSection({
  config,
  onConfigChange,
  onConfigUpdate,
  includeAdvancedRendering = true,
}: ConfigurationSectionProps) {
  const [workaroundsCollapsed, setWorkaroundsCollapsed] =
    usePersistentCollapseState(WORKAROUNDS_COLLAPSED_KEY, true, "workarounds");
  const [manualOverridesCollapsed, setManualOverridesCollapsed] =
    usePersistentCollapseState(
      MANUAL_OVERRIDES_COLLAPSED_KEY,
      true,
      "manual overrides",
    );
  const [externalToolsCollapsed, setExternalToolsCollapsed] =
    usePersistentCollapseState(
      EXTERNAL_TOOLS_COLLAPSED_KEY,
      true,
      "external tools",
    );

  return (
    <>
      <style>{`${collapseControlStyles}
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
      `}</style>

      {includeAdvancedRendering && (
        <FrameGenerationConfigurationSection
          config={config}
          onConfigChange={onConfigChange}
          onConfigUpdate={onConfigUpdate}
          initiallyCollapsed={false}
        />
      )}

      <CompatibilityConfigurationGroup
        config={config}
        onConfigChange={onConfigChange}
        onConfigUpdate={onConfigUpdate}
        collapsed={workaroundsCollapsed}
        onToggle={() => setWorkaroundsCollapsed(!workaroundsCollapsed)}
      />

      <ExternalToolsConfigurationGroup
        config={config}
        onConfigChange={onConfigChange}
        collapsed={externalToolsCollapsed}
        onToggle={() => setExternalToolsCollapsed(!externalToolsCollapsed)}
      />

      <ManualOverridesConfigurationGroup
        config={config}
        onConfigChange={onConfigChange}
        collapsed={manualOverridesCollapsed}
        onToggle={() => setManualOverridesCollapsed(!manualOverridesCollapsed)}
      />
    </>
  );
}
