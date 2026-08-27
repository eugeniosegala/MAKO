import { useCallback, useEffect, useRef } from "react";
import type { ConfigUpdateResult } from "../api/makoApi";
import type {
  ConfigurationData,
  ConfigurationPatch,
} from "../config/configSchema";

export const PROFILE_CONFIG_SAVE_DELAY_MS = 250;

type UpdateProfileConfigFields = (
  profileName: string,
  changes: ConfigurationPatch,
) => Promise<ConfigUpdateResult>;

interface ProfileConfigWriterOptions {
  editingProfile: string;
  getEditingProfile: () => string;
  updateProfileConfigFields: UpdateProfileConfigFields;
  loadProfileConfig: (profileName: string) => Promise<void>;
  applyConfigPatch: (changes: ConfigurationPatch) => void;
  replaceConfig: (config: ConfigurationData) => void;
}

interface PendingProfileWrite {
  changes: ConfigurationPatch;
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
  const mounted = useRef(true);
  const flushNextWriteRef = useRef<() => void>(() => undefined);

  const scheduleWrite = useCallback((delay = PROFILE_CONFIG_SAVE_DELAY_MS) => {
    if (saveTimer.current !== null) clearTimeout(saveTimer.current);
    saveTimer.current = setTimeout(() => {
      saveTimer.current = null;
      flushNextWriteRef.current();
    }, delay);
  }, []);

  const reconcileProfile = useCallback(
    async (profileName: string) => {
      if (mounted.current && getEditingProfile() === profileName) {
        try {
          await loadProfileConfig(profileName);
        } catch {
          return;
        }
        const newerChanges = pendingWrites.current.get(profileName)?.changes;
        if (newerChanges && getEditingProfile() === profileName) {
          applyConfigPatch(newerChanges);
        }
      }
    },
    [applyConfigPatch, getEditingProfile, loadProfileConfig],
  );

  const flushNextWrite = useCallback(async () => {
    if (writeInFlight.current) return;

    const profileName = pendingOrder.current.shift();
    if (!profileName) {
      flushImmediately.current = false;
      return;
    }

    const pendingWrite = pendingWrites.current.get(profileName);
    if (!pendingWrite) {
      flushNextWriteRef.current();
      return;
    }

    pendingWrites.current.delete(profileName);
    writeInFlight.current = true;
    try {
      const result = await updateProfileConfigFields(
        profileName,
        pendingWrite.changes,
      );
      if (mounted.current && getEditingProfile() === profileName) {
        if (result.success && result.config) {
          const newerChanges = pendingWrites.current.get(profileName)?.changes;
          replaceConfig({
            ...result.config,
            ...(newerChanges || {}),
          });
        } else {
          await reconcileProfile(profileName);
        }
      }
    } catch {
      await reconcileProfile(profileName);
    } finally {
      writeInFlight.current = false;
      if (pendingOrder.current.length > 0) {
        if (flushImmediately.current || !mounted.current) {
          flushNextWriteRef.current();
        } else {
          scheduleWrite();
        }
      } else {
        flushImmediately.current = false;
      }
    }
  }, [
    getEditingProfile,
    reconcileProfile,
    replaceConfig,
    scheduleWrite,
    updateProfileConfigFields,
  ]);
  flushNextWriteRef.current = () => void flushNextWrite();

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
      const targetProfile = editingProfile;
      const ownedChanges = { ...changes };
      if (getEditingProfile() === targetProfile) {
        applyConfigPatch(ownedChanges);
      }

      let pendingWrite = pendingWrites.current.get(targetProfile);
      if (!pendingWrite) {
        pendingWrite = { changes: {} };
        pendingWrites.current.set(targetProfile, pendingWrite);
        pendingOrder.current.push(targetProfile);
      }
      pendingWrite.changes = {
        ...pendingWrite.changes,
        ...ownedChanges,
      };

      scheduleWrite();
      return Promise.resolve();
    },
    [applyConfigPatch, editingProfile, getEditingProfile, scheduleWrite],
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

  return { saveConfigChanges, saveConfigField };
}
