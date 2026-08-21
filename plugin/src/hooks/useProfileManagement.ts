import { useState, useEffect, useCallback } from "react";
import {
  getProfiles,
  deleteProfile,
  renameProfile,
  setCurrentProfile,
  syncCurrentProfile,
  updateProfileConfig,
  configFailureResult,
  profileFailureResult,
  profilesFailureResult,
  type ProfilesResult,
  type ProfileResult,
  type ConfigUpdateResult,
} from "../api/makoApi";
import {
  ConfigurationData,
  DEFAULT_PROFILE_NAME,
} from "../config/configSchema";
import { showSuccessToast, showErrorToast } from "../utils/toastUtils";
import t from "../i18n/i18n";

export function useProfileManagement() {
  const [profiles, setProfiles] = useState<string[]>([]);
  const [currentProfile, setCurrentProfileState] =
    useState<string>(DEFAULT_PROFILE_NAME);
  const [isLoading, setIsLoading] = useState(false);

  // Load profiles on hook initialization
  const loadProfiles = useCallback(async () => {
    try {
      const result: ProfilesResult = await getProfiles();
      if (result.success && result.profiles) {
        setProfiles(result.profiles);
        const resolvedProfile =
          result.current_profile &&
          result.profiles.includes(result.current_profile)
            ? result.current_profile
            : result.profiles.includes(DEFAULT_PROFILE_NAME)
              ? DEFAULT_PROFILE_NAME
              : result.profiles[0];
        if (resolvedProfile) setCurrentProfileState(resolvedProfile);
        return result;
      } else {
        console.error("Failed to load profiles:", result.error);
        showErrorToast(
          t("PROFILE_LOAD_FAILED", "Failed to load profiles"),
          result.error || t("PROFILE_UNKNOWN_ERROR", "Unknown error"),
        );
        return result;
      }
    } catch (error) {
      console.error("Error loading profiles:", error);
      showErrorToast(
        t("PROFILE_LOAD_ERROR", "Error loading profiles"),
        String(error),
      );
      return profilesFailureResult(String(error));
    }
  }, []);

  // Delete a profile
  const handleDeleteProfile = useCallback(
    async (profileName: string) => {
      if (profileName === DEFAULT_PROFILE_NAME) {
        showErrorToast(
          t("PROFILE_CANNOT_DELETE_TITLE", "Cannot delete default profile"),
          t(
            "PROFILE_CANNOT_DELETE_MSG",
            "The default profile cannot be deleted",
          ),
        );
        return profileFailureResult(
          t("PROFILE_CANNOT_DELETE_TITLE", "Cannot delete default profile"),
        );
      }

      setIsLoading(true);
      try {
        const result: ProfileResult = await deleteProfile(profileName);
        if (result.success) {
          showSuccessToast(
            t("PROFILE_DELETED", "Profile deleted"),
            `${t("PROFILE_DELETED_DESC", "Deleted profile:")} ${profileName}`,
          );
          await loadProfiles();
          // If we deleted the current profile, it should have switched to default
          if (currentProfile === profileName) {
            setCurrentProfileState(DEFAULT_PROFILE_NAME);
          }
          return result;
        } else {
          console.error("Failed to delete profile:", result.error);
          showErrorToast(
            t("PROFILE_DELETE_FAILED", "Failed to delete profile"),
            result.error || t("PROFILE_UNKNOWN_ERROR", "Unknown error"),
          );
          return result;
        }
      } catch (error) {
        console.error("Error deleting profile:", error);
        showErrorToast(
          t("PROFILE_DELETE_ERROR", "Error deleting profile"),
          String(error),
        );
        return profileFailureResult(String(error));
      } finally {
        setIsLoading(false);
      }
    },
    [currentProfile, loadProfiles],
  );

  // Rename a profile
  const handleRenameProfile = useCallback(
    async (oldName: string, newName: string) => {
      if (oldName === DEFAULT_PROFILE_NAME) {
        showErrorToast(
          t("PROFILE_CANNOT_RENAME_TITLE", "Cannot rename default profile"),
          t(
            "PROFILE_CANNOT_RENAME_MSG",
            "The default profile cannot be renamed",
          ),
        );
        return profileFailureResult(
          t("PROFILE_CANNOT_RENAME_TITLE", "Cannot rename default profile"),
        );
      }

      setIsLoading(true);
      try {
        const result: ProfileResult = await renameProfile(oldName, newName);
        if (result.success) {
          // Use the normalized name returned from backend (spaces converted to dashes)
          const actualNewName = result.profile_name || newName;
          showSuccessToast(
            t("PROFILE_RENAMED", "Profile renamed"),
            `${t("PROFILE_RENAMED_DESC", "Renamed profile to:")} ${actualNewName}`,
          );
          await loadProfiles();
          // Update current profile if it was renamed
          if (currentProfile === oldName) {
            setCurrentProfileState(actualNewName);
          }
          return result;
        } else {
          console.error("Failed to rename profile:", result.error);
          showErrorToast(
            t("PROFILE_RENAME_FAILED", "Failed to rename profile"),
            result.error || t("PROFILE_UNKNOWN_ERROR", "Unknown error"),
          );
          return result;
        }
      } catch (error) {
        console.error("Error renaming profile:", error);
        showErrorToast(
          t("PROFILE_RENAME_ERROR", "Error renaming profile"),
          String(error),
        );
        return profileFailureResult(String(error));
      } finally {
        setIsLoading(false);
      }
    },
    [currentProfile, loadProfiles],
  );

  // Set the current active profile
  const handleSetCurrentProfile = useCallback(async (profileName: string) => {
    setIsLoading(true);
    try {
      const result: ProfileResult = await setCurrentProfile(profileName);
      if (result.success) {
        setCurrentProfileState(profileName);
        showSuccessToast(
          t("PROFILE_SWITCHED", "Profile switched"),
          `${t("PROFILE_SWITCHED_DESC", "Switched to profile:")} ${profileName}`,
        );
        return result;
      } else {
        console.error("Failed to switch profile:", result.error);
        showErrorToast(
          t("PROFILE_SWITCH_FAILED", "Failed to switch profile"),
          result.error || t("PROFILE_UNKNOWN_ERROR", "Unknown error"),
        );
        return result;
      }
    } catch (error) {
      console.error("Error switching profile:", error);
      showErrorToast(
        t("PROFILE_SWITCH_ERROR", "Error switching profile"),
        String(error),
      );
      return profileFailureResult(String(error));
    } finally {
      setIsLoading(false);
    }
  }, []);

  const handleSyncCurrentProfile = useCallback(async (appId?: string) => {
    try {
      const result: ProfileResult = await syncCurrentProfile(appId || "");
      if (result.success && result.profile_name) {
        setCurrentProfileState(result.profile_name);
      } else if (!result.success) {
        console.error("Failed to synchronise current profile:", result.error);
      }
      return result;
    } catch (error) {
      console.error("Error synchronising current profile:", error);
      return profileFailureResult(String(error), { changed: false });
    }
  }, []);

  // Update configuration for a specific profile
  const handleUpdateProfileConfig = useCallback(
    async (profileName: string, config: ConfigurationData) => {
      setIsLoading(true);
      try {
        const result: ConfigUpdateResult = await updateProfileConfig(
          profileName,
          config,
        );
        if (result.success) {
          return result;
        } else {
          console.error("Failed to update profile config:", result.error);
          showErrorToast(
            t(
              "PROFILE_UPDATE_CONFIG_FAILED",
              "Failed to update profile config",
            ),
            result.error || t("PROFILE_UNKNOWN_ERROR", "Unknown error"),
          );
          return result;
        }
      } catch (error) {
        console.error("Error updating profile config:", error);
        showErrorToast(
          t("PROFILE_UPDATE_CONFIG_ERROR", "Error updating profile config"),
          String(error),
        );
        return configFailureResult(String(error));
      } finally {
        setIsLoading(false);
      }
    },
    [currentProfile],
  );

  // Initialize profiles on mount
  useEffect(() => {
    loadProfiles();
  }, [loadProfiles]);

  return {
    profiles,
    currentProfile,
    isLoading,
    loadProfiles,
    deleteProfile: handleDeleteProfile,
    renameProfile: handleRenameProfile,
    setCurrentProfile: handleSetCurrentProfile,
    syncCurrentProfile: handleSyncCurrentProfile,
    updateProfileConfig: handleUpdateProfileConfig,
  };
}
