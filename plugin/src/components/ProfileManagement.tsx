import { useState, type CSSProperties } from "react";
import {
  type AppOverview,
  ButtonItem,
  ConfirmModal,
  DialogButton,
  Dropdown,
  Field,
  ModalRoot,
  PanelSectionRow,
  TextField,
  showModal,
} from "@decky/ui";
import {
  RiArrowDownSFill,
  RiArrowUpSFill,
  RiDeleteBinLine,
  RiEditLine,
} from "react-icons/ri";
import t from "../i18n/i18n";
import {
  DEFAULT_PROFILE_NAME,
  PROFILE_KIND_DEFAULT,
  PROFILE_KIND_GAME,
} from "../config/configSchema";
import { useProfileEditorModel } from "../hooks/useProfileEditorModel";
import { usePersistentCollapseState } from "../hooks/usePersistentCollapseState";
import {
  MakoFocusable,
  MakoSectionHeader,
  makoDialogButtonStyle,
} from "./MakoUi";

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
  closeModal,
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
        <Field
          label={t("PROFILE_NAME_LABEL", "Name")}
          childrenLayout="below"
          childrenContainerWidth="max"
        >
          <TextField
            value={value}
            onChange={(event) => setValue(event?.target?.value || "")}
            style={{ width: "100%" }}
          />
        </Field>
        <MakoFocusable
          style={{
            display: "flex",
            justifyContent: "flex-end",
            gap: "8px",
            marginTop: "24px",
          }}
          flow-children="row"
        >
          <DialogButton onClick={closeModal}>{cancelText}</DialogButton>
          <DialogButton onClick={handleOK} disabled={!value.trim()}>
            {okText}
          </DialogButton>
        </MakoFocusable>
      </div>
    </ModalRoot>
  );
}

interface ProfileManagementProps {
  editingProfile?: string;
  onProfileChange?: (profileName: string) => void | Promise<void>;
  mainRunningApp?: AppOverview;
  topMargin?: CSSProperties["marginTop"];
}

export function ProfileManagement({
  editingProfile,
  onProfileChange,
  mainRunningApp,
  topMargin,
}: ProfileManagementProps) {
  const [focusedAction, setFocusedAction] = useState<"edit" | "delete" | null>(
    null,
  );
  const [profilesCollapsed, setProfilesCollapsed] = usePersistentCollapseState(
    PROFILES_COLLAPSED_KEY,
    false,
    "profiles",
  );
  const {
    selectedProfile,
    selectedDetails,
    runningProfile,
    profileOptions,
    isLoading,
    switchProfile,
    saveRunningGame,
    renameSelectedProfile,
    deleteSelectedProfile,
  } = useProfileEditorModel({
    editingProfile,
    onProfileChange,
    mainRunningApp,
  });

  const showRenameProfile = () => {
    if (selectedProfile === DEFAULT_PROFILE_NAME) return;
    showModal(
      <TextInputModal
        title={t("PROFILE_RENAME_TITLE", "Rename Profile")}
        description={t(
          "PROFILE_RENAME_DESC_PREFIX",
          "Choose a friendly name for this game or process profile.",
        )}
        defaultValue={selectedDetails?.display_name || selectedProfile}
        okText={t("PROFILE_RENAME_BTN", "Rename")}
        cancelText={t("PROFILE_CANCEL_BTN", "Cancel")}
        onOK={(name) => void renameSelectedProfile(name)}
      />,
    );
  };

  const showDeleteProfile = () => {
    if (selectedProfile === DEFAULT_PROFILE_NAME) return;
    showModal(
      <ConfirmModal
        strTitle={t("PROFILE_DELETE_TITLE", "Delete Game / Process Profile")}
        strDescription={t(
          "PROFILE_DELETE_CONFIRM",
          'Delete "{profile}" and all of its saved settings?',
          { profile: selectedDetails?.display_name || selectedProfile },
        )}
        strOKButtonText={t("PROFILE_DELETE_BTN", "Delete")}
        strCancelButtonText={t("PROFILE_CANCEL_BTN", "Cancel")}
        onOK={() => void deleteSelectedProfile()}
      />,
    );
  };

  return (
    <>
      <style>{`
        .Mako_ProfilesCollapseButton_Container > div > div > div > button,
        .Mako_ProfilesCollapseButton_Container > div > div > div > div > button {
          height: 10px !important;
        }
      `}</style>

      <MakoSectionHeader topMargin={topMargin}>
        {t("PROFILE_SECTION_TITLE", "Game / Process Profiles")}
      </MakoSectionHeader>

      <PanelSectionRow>
        <div
          style={{
            fontSize: "11px",
            lineHeight: "1.35",
            color: "#b8c5d6",
            marginBottom: "4px",
          }}
        >
          {t(
            "PROFILE_HELP",
            "Start a game and save its process once. MAKO selects saved profiles automatically; outside a game, the dropdown only chooses which profile to edit.",
          )}
        </div>
      </PanelSectionRow>

      {mainRunningApp && !runningProfile && (
        <PanelSectionRow>
          <div className="Mako_BrandButton">
            <ButtonItem
              layout="below"
              onClick={() => void saveRunningGame()}
              disabled={isLoading}
            >
              {t("PROFILE_SAVE_RUNNING", "Save profile for {game}", {
                game: mainRunningApp.display_name,
              })}
            </ButtonItem>
          </div>
        </PanelSectionRow>
      )}

      <PanelSectionRow>
        <div
          className="Mako_ProfilesCollapseButton_Container"
          style={{ marginTop: "2px", marginBottom: "4px" }}
        >
          <ButtonItem
            layout="below"
            bottomSeparator="none"
            onClick={() => setProfilesCollapsed(!profilesCollapsed)}
          >
            {profilesCollapsed ? (
              <RiArrowDownSFill
                style={{ transform: "translate(0, -13px)", fontSize: "1.5em" }}
              />
            ) : (
              <RiArrowUpSFill
                style={{ transform: "translate(0, -12px)", fontSize: "1.5em" }}
              />
            )}
          </ButtonItem>
        </div>
      </PanelSectionRow>

      {!profilesCollapsed && (
        <>
          <PanelSectionRow>
            <Field
              label={t("PROFILE_SAVED_LABEL", "Saved profile")}
              childrenLayout="below"
              childrenContainerWidth="max"
              bottomSeparator="none"
            >
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
              <div
                style={{
                  width: "100%",
                  padding: "6px 8px",
                  boxSizing: "border-box",
                  borderRadius: "4px",
                  background: "rgba(255,255,255,0.06)",
                  color: "#c8d3df",
                  fontSize: "10px",
                  lineHeight: "1.35",
                  overflowWrap: "anywhere",
                }}
              >
                <div>
                  {selectedDetails.kind === PROFILE_KIND_DEFAULT
                    ? t(
                        "PROFILE_DETAIL_DEFAULT",
                        "Open a game to save its profile",
                      )
                    : selectedDetails.kind === PROFILE_KIND_GAME
                      ? t("PROFILE_DETAIL_GAME", "Saved game")
                      : t("PROFILE_DETAIL_PROCESS", "Saved process")}
                </div>
                {selectedDetails.steam_app_id && (
                  <div>
                    {t("PROFILE_STEAM_APP_ID", "Steam app ID: {app_id}", {
                      app_id: selectedDetails.steam_app_id,
                    })}
                  </div>
                )}
                {selectedDetails.kind !== PROFILE_KIND_DEFAULT && (
                  <div>
                    {selectedDetails.processes.length
                      ? t("PROFILE_PROCESSES", "Processes: {processes}", {
                          processes: selectedDetails.processes.join(", "),
                        })
                      : t(
                          "PROFILE_PROCESSES_EMPTY",
                          "Processes: enter one in Matched Processes below",
                        )}
                  </div>
                )}
                {mainRunningApp && (
                  <div style={{ marginTop: "4px", color: "#d9b98c" }}>
                    {t(
                      "PROFILE_MANAGE_WHEN_IDLE",
                      "Close the running game to rename or delete profiles.",
                    )}
                  </div>
                )}
              </div>
            </PanelSectionRow>
          )}

          <PanelSectionRow>
            <MakoFocusable
              style={{
                display: "flex",
                flexDirection: "column",
                alignItems: "stretch",
                gap: "6px",
                width: "100%",
                marginTop: "6px",
              }}
              flow-children="column"
              noFocusRing
            >
              <DialogButton
                className="Mako_DialogButton"
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
                  ...makoDialogButtonStyle(focusedAction === "edit"),
                }}
                onClick={showRenameProfile}
                onGamepadFocus={() => setFocusedAction("edit")}
                onGamepadBlur={() => setFocusedAction(null)}
                disabled={
                  isLoading ||
                  selectedProfile === DEFAULT_PROFILE_NAME ||
                  !!mainRunningApp
                }
              >
                <RiEditLine size={16} />
                <span>{t("PROFILE_RENAME_BTN", "Rename")}</span>
              </DialogButton>
              <DialogButton
                className="Mako_DialogButton Mako_DialogButton--danger"
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
                  ...makoDialogButtonStyle(
                    focusedAction === "delete",
                    "danger",
                  ),
                }}
                onClick={showDeleteProfile}
                onGamepadFocus={() => setFocusedAction("delete")}
                onGamepadBlur={() => setFocusedAction(null)}
                disabled={
                  isLoading ||
                  selectedProfile === DEFAULT_PROFILE_NAME ||
                  !!mainRunningApp
                }
              >
                <RiDeleteBinLine size={16} />
                <span>{t("PROFILE_DELETE_BTN", "Delete")}</span>
              </DialogButton>
            </MakoFocusable>
          </PanelSectionRow>
        </>
      )}
    </>
  );
}
