import { useEffect, useMemo, useRef, useState } from "react";
import type { AppOverview, DropdownOption } from "@decky/ui";
import {
  captureGameProfile,
  deleteProfile,
  getProfiles,
  type ProfileDetails,
  type ProfilesResult,
  renameProfile,
} from "../api/makoApi";
import { DEFAULT_PROFILE_NAME } from "../config/configSchema";
import { showErrorToast, showSuccessToast } from "../utils/toastUtils";
import t from "../i18n/i18n";

interface ProfileEditorModelOptions {
  editingProfile?: string;
  onProfileChange?: (profileName: string) => void | Promise<void>;
  mainRunningApp?: AppOverview;
}

/**
 * Owns the editable profile list and its backend transactions.
 *
 * This is deliberately separate from runtime profile synchronisation: the
 * offline dropdown selects a profile to edit and must never become a runtime
 * override. A running game is captured explicitly and otherwise locks the
 * editor to the profile selected by the runtime session.
 */
export function useProfileEditorModel({
  editingProfile,
  onProfileChange,
  mainRunningApp,
}: ProfileEditorModelOptions) {
  const [profiles, setProfiles] = useState<string[]>([]);
  const [profileDetails, setProfileDetails] = useState<ProfileDetails[]>([]);
  const [selectedProfile, setSelectedProfile] = useState(
    editingProfile || DEFAULT_PROFILE_NAME,
  );
  const editingProfileRef = useRef(editingProfile || DEFAULT_PROFILE_NAME);
  const [isLoading, setIsLoading] = useState(false);

  const loadProfiles = async (preferredProfile?: string): Promise<string> => {
    try {
      const result: ProfilesResult = await getProfiles();
      if (!result.success || !result.profiles) {
        throw new Error(
          result.error || t("PROFILE_UNKNOWN_ERROR", "Unknown error"),
        );
      }
      setProfiles(result.profiles);
      setProfileDetails(result.profile_details || []);
      const resolvedProfile = result.profiles.includes(
        editingProfileRef.current,
      )
        ? editingProfileRef.current
        : preferredProfile && result.profiles.includes(preferredProfile)
          ? preferredProfile
          : result.current_profile &&
              result.profiles.includes(result.current_profile)
            ? result.current_profile
            : result.profiles.includes(DEFAULT_PROFILE_NAME)
              ? DEFAULT_PROFILE_NAME
              : result.profiles[0];
      if (resolvedProfile) setSelectedProfile(resolvedProfile);
      return resolvedProfile || DEFAULT_PROFILE_NAME;
    } catch (error) {
      console.error("Error loading profiles:", error);
      showErrorToast(
        t("PROFILE_LOAD_FAILED", "Failed to load profiles"),
        String(error),
      );
      return DEFAULT_PROFILE_NAME;
    }
  };

  useEffect(() => {
    void loadProfiles(editingProfile || DEFAULT_PROFILE_NAME);
  }, []);

  useEffect(() => {
    if (editingProfile) {
      editingProfileRef.current = editingProfile;
      setSelectedProfile(editingProfile);
    }
  }, [editingProfile]);

  const selectedDetails = useMemo(
    () =>
      profileDetails.find(
        (profile) => profile.profile_name === selectedProfile,
      ),
    [profileDetails, selectedProfile],
  );
  const runningProfile = useMemo(
    () =>
      profileDetails.find(
        (profile) =>
          profile.steam_app_id &&
          profile.steam_app_id === String(mainRunningApp?.appid || ""),
      ),
    [profileDetails, mainRunningApp],
  );

  const notifyProfileChanged = async (profileName: string) => {
    const resolvedProfile = await loadProfiles(profileName);
    await onProfileChange?.(resolvedProfile || profileName);
    return resolvedProfile;
  };

  const switchProfile = async (profileName: string) => {
    setIsLoading(true);
    try {
      if (!profiles.includes(profileName)) {
        throw new Error(`Profile '${profileName}' does not exist`);
      }
      editingProfileRef.current = profileName;
      setSelectedProfile(profileName);
      await onProfileChange?.(profileName);
    } catch (error) {
      showErrorToast(
        t("PROFILE_SWITCH_FAILED", "Failed to switch profile"),
        String(error),
      );
    } finally {
      setIsLoading(false);
    }
  };

  const saveRunningGame = async () => {
    if (!mainRunningApp) return;
    setIsLoading(true);
    try {
      const result = await captureGameProfile(
        String(mainRunningApp.appid),
        mainRunningApp.display_name,
        selectedProfile,
      );
      if (!result.success || !result.profile_name) {
        throw new Error(result.error || "Unknown error");
      }
      showSuccessToast(
        t("PROFILE_GAME_SAVED", "Game profile saved"),
        result.profile?.processes?.length
          ? `${mainRunningApp.display_name}: ${result.profile.processes.join(", ")}`
          : mainRunningApp.display_name,
      );
      editingProfileRef.current = result.profile_name;
      setSelectedProfile(result.profile_name);
      await notifyProfileChanged(result.profile_name);
    } catch (error) {
      showErrorToast(
        t("PROFILE_GAME_SAVE_FAILED", "Could not save game profile"),
        String(error),
      );
    } finally {
      setIsLoading(false);
    }
  };

  const renameSelectedProfile = async (newName: string) => {
    setIsLoading(true);
    try {
      const result = await renameProfile(selectedProfile, newName);
      if (!result.success || !result.profile_name) {
        throw new Error(result.error || "Unknown error");
      }
      editingProfileRef.current = result.profile_name;
      setSelectedProfile(result.profile_name);
      await notifyProfileChanged(result.profile_name);
      showSuccessToast(t("PROFILE_RENAMED", "Profile renamed"), newName);
    } catch (error) {
      showErrorToast(
        t("PROFILE_RENAME_FAILED", "Failed to rename profile"),
        String(error),
      );
    } finally {
      setIsLoading(false);
    }
  };

  const deleteSelectedProfile = async () => {
    setIsLoading(true);
    try {
      const deletedName = selectedDetails?.display_name || selectedProfile;
      const result = await deleteProfile(selectedProfile);
      if (!result.success) throw new Error(result.error || "Unknown error");
      const nextProfile = result.current_profile || DEFAULT_PROFILE_NAME;
      editingProfileRef.current = nextProfile;
      setSelectedProfile(nextProfile);
      await notifyProfileChanged(nextProfile);
      showSuccessToast(t("PROFILE_DELETED", "Profile deleted"), deletedName);
    } catch (error) {
      showErrorToast(
        t("PROFILE_DELETE_FAILED", "Failed to delete profile"),
        String(error),
      );
    } finally {
      setIsLoading(false);
    }
  };

  const profileOptions: DropdownOption[] = profiles.map((profileName) => {
    const detail = profileDetails.find(
      (item) => item.profile_name === profileName,
    );
    return {
      data: profileName,
      label:
        detail?.display_name ||
        (profileName === DEFAULT_PROFILE_NAME
          ? t("PROFILE_DEFAULT", "Default")
          : profileName),
    };
  });

  return {
    selectedProfile,
    selectedDetails,
    runningProfile,
    profileOptions,
    isLoading,
    switchProfile,
    saveRunningGame,
    renameSelectedProfile,
    deleteSelectedProfile,
  };
}
