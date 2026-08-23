import { getDefaults, type ConfigurationData } from "./configSchema";

const DEFAULT_CONFIGURATION = getDefaults();

export function adaptiveModeChanges(
  enabled: boolean,
  dynamicCadenceRecovery = false,
): Partial<ConfigurationData> {
  if (!enabled) {
    return { adaptive: false };
  }

  return {
    adaptive: true,
    adaptive_auto_base_fps_cap: !dynamicCadenceRecovery,
    ...(dynamicCadenceRecovery ? { base_fps_cap: 0 } : {}),
  };
}

export function isFractionalAdaptivePresetEnabled(
  config: ConfigurationData,
): boolean {
  return (
    (config.frame_generation_enabled ??
      DEFAULT_CONFIGURATION.frame_generation_enabled) &&
    config.adaptive &&
    !config.dynamic_cadence_recovery &&
    !(config.adaptive_auto_base_fps_cap ??
      DEFAULT_CONFIGURATION.adaptive_auto_base_fps_cap)
  );
}

export function fractionalAdaptivePresetChanges(
  enabled: boolean,
): Partial<ConfigurationData> {
  if (!enabled) {
    return {
      adaptive_auto_base_fps_cap: true,
      dynamic_cadence_recovery: false,
    };
  }

  return {
    frame_generation_enabled: true,
    adaptive: true,
    adaptive_auto_base_fps_cap: false,
    dynamic_cadence_recovery: false,
  };
}
