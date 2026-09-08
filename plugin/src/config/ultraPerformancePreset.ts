import {
  ALLOW_FP16,
  FLOW_SCALE,
  PERFORMANCE_MODE,
  SCALING_METHOD_LS1_PERFORMANCE,
  ULTRA_PERFORMANCE,
  ULTRA_PERFORMANCE_FLOW_SCALE,
  getDefaults,
  type ConfigurationData,
} from "./configSchema";

const DEFAULT_CONFIGURATION = getDefaults();

export function effectiveScalingMethod(config: ConfigurationData): string {
  return config.scaling_enabled && config.ultra_performance
    ? SCALING_METHOD_LS1_PERFORMANCE
    : config.scaling_method;
}

export function ultraPerformanceChanges(
  enabled: boolean,
): Partial<ConfigurationData> {
  return {
    [ULTRA_PERFORMANCE]: enabled,
    [FLOW_SCALE]: enabled
      ? ULTRA_PERFORMANCE_FLOW_SCALE
      : DEFAULT_CONFIGURATION.flow_scale,
    [PERFORMANCE_MODE]: enabled,
    [ALLOW_FP16]: DEFAULT_CONFIGURATION.allow_fp16,
  };
}
