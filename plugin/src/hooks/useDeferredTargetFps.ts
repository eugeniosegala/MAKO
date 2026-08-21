import { useEffect, useRef, useState } from "react";
import { TARGET_FPS } from "../config/generatedConfigSchema";
import type { ConfigurationData } from "../config/configSchema";

const TARGET_FPS_SAVE_DELAY_MS = 250;

type SaveConfigurationField = (
  fieldName: keyof ConfigurationData,
  value: boolean | number | string,
) => Promise<void>;

/**
 * Keeps Decky's target-FPS slider responsive while coalescing backend writes.
 * The last unsaved value is flushed on unmount so closing the quick-access
 * menu cannot discard the user's final slider position.
 */
export function useDeferredTargetFps(
  configuredTargetFps: number,
  saveConfigurationField: SaveConfigurationField,
) {
  const [targetFps, setTargetFps] = useState(configuredTargetFps);
  const pendingTargetFps = useRef<{
    value: number;
    save: SaveConfigurationField;
  } | null>(null);
  const targetFpsSaveTimer = useRef<ReturnType<typeof setTimeout> | null>(null);

  useEffect(() => {
    if (pendingTargetFps.current === null) {
      setTargetFps(configuredTargetFps);
    }
  }, [configuredTargetFps]);

  useEffect(
    () => () => {
      if (targetFpsSaveTimer.current !== null) {
        clearTimeout(targetFpsSaveTimer.current);
      }
      if (pendingTargetFps.current !== null) {
        const pendingChange = pendingTargetFps.current;
        pendingTargetFps.current = null;
        void pendingChange.save(TARGET_FPS, pendingChange.value);
      }
    },
    [],
  );

  const changeTargetFps = (value: number) => {
    setTargetFps(value);
    pendingTargetFps.current = { value, save: saveConfigurationField };
    if (targetFpsSaveTimer.current !== null) {
      clearTimeout(targetFpsSaveTimer.current);
    }
    targetFpsSaveTimer.current = setTimeout(() => {
      const pendingChange = pendingTargetFps.current;
      pendingTargetFps.current = null;
      targetFpsSaveTimer.current = null;
      if (pendingChange !== null) {
        void pendingChange.save(TARGET_FPS, pendingChange.value);
      }
    }, TARGET_FPS_SAVE_DELAY_MS);
  };

  return { targetFps, changeTargetFps };
}
