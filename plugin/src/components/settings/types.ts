import type { ConfigurationData } from "../../config/configSchema";

export interface ConfigurationControlProps {
  config: ConfigurationData;
  onConfigChange: (
    fieldName: keyof ConfigurationData,
    value: boolean | number | string,
  ) => Promise<void>;
}

export interface ConfigurationEditorProps extends ConfigurationControlProps {
  onConfigUpdate: (changes: Partial<ConfigurationData>) => Promise<void>;
}

export interface ConfigurationGroupProps extends ConfigurationControlProps {
  collapsed: boolean;
  onToggle: () => void;
}

export interface ConfigurationUpdateGroupProps
  extends ConfigurationGroupProps, ConfigurationEditorProps {}
