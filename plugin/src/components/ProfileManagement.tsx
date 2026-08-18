import { useEffect, useMemo, useState } from "react";
import {
  AppOverview,
  ButtonItem,
  ConfirmModal,
  DialogButton,
  Dropdown,
  DropdownOption,
  Field,
  Focusable,
  ModalRoot,
  PanelSectionRow,
  TextField,
  showModal
} from "@decky/ui";
import { RiArrowDownSFill, RiArrowUpSFill, RiDeleteBinLine, RiEditLine } from "react-icons/ri";
import {
  captureGameProfile,
  deleteProfile,
  getProfiles,
  ProfileDetails,
  ProfilesResult,
  renameProfile,
  setCurrentProfile
} from "../api/makoApi";
import { showErrorToast, showSuccessToast } from "../utils/toastUtils";
import t from "../i18n/i18n";

const PROFILES_COLLAPSED_KEY = "mako-profiles-collapsed";

interface TextInputModalProps {
  title: string;
  description: string;
  defaultValue?: string;
  okText?: string;
  cancelText?: string;
  onOK: (value: string) => void;
  closeModal?: () => void;
}

function TextInputModal({
  title,
  description,
  defaultValue = "",
  okText = "OK",
  cancelText = "Cancel",
  onOK,
  closeModal
}: TextInputModalProps) {
  const [value, setValue] = useState(defaultValue);
  const handleOK = () => {
    if (value.trim()) {
      onOK(value.trim());
      closeModal?.();
    }
  };

  return (
    <ModalRoot>
      <div style={{ padding: "16px", minWidth: "400px" }}>
        <h2 style={{ marginBottom: "16px" }}>{title}</h2>
        <p style={{ marginBottom: "24px" }}>{description}</p>
        <Field label={t("PROFILE_NAME_LABEL", "Name")} childrenLayout="below" childrenContainerWidth="max">
          <TextField
            value={value}
            onChange={(event) => setValue(event?.target?.value || "")}
            style={{ width: "100%" }}
          />
        </Field>
        <Focusable
          style={{ display: "flex", justifyContent: "flex-end", gap: "8px", marginTop: "24px" }}
          flow-children="horizontal"
        >
          <DialogButton onClick={closeModal}>{cancelText}</DialogButton>
          <DialogButton onClick={handleOK} disabled={!value.trim()}>{okText}</DialogButton>
        </Focusable>
      </div>
    </ModalRoot>
  );
}

interface ProfileManagementProps {
  currentProfile?: string;
  onProfileChange?: (profileName: string) => void | Promise<void>;
  mainRunningApp?: AppOverview;
}

export function ProfileManagement({ currentProfile, onProfileChange, mainRunningApp }: ProfileManagementProps) {
  const [profiles, setProfiles] = useState<string[]>([]);
  const [profileDetails, setProfileDetails] = useState<ProfileDetails[]>([]);
  const [selectedProfile, setSelectedProfile] = useState(currentProfile || "mako");
  const [isLoading, setIsLoading] = useState(false);
  const [focusedAction, setFocusedAction] = useState<"edit" | "delete" | null>(null);
  const [profilesCollapsed, setProfilesCollapsed] = useState(() => {
    try {
      const saved = localStorage.getItem(PROFILES_COLLAPSED_KEY);
      return saved !== null ? JSON.parse(saved) : false;
    } catch {
      return false;
    }
  });

  useEffect(() => {
    try {
      localStorage.setItem(PROFILES_COLLAPSED_KEY, JSON.stringify(profilesCollapsed));
    } catch (error) {
      console.warn("Failed to save profiles collapse state:", error);
    }
  }, [profilesCollapsed]);

  const loadProfiles = async () => {
    try {
      const result: ProfilesResult = await getProfiles();
      if (!result.success || !result.profiles) {
        throw new Error(result.error || t("PROFILE_UNKNOWN_ERROR", "Unknown error"));
      }
      setProfiles(result.profiles);
      setProfileDetails(result.profile_details || []);
      if (result.current_profile) setSelectedProfile(result.current_profile);
    } catch (error) {
      console.error("Error loading profiles:", error);
      showErrorToast(t("PROFILE_LOAD_FAILED", "Failed to load profiles"), String(error));
    }
  };

  useEffect(() => {
    void loadProfiles();
  }, []);

  useEffect(() => {
    if (currentProfile) setSelectedProfile(currentProfile);
  }, [currentProfile]);

  const selectedDetails = useMemo(
    () => profileDetails.find((profile) => profile.profile_name === selectedProfile),
    [profileDetails, selectedProfile]
  );
  const runningProfile = useMemo(
    () => profileDetails.find(
      (profile) => profile.steam_app_id && profile.steam_app_id === String(mainRunningApp?.appid || "")
    ),
    [profileDetails, mainRunningApp]
  );

  const notifyProfileChanged = async (profileName: string) => {
    await loadProfiles();
    await onProfileChange?.(profileName);
  };

  const switchProfile = async (profileName: string) => {
    setIsLoading(true);
    try {
      const result = await setCurrentProfile(profileName);
      if (!result.success) throw new Error(result.error || "Unknown error");
      setSelectedProfile(profileName);
      await notifyProfileChanged(profileName);
    } catch (error) {
      showErrorToast(t("PROFILE_SWITCH_FAILED", "Failed to switch profile"), String(error));
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
        selectedProfile
      );
      if (!result.success || !result.profile_name) {
        throw new Error(result.error || "Unknown error");
      }
      showSuccessToast(
        t("PROFILE_GAME_SAVED", "Game profile saved"),
        result.profile?.processes?.length
          ? `${mainRunningApp.display_name}: ${result.profile.processes.join(", ")}`
          : mainRunningApp.display_name
      );
      setSelectedProfile(result.profile_name);
      await notifyProfileChanged(result.profile_name);
    } catch (error) {
      showErrorToast(t("PROFILE_GAME_SAVE_FAILED", "Could not save game profile"), String(error));
    } finally {
      setIsLoading(false);
    }
  };

  const showRenameProfile = () => {
    if (selectedProfile === "mako") return;
    showModal(
      <TextInputModal
        title={t("PROFILE_RENAME_TITLE", "Rename Profile")}
        description={t("PROFILE_RENAME_DESC_PREFIX", "Choose a friendly name for this game or process profile.")}
        defaultValue={selectedDetails?.display_name || selectedProfile}
        okText={t("PROFILE_RENAME_BTN", "Rename")}
        cancelText={t("PROFILE_CANCEL_BTN", "Cancel")}
        onOK={(name) => void renameSelectedProfile(name)}
      />
    );
  };

  const renameSelectedProfile = async (newName: string) => {
    setIsLoading(true);
    try {
      const result = await renameProfile(selectedProfile, newName);
      if (!result.success || !result.profile_name) throw new Error(result.error || "Unknown error");
      setSelectedProfile(result.profile_name);
      await notifyProfileChanged(result.profile_name);
      showSuccessToast(t("PROFILE_RENAMED", "Profile renamed"), newName);
    } catch (error) {
      showErrorToast(t("PROFILE_RENAME_FAILED", "Failed to rename profile"), String(error));
    } finally {
      setIsLoading(false);
    }
  };

  const showDeleteProfile = () => {
    if (selectedProfile === "mako") return;
    showModal(
      <ConfirmModal
        strTitle={t("PROFILE_DELETE_TITLE", "Delete Game / Process Profile")}
        strDescription={t(
          "PROFILE_DELETE_CONFIRM",
          'Delete "{profile}" and all of its saved settings?',
          { profile: selectedDetails?.display_name || selectedProfile }
        )}
        strOKButtonText={t("PROFILE_DELETE_BTN", "Delete")}
        strCancelButtonText={t("PROFILE_CANCEL_BTN", "Cancel")}
        onOK={() => void deleteSelectedProfile()}
      />
    );
  };

  const deleteSelectedProfile = async () => {
    setIsLoading(true);
    try {
      const deletedName = selectedDetails?.display_name || selectedProfile;
      const result = await deleteProfile(selectedProfile);
      if (!result.success) throw new Error(result.error || "Unknown error");
      setSelectedProfile("mako");
      await notifyProfileChanged("mako");
      showSuccessToast(t("PROFILE_DELETED", "Profile deleted"), deletedName);
    } catch (error) {
      showErrorToast(t("PROFILE_DELETE_FAILED", "Failed to delete profile"), String(error));
    } finally {
      setIsLoading(false);
    }
  };

  const profileOptions: DropdownOption[] = profiles.map((profileName) => {
    const detail = profileDetails.find((item) => item.profile_name === profileName);
    return {
      data: profileName,
      label: detail?.display_name || (profileName === "mako" ? t("PROFILE_DEFAULT", "Default") : profileName)
    };
  });

  return (
    <>
      <style>{`
        .Mako_ProfilesCollapseButton_Container > div > div > div > button,
        .Mako_ProfilesCollapseButton_Container > div > div > div > div > button {
          height: 10px !important;
        }
      `}</style>

      <PanelSectionRow>
        <div style={{ fontSize: "14px", fontWeight: "bold", marginTop: "16px", marginBottom: "4px", color: "white" }}>
          {t("PROFILE_SECTION_TITLE", "Game / Process Profiles")}
        </div>
      </PanelSectionRow>

      <PanelSectionRow>
        <div style={{ fontSize: "11px", lineHeight: "1.35", color: "#b8c5d6", marginBottom: "4px" }}>
          {t(
            "PROFILE_HELP",
            "Start a game, then save its running process once. MAKO will retain its renderer and compatibility settings and select them automatically on future launches."
          )}
        </div>
      </PanelSectionRow>

      {mainRunningApp && !runningProfile && (
        <PanelSectionRow>
          <div className="Mako_BrandButton">
            <ButtonItem layout="below" onClick={() => void saveRunningGame()} disabled={isLoading}>
              {t("PROFILE_SAVE_RUNNING", "Save profile for {game}", { game: mainRunningApp.display_name })}
            </ButtonItem>
          </div>
        </PanelSectionRow>
      )}

      <PanelSectionRow>
        <div className="Mako_ProfilesCollapseButton_Container" style={{ marginTop: "2px", marginBottom: "4px" }}>
          <ButtonItem
            layout="below"
            bottomSeparator={profilesCollapsed ? "standard" : "none"}
            onClick={() => setProfilesCollapsed(!profilesCollapsed)}
          >
            {profilesCollapsed
              ? <RiArrowDownSFill style={{ transform: "translate(0, -13px)", fontSize: "1.5em" }} />
              : <RiArrowUpSFill style={{ transform: "translate(0, -12px)", fontSize: "1.5em" }} />}
          </ButtonItem>
        </div>
      </PanelSectionRow>

      {!profilesCollapsed && (
        <>
          <PanelSectionRow>
            <Field label={t("PROFILE_SAVED_LABEL", "Saved profile")} childrenLayout="below" childrenContainerWidth="max" bottomSeparator="none">
              <Dropdown
                rgOptions={profileOptions}
                selectedOption={selectedProfile}
                onChange={(option) => void switchProfile(String(option.data))}
                disabled={isLoading || !!mainRunningApp}
              />
            </Field>
          </PanelSectionRow>

          {selectedDetails && (
            <PanelSectionRow>
              <div style={{ width: "100%", padding: "6px 8px", boxSizing: "border-box", borderRadius: "4px", background: "rgba(255,255,255,0.06)", color: "#c8d3df", fontSize: "10px", lineHeight: "1.35", overflowWrap: "anywhere" }}>
                <div>{
                  selectedDetails.kind === "default"
                    ? t("PROFILE_DETAIL_DEFAULT", "Open a game to save its profile")
                    : selectedDetails.kind === "game"
                      ? t("PROFILE_DETAIL_GAME", "Saved game")
                      : t("PROFILE_DETAIL_PROCESS", "Saved process")
                }</div>
                {selectedDetails.steam_app_id && (
                  <div>{t("PROFILE_STEAM_APP_ID", "Steam app ID: {app_id}", { app_id: selectedDetails.steam_app_id })}</div>
                )}
                {selectedDetails.kind !== "default" && (
                  <div>{
                    selectedDetails.processes.length
                      ? t("PROFILE_PROCESSES", "Processes: {processes}", { processes: selectedDetails.processes.join(", ") })
                      : t("PROFILE_PROCESSES_EMPTY", "Processes: enter one in Matched Processes below")
                  }</div>
                )}
              </div>
            </PanelSectionRow>
          )}

          <PanelSectionRow>
            <Focusable
              style={{
                display: "flex",
                flexDirection: "column",
                alignItems: "stretch",
                gap: "6px",
                width: "100%",
                marginTop: "6px",
                marginBottom: "2px"
              }}
              flow-children="vertical"
              noFocusRing
            >
              <DialogButton
                style={{
                  width: "100%",
                  minWidth: 0,
                  height: "34px",
                  display: "flex",
                  alignItems: "center",
                  justifyContent: "center",
                  gap: "6px",
                  padding: "4px 8px",
                  fontSize: "12px",
                  color: "#fff8ed",
                  background: "linear-gradient(135deg, #9d4a00 0%, #d97116 55%, #f7a743 100%)",
                  border: "1px solid rgba(255, 212, 154, 0.9)",
                  borderRadius: "4px",
                  outline: focusedAction === "edit" ? "2px solid #fff" : "none",
                  outlineOffset: "1px"
                }}
                onClick={showRenameProfile}
                onGamepadFocus={() => setFocusedAction("edit")}
                onGamepadBlur={() => setFocusedAction(null)}
                disabled={isLoading || selectedProfile === "mako" || !!mainRunningApp}
              >
                <RiEditLine size={16} />
                <span>{t("PROFILE_RENAME_BTN", "Rename")}</span>
              </DialogButton>
              <DialogButton
                style={{
                  width: "100%",
                  minWidth: 0,
                  height: "34px",
                  display: "flex",
                  alignItems: "center",
                  justifyContent: "center",
                  gap: "6px",
                  padding: "4px 8px",
                  fontSize: "12px",
                  color: "#fff5f5",
                  background: "linear-gradient(135deg, #8d1f2d 0%, #c43a47 55%, #ed6b63 100%)",
                  border: "1px solid rgba(255, 183, 178, 0.9)",
                  borderRadius: "4px",
                  outline: focusedAction === "delete" ? "2px solid #fff" : "none",
                  outlineOffset: "1px"
                }}
                onClick={showDeleteProfile}
                onGamepadFocus={() => setFocusedAction("delete")}
                onGamepadBlur={() => setFocusedAction(null)}
                disabled={isLoading || selectedProfile === "mako" || !!mainRunningApp}
              >
                <RiDeleteBinLine size={16} />
                <span>{t("PROFILE_DELETE_BTN", "Delete")}</span>
              </DialogButton>
            </Focusable>
          </PanelSectionRow>
        </>
      )}
    </>
  );
}
