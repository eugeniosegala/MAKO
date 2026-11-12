import {
  FLOW_SCALE,
  PERFORMANCE_MODE,
  SCALING_METHOD_MAKO,
  ULTRA_PERFORMANCE,
  ULTRA_PERFORMANCE_FLOW_SCALE,
  ULTRA_PERFORMANCE_MAKO_FG_FLOW_SCALE,
  getDefaults,
  type ConfigurationData,
} from "./configSchema";

const DEFAULT_CONFIGURATION = getDefaults();

export function effectiveScalingMethod(config: ConfigurationData): string {
  return config.scaling_enabled && config.ultra_performance
    ? SCALING_METHOD_MAKO
    : config.scaling_method;
}

/** Ultra Performance locks each model at its own Flow Scale. */
export function ultraPerformanceFlowScale(
  config: Pick<ConfigurationData, "frame_generation_model">,
): number {
  return config.frame_generation_model === "mako-fg"
    ? ULTRA_PERFORMANCE_MAKO_FG_FLOW_SCALE
    : ULTRA_PERFORMANCE_FLOW_SCALE;
}

export function ultraPerformanceChanges(
  enabled: boolean,
  config: Pick<ConfigurationData, "frame_generation_model">,
): Partial<ConfigurationData> {
  return {
    [ULTRA_PERFORMANCE]: enabled,
    [FLOW_SCALE]: enabled
      ? ultraPerformanceFlowScale(config)
      : DEFAULT_CONFIGURATION.flow_scale,
    [PERFORMANCE_MODE]: enabled,
  };
}
