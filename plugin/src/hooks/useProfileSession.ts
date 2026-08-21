import { useCallback, useEffect, useRef, useState } from "react";
import { Router, type AppOverview } from "@decky/ui";
import type { ProfileResult } from "../api/makoApi";
import { DEFAULT_PROFILE_NAME } from "../config/configSchema";

const PROFILE_SYNC_INTERVAL_MS = 2000;

interface ProfileSessionOptions {
  isInstalled: boolean;
  loadProfileConfig: (profileName?: string) => Promise<void>;
  syncCurrentProfile: (appId?: string) => Promise<ProfileResult>;
}

/**
 * Coordinates the profile being edited with Decky's running-game state.
 *
 * A live game locks the editor to its resolved profile. After that game exits,
 * the editor returns to Default exactly once; later offline profile selections
 * remain untouched until another game starts.
 */
export function useProfileSession({
  isInstalled,
  loadProfileConfig,
  syncCurrentProfile,
}: ProfileSessionOptions) {
  const [mainRunningApp, setMainRunningApp] = useState<
    AppOverview | undefined
  >(undefined);
  const [editingProfile, setEditingProfile] =
    useState<string>(DEFAULT_PROFILE_NAME);
  const editingProfileRef = useRef<string>(DEFAULT_PROFILE_NAME);
  const gameWasRunningRef = useRef(false);

  useEffect(() => {
    if (isInstalled) {
      void loadProfileConfig(editingProfileRef.current);
    }
  }, [isInstalled, loadProfileConfig]);

  useEffect(() => {
    let cancelled = false;
    let syncInFlight = false;

    const checkRunningApp = async () => {
      const runningApp = Router.MainRunningApp;
      if (syncInFlight) return;

      syncInFlight = true;
      try {
        const result = await syncCurrentProfile(
          runningApp ? String(runningApp.appid) : undefined,
        );
        if (!cancelled && result.success) {
          const gameIsRunning = Boolean(result.game_running && runningApp);
          const nextEditingProfile = gameIsRunning
            ? result.profile_name || DEFAULT_PROFILE_NAME
            : gameWasRunningRef.current
              ? DEFAULT_PROFILE_NAME
              : undefined;
          const editingProfileChanged = Boolean(
            nextEditingProfile &&
              nextEditingProfile !== editingProfileRef.current,
          );

          // On exit, reset the editor before unlocking profile controls. On
          // launch, lock controls before following the detected game profile.
          if (!gameIsRunning && editingProfileChanged && nextEditingProfile) {
            editingProfileRef.current = nextEditingProfile;
            setEditingProfile(nextEditingProfile);
          }
          setMainRunningApp(gameIsRunning ? runningApp : undefined);
          gameWasRunningRef.current = gameIsRunning;
          if (gameIsRunning && editingProfileChanged && nextEditingProfile) {
            editingProfileRef.current = nextEditingProfile;
            setEditingProfile(nextEditingProfile);
          }
          if (editingProfileChanged && nextEditingProfile) {
            await loadProfileConfig(nextEditingProfile);
          }
        }
      } finally {
        syncInFlight = false;
      }
    };

    void checkRunningApp();
    const interval = setInterval(
      () => void checkRunningApp(),
      PROFILE_SYNC_INTERVAL_MS,
    );
    return () => {
      cancelled = true;
      clearInterval(interval);
    };
  }, [loadProfileConfig, syncCurrentProfile]);

  const selectEditingProfile = useCallback((profileName: string) => {
    editingProfileRef.current = profileName;
    setEditingProfile(profileName);
  }, []);

  const getEditingProfile = useCallback(
    () => editingProfileRef.current,
    [],
  );

  return {
    mainRunningApp,
    editingProfile,
    selectEditingProfile,
    getEditingProfile,
  };
}
