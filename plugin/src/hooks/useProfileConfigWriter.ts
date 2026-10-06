import { useCallback, useEffect, useRef } from "react";
import type { ConfigUpdateResult } from "../api/makoApi";
import type {
  ConfigurationData,
  ConfigurationPatch,
} from "../config/configSchema";

export const PROFILE_CONFIG_SAVE_DELAY_MS = 250;
export const BASE_FPS_CAP_SAVE_DELAY_MS = 1000;

type UpdateProfileConfigFields = (
  profileName: string,
  changes: ConfigurationPatch,
  powerMode?: string,
) => Promise<ConfigUpdateResult>;

interface ProfileConfigWriterOptions {
  editingProfile: string;
  editingPowerMode?: string;
  getEditingPowerMode?: () => string;
  canEditConfig?: () => boolean;
  getEditingProfile: () => string;
  updateProfileConfigFields: UpdateProfileConfigFields;
  loadProfileConfig: (profileName: string, powerMode?: string) => Promise<void>;
  applyConfigPatch: (changes: ConfigurationPatch) => void;
  replaceConfig: (config: ConfigurationData) => void;
}

interface PendingProfileWrite {
  profileName: string;
  powerMode?: string;
  changes: ConfigurationPatch;
  delayMs: number;
}

function saveDelayForChanges(changes: ConfigurationPatch): number {
  const keys = Object.keys(changes) as (keyof ConfigurationData)[];
  const baseFpsCapDrag =
    keys.includes("base_fps_cap") &&
    keys.every(
      (key) =>
        key === "base_fps_cap" ||
        (key === "dynamic_cadence_recovery" &&
          changes.dynamic_cadence_recovery === false),
    );
  return baseFpsCapDrag
    ? BASE_FPS_CAP_SAVE_DELAY_MS
    : PROFILE_CONFIG_SAVE_DELAY_MS;
}

/**
 * Creates one bounded persistence boundary for every profile control.
 *
 * UI state updates optimistically, while rapid edits merge by profile and only
 * one backend request can be active at a time. Each callback retains the
 * profile selected in the render that created it, so queued writes cannot move
 * to a newly selected profile. Pending writes flush when Decky unmounts the
 * quick-access panel.
 */
export function useProfileConfigWriter({
  editingProfile,
  editingPowerMode,
  getEditingPowerMode,
  canEditConfig,
  getEditingProfile,
  updateProfileConfigFields,
  loadProfileConfig,
  applyConfigPatch,
  replaceConfig,
}: ProfileConfigWriterOptions) {
  const pendingWrites = useRef(new Map<string, PendingProfileWrite>());
  const pendingOrder = useRef<string[]>([]);
  const saveTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const writeInFlight = useRef(false);
  const flushImmediately = useRef(false);
  const idleWaiters = useRef<Array<() => void>>([]);
  const finishFlush = () => {
    for (const resolve of idleWaiters.current.splice(0)) resolve();
  };
  const mounted = useRef(true);
  const flushNextWriteRef = useRef<() => void>(() => undefined);

  const isEditing = useCallback(
    (profileName: string, mode?: string) =>
      (!canEditConfig || canEditConfig()) &&
      getEditingProfile() === profileName &&
      (!getEditingPowerMode || getEditingPowerMode() === mode),
    [canEditConfig, getEditingProfile, getEditingPowerMode],
  );
  const writeKey = (profileName: string, mode?: string) =>
    JSON.stringify([profileName, mode || ""]);

  const scheduleWrite = useCallback((delay = PROFILE_CONFIG_SAVE_DELAY_MS) => {
    if (saveTimer.current !== null) clearTimeout(saveTimer.current);
    saveTimer.current = setTimeout(() => {
      saveTimer.current = null;
      flushNextWriteRef.current();
    }, delay);
  }, []);

  const reconcileProfile = useCallback(
    async (profileName: string, mode?: string) => {
      if (mounted.current && isEditing(profileName, mode)) {
        try {
          await loadProfileConfig(
            profileName,
            ...(mode ? ([mode] as [string]) : []),
          );
        } catch {
          return;
        }
        const newerChanges = pendingWrites.current.get(
          writeKey(profileName, mode),
        )?.changes;
        if (newerChanges && isEditing(profileName, mode)) {
          applyConfigPatch(newerChanges);
        }
      }
    },
    [applyConfigPatch, isEditing, loadProfileConfig],
  );

  const flushNextWrite = useCallback(async () => {
    if (writeInFlight.current) return;

    const key = pendingOrder.current.shift();
    if (!key) {
      flushImmediately.current = false;
      finishFlush();
      return;
    }

    const pendingWrite = pendingWrites.current.get(key);
    if (!pendingWrite) {
      flushNextWriteRef.current();
      return;
    }

    const { profileName, powerMode } = pendingWrite;
    pendingWrites.current.delete(key);
    writeInFlight.current = true;
    try {
      const result = await updateProfileConfigFields(
        profileName,
        pendingWrite.changes,
        ...(powerMode ? ([powerMode] as [string]) : []),
      );
      if (mounted.current && isEditing(profileName, powerMode)) {
        if (result.success && result.config) {
          const newerChanges = pendingWrites.current.get(key)?.changes;
          replaceConfig({
            ...result.config,
            ...(newerChanges || {}),
          });
        } else {
          await reconcileProfile(profileName, powerMode);
        }
      }
    } catch {
      await reconcileProfile(profileName, powerMode);
    } finally {
      writeInFlight.current = false;
      if (pendingOrder.current.length > 0) {
        if (flushImmediately.current || !mounted.current) {
          flushNextWriteRef.current();
        } else {
          const nextProfile = pendingOrder.current[0];
          scheduleWrite(
            pendingWrites.current.get(nextProfile)?.delayMs ??
              PROFILE_CONFIG_SAVE_DELAY_MS,
          );
        }
      } else {
        flushImmediately.current = false;
        finishFlush();
      }
    }
  }, [
    isEditing,
    reconcileProfile,
    replaceConfig,
    scheduleWrite,
    updateProfileConfigFields,
  ]);
  flushNextWriteRef.current = () => void flushNextWrite();

  const flushConfigChanges = useCallback((): Promise<void> => {
    if (!writeInFlight.current && pendingOrder.current.length === 0) {
      return Promise.resolve();
    }
    flushImmediately.current = true;
    if (saveTimer.current !== null) {
      clearTimeout(saveTimer.current);
      saveTimer.current = null;
    }
    const completion = new Promise<void>((resolve) =>
      idleWaiters.current.push(resolve),
    );
    flushNextWriteRef.current();
    return completion;
  }, []);

  useEffect(() => {
    mounted.current = true;
    return () => {
      mounted.current = false;
      flushImmediately.current = true;
      if (saveTimer.current !== null) {
        clearTimeout(saveTimer.current);
        saveTimer.current = null;
      }
      flushNextWriteRef.current();
    };
  }, []);

  const saveConfigChanges = useCallback(
    (changes: ConfigurationPatch): Promise<void> => {
      if (canEditConfig && !canEditConfig()) return Promise.resolve();
      const targetProfile = editingProfile;
      const key = writeKey(targetProfile, editingPowerMode);
      const ownedChanges = { ...changes };
      if (isEditing(targetProfile, editingPowerMode)) {
        applyConfigPatch(ownedChanges);
      }

      let pendingWrite = pendingWrites.current.get(key);
      const requestedDelay = saveDelayForChanges(ownedChanges);
      if (!pendingWrite) {
        pendingWrite = {
          profileName: targetProfile,
          powerMode: editingPowerMode,
          changes: {},
          delayMs: requestedDelay,
        };
        pendingWrites.current.set(key, pendingWrite);
        pendingOrder.current.push(key);
      } else {
        pendingWrite.delayMs = Math.min(pendingWrite.delayMs, requestedDelay);
      }
      pendingWrite.changes = {
        ...pendingWrite.changes,
        ...ownedChanges,
      };

      scheduleWrite(pendingWrite.delayMs);
      return Promise.resolve();
    },
    [
      applyConfigPatch,
      canEditConfig,
      editingProfile,
      editingPowerMode,
      isEditing,
      scheduleWrite,
    ],
  );

  const saveConfigField = useCallback(
    async (
      fieldName: keyof ConfigurationData,
      value: boolean | number | string,
    ) => {
      return saveConfigChanges({
        [fieldName]: value,
      } as ConfigurationPatch);
    },
    [saveConfigChanges],
  );

  return { saveConfigChanges, saveConfigField, flushConfigChanges };
}
