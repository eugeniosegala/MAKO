import {
  ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_AUTO,
  ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_HIGH,
  ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_LOW,
  ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_MEDIUM,
  ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_VERY_HIGH,
  type AdaptiveFractionalRealFramePriority,
  getDefaults,
  type ConfigurationData,
} from "./configSchema";

const DEFAULT_CONFIGURATION = getDefaults();

export function adaptiveModeChanges(
  enabled: boolean,
): Partial<ConfigurationData> {
  return { adaptive: enabled };
}

export function baseFpsCapChanges(value: number): Partial<ConfigurationData> {
  return {
    base_fps_cap: value,
    adaptive_fractional_real_frame_priority:
      ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_AUTO,
    dynamic_cadence_recovery: false,
  };
}

export function fractionalRealFramePriorityChanges(
  value: AdaptiveFractionalRealFramePriority,
): Partial<ConfigurationData> {
  return {
    adaptive_fractional_real_frame_priority: value,
    dynamic_cadence_recovery: false,
  };
}

export function fractionalRealFramePriorityCap(
  targetFps: number,
  priority: AdaptiveFractionalRealFramePriority,
): number | undefined {
  switch (priority) {
    case ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_LOW:
      return (targetFps * 3) / 5;
    case ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_MEDIUM:
      return (targetFps * 2) / 3;
    case ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_HIGH:
      return (targetFps * 3) / 4;
    case ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_VERY_HIGH:
      return (targetFps * 4) / 5;
    case ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_AUTO:
      return undefined;
  }
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
    adaptive_fractional_real_frame_priority:
      ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_AUTO,
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
