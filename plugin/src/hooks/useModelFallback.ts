import { useEffect, useRef, useState } from "react";
import type { ModelStatusResult } from "../api/makoApi";
import type {
  ConfigurationData,
  ConfigurationPatch,
} from "../config/configSchema";

interface ModelFallbackState {
  dllMissing: boolean;
  lsfg: ModelStatusResult | null;
  ls1: ModelStatusResult | null;
  ls1RuntimeFallback: boolean;
}

interface ModelFallbackOptions {
  config: ConfigurationData;
  profileName: string;
  powerMode?: string;
  enabled: boolean;
  dllMissing: boolean;
  dllDetected: boolean;
  lsfg?: ModelStatusResult | null;
  ls1?: ModelStatusResult | null;
  ls1RuntimeFallback: boolean;
  saveConfigChanges: (changes: ConfigurationPatch) => Promise<void>;
}

/** Repair only confirmed unavailable selections through the ordinary profile writer. */
export function useModelFallback({
  config,
  profileName,
  powerMode,
  enabled,
  dllMissing,
  dllDetected,
  lsfg,
  ls1,
  ls1RuntimeFallback,
  saveConfigChanges,
}: ModelFallbackOptions): ModelFallbackState {
  const scope = JSON.stringify([
    profileName,
    powerMode,
    config.dll,
    config.allow_fp16,
    config.scaling_sharpness,
  ]);
  const [notice, setNotice] = useState<
    (ModelFallbackState & { scope: string; detected: boolean }) | null
  >(null);
  const lastRequest = useRef<{
    key: string;
    detected: boolean;
    fg: boolean;
    scaling: boolean;
  } | null>(null);
  const lsfgSelected = config.frame_generation_model === "lossless-scaling";
  const ls1Selected = ["ls1", "ls1-performance"].includes(config.scaling_method);
  // A missing DLL can return, as an unmounted SD card does, so the saved choice
  // stays and the Renderer uses MAKO until then.
  const confirmedFailure = (status: ModelStatusResult | null | undefined) =>
    status?.compatible === false && status.reason !== "dll-unavailable";
  const changeFg = lsfgSelected && !dllMissing && confirmedFailure(lsfg);
  const changeScaling =
    ls1Selected &&
    !dllMissing &&
    (confirmedFailure(ls1) || ls1RuntimeFallback);

  useEffect(() => {
    const previous = lastRequest.current;
    if (
      previous &&
      (!previous.detected
        ? dllDetected
        : (!previous.fg || lsfg?.compatible === true) &&
          (!previous.scaling || ls1?.compatible === true))
    )
      lastRequest.current = null;
    if (!enabled || (!changeFg && !changeScaling)) return;
    const request = JSON.stringify([
      scope,
      config.frame_generation_model,
      config.scaling_method,
      changeFg,
      changeScaling,
    ]);
    // A failed save is reconciled by the writer; do not create an automatic retry loop.
    if (lastRequest.current?.key === request) return;
    lastRequest.current = {
      key: request,
      detected: dllDetected,
      fg: changeFg,
      scaling: changeScaling,
    };
    const changes: ConfigurationPatch = {};
    if (changeFg) changes.frame_generation_model = "mako-fg";
    if (changeScaling) changes.scaling_method = "mako";
    setNotice({
      scope,
      detected: dllDetected,
      dllMissing,
      lsfg: changeFg ? (lsfg ?? null) : null,
      ls1: changeScaling ? (ls1 ?? null) : null,
      ls1RuntimeFallback,
    });
    void saveConfigChanges(changes);
  }, [
    enabled,
    scope,
    changeFg,
    changeScaling,
    config.frame_generation_model,
    config.scaling_method,
    dllMissing,
    dllDetected,
    lsfg,
    ls1,
    ls1RuntimeFallback,
    saveConfigChanges,
  ]);

  // Keep the explanation after the saved selection changes, scoped to this editor/source.
  const current =
    enabled && notice?.scope === scope && !(!notice.detected && dllDetected)
      ? notice
      : null;
  if (!enabled)
    return {
      dllMissing: false,
      lsfg: null,
      ls1: null,
      ls1RuntimeFallback: false,
    };
  return {
    dllMissing:
      dllMissing && Boolean(lsfgSelected || ls1Selected || current?.dllMissing),
    lsfg: lsfg ?? current?.lsfg ?? null,
    ls1: ls1 ?? current?.ls1 ?? null,
    ls1RuntimeFallback:
      ls1RuntimeFallback || Boolean(current?.ls1RuntimeFallback),
  };
}
