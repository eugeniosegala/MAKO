import type { ConfigurationData } from "./configSchema";

export function isFractionalAdaptivePresetEnabled(config: ConfigurationData): boolean {
  return (config.frame_generation_enabled ?? true)
    && config.adaptive
    && !(config.adaptive_auto_base_fps_cap ?? true);
}

export function fractionalAdaptivePresetChanges(
  enabled: boolean
): Partial<ConfigurationData> {
  if (!enabled) {
    return { adaptive_auto_base_fps_cap: true };
  }

  return {
    frame_generation_enabled: true,
    adaptive: true,
    adaptive_auto_base_fps_cap: false
  };
}
