import { useEffect, useState } from "react";
import { checkScalingModel } from "../api/makoApi";
import {
  SCALING_METHOD_LS1,
  SCALING_METHOD_LS1_PERFORMANCE,
  type ConfigurationData,
} from "../config/configSchema";
import { effectiveScalingMethod } from "../config/ultraPerformancePreset";
import {
  SCALING_MODEL_DEBOUNCE_MS,
  SCALING_MODEL_POLL_INTERVAL_MS,
} from "../config/uiTiming";

export function useScalingModelStatus(
  config: ConfigurationData,
  enabled: boolean,
) {
  const method = effectiveScalingMethod(config);
  const applicable =
    enabled &&
    config.scaling_enabled &&
    (method === SCALING_METHOD_LS1 || method === SCALING_METHOD_LS1_PERFORMANCE);
  const key = JSON.stringify([config.dll, method, config.scaling_sharpness]);
  const [result, setResult] = useState<{
    key: string;
    compatible: boolean | null;
  } | null>(null);

  useEffect(() => {
    if (!applicable) return;
    let active = true;
    let timer: ReturnType<typeof setTimeout>;
    const refresh = async () => {
      let compatible: boolean | null = null;
      try {
        const status = await checkScalingModel(
          config.dll,
          method,
          config.scaling_sharpness,
        );
        if (typeof status.compatible === "boolean")
          compatible = status.compatible;
      } catch {
        // A missing/older backend is not evidence of model incompatibility.
      }
      if (active) {
        setResult({ key, compatible });
        timer = setTimeout(refresh, SCALING_MODEL_POLL_INTERVAL_MS);
      }
    };
    timer = setTimeout(refresh, SCALING_MODEL_DEBOUNCE_MS);
    return () => {
      active = false;
      clearTimeout(timer);
    };
  }, [applicable, key, config.dll, method, config.scaling_sharpness]);

  return applicable && result?.key === key ? result.compatible : null;
}
