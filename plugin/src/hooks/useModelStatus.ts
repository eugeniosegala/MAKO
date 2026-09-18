import { useEffect, useState } from "react";
import {
  checkScalingModel,
  checkFrameGenerationModel,
  type ModelStatusResult,
} from "../api/makoApi";
import {
  SCALING_METHOD_LS1,
  SCALING_METHOD_LS1_PERFORMANCE,
  type ConfigurationData,
} from "../config/configSchema";
import { effectiveScalingMethod } from "../config/ultraPerformancePreset";
import {
  MODEL_STATUS_DEBOUNCE_MS,
  MODEL_STATUS_POLL_INTERVAL_MS,
} from "../config/uiTiming";

interface ModelStatuses {
  ls1: ModelStatusResult | null;
  lsfg: ModelStatusResult | null;
}
const EMPTY_STATUS: ModelStatuses = { ls1: null, lsfg: null };

async function inspect(action: () => Promise<ModelStatusResult>) {
  try {
    const status = await action();
    return {
      compatible:
        typeof status.compatible === "boolean" ? status.compatible : null,
      reason: typeof status.reason === "string" ? status.reason : null,
    };
  } catch {
    // An older/missing backend is not proof that the model has failed.
    return null;
  }
}

export function useModelStatus(
  config: ConfigurationData,
  enabled: boolean,
): ModelStatuses {
  const method = effectiveScalingMethod(config);
  const allowFp16 = config.ultra_performance || config.allow_fp16;
  const ls1 =
    enabled &&
    !config.disable_mako &&
    config.scaling_enabled &&
    (method === SCALING_METHOD_LS1 ||
      method === SCALING_METHOD_LS1_PERFORMANCE);
  const lsfg =
    enabled && !config.disable_mako && config.frame_generation_enabled;
  const key = JSON.stringify([
    config.dll,
    ls1,
    method,
    config.scaling_sharpness,
    lsfg,
    allowFp16,
  ]);
  const [result, setResult] = useState<
    (ModelStatuses & { key: string }) | null
  >(null);

  useEffect(() => {
    if (!ls1 && !lsfg) return;
    let active = true;
    let timer: ReturnType<typeof setTimeout>;
    const refresh = async () => {
      // One sequence prevents competing inspector requests and translation queues.
      const fgStatus = lsfg
        ? await inspect(() => checkFrameGenerationModel(config.dll, allowFp16))
        : null;
      if (!active) return;
      const scalingStatus = ls1
        ? await inspect(() =>
            checkScalingModel(config.dll, method, config.scaling_sharpness),
          )
        : null;
      if (active) {
        setResult({ key, ls1: scalingStatus, lsfg: fgStatus });
        timer = setTimeout(refresh, MODEL_STATUS_POLL_INTERVAL_MS);
      }
    };
    timer = setTimeout(refresh, MODEL_STATUS_DEBOUNCE_MS);
    return () => {
      active = false;
      clearTimeout(timer);
    };
  }, [ls1, lsfg, key, config.dll, method, config.scaling_sharpness, allowFp16]);

  return (ls1 || lsfg) && result?.key === key ? result : EMPTY_STATUS;
}
