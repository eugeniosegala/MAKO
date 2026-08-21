import { useCallback } from "react";
import type { ConfigUpdateResult } from "../api/makoApi";
import type {
  ConfigurationData,
  ConfigurationPatch,
} from "../config/configSchema";

type UpdateProfileConfigFields = (
  profileName: string,
  changes: ConfigurationPatch,
) => Promise<ConfigUpdateResult>;

interface ProfileConfigWriterOptions {
  editingProfile: string;
  getEditingProfile: () => string;
  updateProfileConfigFields: UpdateProfileConfigFields;
  loadProfileConfig: (profileName: string) => Promise<void>;
}

/**
 * Creates profile-bound field writers for immediate and deferred controls.
 *
 * Each callback retains the profile selected in the render that created it.
 * The backend merges its patch into the latest canonical profile, so delayed
 * writes cannot move to a newly selected profile or revert unrelated changes.
 */
export function useProfileConfigWriter({
  editingProfile,
  getEditingProfile,
  updateProfileConfigFields,
  loadProfileConfig,
}: ProfileConfigWriterOptions) {
  const saveConfigChanges = useCallback(
    async (changes: ConfigurationPatch) => {
      const targetProfile = editingProfile;
      const result = await updateProfileConfigFields(targetProfile, changes);
      if (result.success && getEditingProfile() === targetProfile) {
        await loadProfileConfig(targetProfile);
      }
    },
    [
      editingProfile,
      getEditingProfile,
      loadProfileConfig,
      updateProfileConfigFields,
    ],
  );

  const saveConfigField = useCallback(
    async (
      fieldName: keyof ConfigurationData,
      value: boolean | number | string,
    ) => {
      await saveConfigChanges({
        [fieldName]: value,
      } as ConfigurationPatch);
    },
    [saveConfigChanges],
  );

  return { saveConfigChanges, saveConfigField };
}
