import { getDefaults, type ConfigurationData } from "./configSchema";

const DEFAULT_CONFIGURATION = getDefaults();

export function adaptiveModeChanges(
  enabled: boolean,
): Partial<ConfigurationData> {
  return { adaptive: enabled };
}

export function baseFpsCapChanges(value: number): Partial<ConfigurationData> {
  return {
    base_fps_cap: value,
    dynamic_cadence_recovery: false,
  };
}

export function dynamicCadenceRecoveryChanges(
  enabled: boolean,
): Partial<ConfigurationData> {
  if (!enabled) {
    return { dynamic_cadence_recovery: false };
  }

  return {
    dynamic_cadence_recovery: true,
    adaptive_auto_base_fps_cap: false,
    base_fps_cap: 0,
  };
}

export function isFractionalAdaptivePresetEnabled(
  config: ConfigurationData,
): boolean {
  return (
    config.adaptive &&
    !(
      config.adaptive_auto_base_fps_cap ??
      DEFAULT_CONFIGURATION.adaptive_auto_base_fps_cap
    )
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

export function steadyBaseCapChanges(
  enabled: boolean,
): Partial<ConfigurationData> {
  return {
    adaptive_auto_base_fps_cap: enabled,
    dynamic_cadence_recovery: false,
  };
}
