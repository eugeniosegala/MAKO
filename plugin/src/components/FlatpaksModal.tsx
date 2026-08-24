import { FC, useState, useEffect, CSSProperties } from "react";
import {
  ModalRoot,
  DialogBody,
  DialogHeader,
  DialogControlsSection,
  ButtonItem,
  PanelSectionRow,
  Toggle,
  Focusable,
  showModal,
  ConfirmModal,
} from "@decky/ui";
import { FaCheck, FaTimes, FaDownload, FaTrash, FaCog } from "react-icons/fa";
import {
  checkFlatpakExtensionStatus,
  installFlatpakExtension,
  uninstallFlatpakExtension,
  getFlatpakApps,
  getLaunchOption,
  setFlatpakAppOverride,
  removeFlatpakAppOverride,
  FlatpakExtensionStatus,
  FlatpakApp,
  FlatpakAppInfo,
} from "../api/makoApi";
import {
  PER_GAME_WRAPPER_FLATPAK_APP_IDS,
  SUPPORTED_FLATPAK_RUNTIMES,
  type FlatpakRuntimeVersion,
} from "../config/configSchema";
import { DEFAULT_MAKO_WRAPPER_PATH } from "../config/runtimePaths";
import t from "../i18n/i18n";
import { showErrorToast, showSuccessToast } from "../utils/toastUtils";
import {
  MakoCompactSpinner,
  makoPanelDivider,
  makoPanelItemStyle,
  makoPanelSectionHeaderStyle,
  makoPanelStyle,
} from "./MakoUi";

interface FlatpaksModalProps {
  closeModal?: () => void;
}

function translateFlatpakRuntime(version: FlatpakRuntimeVersion): string {
  return t("FLATPAK_RUNTIME_VERSION", "Runtime {version}", { version });
}

export const FlatpaksModal: FC<FlatpaksModalProps> = ({ closeModal }) => {
  const [extensionStatus, setExtensionStatus] =
    useState<FlatpakExtensionStatus | null>(null);
  const [flatpakApps, setFlatpakApps] = useState<FlatpakAppInfo | null>(null);
  const [loading, setLoading] = useState(true);
  const [operationInProgress, setOperationInProgress] = useState<string | null>(
    null,
  );
  const [appErrors, setAppErrors] = useState<Record<string, string>>({});
  const [wrapperPath, setWrapperPath] = useState(DEFAULT_MAKO_WRAPPER_PATH);

  const loadData = async () => {
    setLoading(true);
    try {
      const [statusResult, appsResult, launchOptionResult] = await Promise.all([
        checkFlatpakExtensionStatus(),
        getFlatpakApps(),
        getLaunchOption().catch(() => null),
      ]);

      setExtensionStatus(statusResult);
      setFlatpakApps(appsResult);
      if (launchOptionResult?.wrapper_path) {
        setWrapperPath(launchOptionResult.wrapper_path);
      }
    } catch (error) {
      console.error("Error loading Flatpak data:", error);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadData();
  }, []);

  const handleExtensionOperation = async (
    operation: "install" | "uninstall",
    version: FlatpakRuntimeVersion,
  ) => {
    const operationId = `${operation}-${version}`;
    setOperationInProgress(operationId);

    try {
      const result =
        operation === "install"
          ? await installFlatpakExtension(version)
          : await uninstallFlatpakExtension(version);

      if (result.success) {
        // Reload status after operation
        const newStatus = await checkFlatpakExtensionStatus();
        setExtensionStatus(newStatus);
        showSuccessToast(
          t("FLATPAK_EXTENSION_UPDATED", "Flatpak extension updated"),
          result.message ||
            `${version} ${t("FLATPAK_RUNTIME_EXTENSION_UPDATED", "runtime extension updated")}`,
        );
      } else {
        const action =
          operation === "install"
            ? t("FLATPAK_INSTALL_ACTION", "install")
            : t("FLATPAK_UNINSTALL_ACTION", "uninstall");
        showErrorToast(
          t("FLATPAK_EXTENSION_FAILED", "Flatpak extension failed"),
          result.error ||
            result.message ||
            `${t("FLATPAK_EXTENSION_ACTION_FAILED", "Could not")} ${action} ${version} ${t("FLATPAK_RUNTIME_EXTENSION", "runtime extension")}`,
        );
      }
    } catch (error) {
      console.error(`Error ${operation}ing extension:`, error);
      showErrorToast(
        t("FLATPAK_EXTENSION_FAILED", "Flatpak extension failed"),
        String(error),
      );
    } finally {
      setOperationInProgress(null);
    }
  };

  const handleAppOverrideToggle = async (app: FlatpakApp) => {
    const hasOverrides =
      app.has_filesystem_override &&
      app.has_wrapper_override &&
      app.has_required_env_override !== false;
    const operationId = `app-${app.app_id}`;
    setOperationInProgress(operationId);
    setAppErrors((current) => {
      const next = { ...current };
      delete next[app.app_id];
      return next;
    });

    try {
      const result = hasOverrides
        ? await removeFlatpakAppOverride(app.app_id)
        : await setFlatpakAppOverride(app.app_id);

      if (result.success) {
        // Reload apps data after operation
        const newApps = await getFlatpakApps();
        setFlatpakApps(newApps);
        showSuccessToast(
          t("FLATPAK_APPLICATION_UPDATED", "Flatpak application updated"),
          result.message ||
            `${app.app_name || app.app_id} ${t("FLATPAK_UPDATED", "updated")}`,
        );
      } else {
        setAppErrors((current) => ({
          ...current,
          [app.app_id]:
            result.error ||
            result.message ||
            `${t("FLATPAK_APPLICATION_ACTION_FAILED", "Could not update")} ${app.app_name || app.app_id}`,
        }));
      }
    } catch (error) {
      console.error("Error toggling app override:", error);
      setAppErrors((current) => ({ ...current, [app.app_id]: String(error) }));
    } finally {
      setOperationInProgress(null);
    }
  };

  const confirmOperation = (
    operation: () => void,
    title: string,
    description: string,
  ) => {
    showModal(
      <ConfirmModal
        strTitle={title}
        strDescription={description}
        onOK={operation}
        onCancel={() => {}}
      />,
    );
  };

  const handleRuntimePrimaryAction = (
    version: FlatpakRuntimeVersion,
    installed: boolean,
  ) => {
    const operation: "install" | "uninstall" = installed
      ? "uninstall"
      : "install";
    const action = () => handleExtensionOperation(operation, version);

    if (operation === "uninstall") {
      confirmOperation(
        action,
        t("FLATPAK_UNINSTALL_TITLE", "Uninstall Runtime Extension"),
        `${t("FLATPAK_UNINSTALL_CONFIRM_PREFIX", "Are you sure you want to uninstall the")} ${version} ${t("FLATPAK_UNINSTALL_CONFIRM_SUFFIX", "runtime extension?")}`,
      );
      return;
    }

    action();
  };

  if (loading) {
    return (
      <ModalRoot closeModal={closeModal}>
        <DialogHeader>
          {t("FLATPAK_MODAL_TITLE", "Flatpak Extensions")}
        </DialogHeader>
        <DialogBody>
          <div
            style={{
              display: "flex",
              justifyContent: "center",
              padding: "20px",
            }}
          >
            <MakoCompactSpinner size={28} />
          </div>
        </DialogBody>
      </ModalRoot>
    );
  }

  const instructionSteps = [
    {
      id: "try-first",
      title: t("FLATPAK_STEP_WRAPPER_PATH", "Wrapper path for this device:"),
      command: wrapperPath,
    },
    {
      id: "final-result",
      title: t("FLATPAK_STEP_FINAL", "Final result should look like:"),
      command: `${wrapperPath} "usr/bin/flatpak"`,
    },
  ];

  const focusableInstructionStyle: CSSProperties = {
    padding: "10px",
    background: "rgba(0, 0, 0, 0.3)",
    borderRadius: "6px",
    marginBottom: "12px",
  };

  const commandStyle: CSSProperties = {
    fontFamily: "monospace",
    fontSize: "0.85em",
    background: "rgba(0, 0, 0, 0.45)",
    padding: "8px",
    borderRadius: "4px",
    marginTop: "6px",
    overflowWrap: "anywhere",
  };

  return (
    <ModalRoot closeModal={closeModal}>
      <DialogHeader>
        {t("FLATPAK_MODAL_TITLE", "Flatpak Extensions")}
      </DialogHeader>
      <DialogBody>
        <Focusable flow-children="vertical">
          <div
            style={{
              ...makoPanelStyle,
              margin: "8px 0 18px",
            }}
          >
            <div style={makoPanelSectionHeaderStyle}>
              {t("FLATPAK_RUNTIME_INSTALLER", "Runtime Extension Installer")}
            </div>

            {extensionStatus && extensionStatus.success ? (
              SUPPORTED_FLATPAK_RUNTIMES.map((runtime) => {
                const installBusy =
                  operationInProgress === `install-${runtime.version}`;
                const uninstallBusy =
                  operationInProgress === `uninstall-${runtime.version}`;
                const isBusy = installBusy || uninstallBusy;
                const installed = extensionStatus[runtime.statusField];

                return (
                  <div key={runtime.version} style={makoPanelItemStyle}>
                    <div
                      style={{
                        display: "flex",
                        alignItems: "center",
                        gap: "10px",
                      }}
                    >
                      {installed ? (
                        <FaCheck
                          style={{ color: "#65b9c9", flex: "0 0 16px" }}
                        />
                      ) : (
                        <FaTimes
                          style={{ color: "#c89558", flex: "0 0 16px" }}
                        />
                      )}
                      <div style={{ minWidth: 0 }}>
                        <div style={{ color: "#edf8fb", fontWeight: 600 }}>
                            {translateFlatpakRuntime(runtime.version)}
                        </div>
                        <div
                          style={{
                            marginTop: "2px",
                            color: "#b9cbd0",
                            fontSize: "12px",
                          }}
                        >
                          {installed
                            ? t("FLATPAK_INSTALLED", "Installed")
                            : t("FLATPAK_NOT_INSTALLED", "Not installed")}
                        </div>
                      </div>
                    </div>
                    <div
                      style={{ display: "flex", gap: "8px", marginTop: "10px" }}
                    >
                      {installed && (
                        <div
                          className="Mako_BrandButton"
                          style={{ flex: 1, minWidth: 0 }}
                        >
                          <ButtonItem
                            layout="below"
                            onClick={() =>
                              handleExtensionOperation(
                                "install",
                                runtime.version,
                              )
                            }
                            disabled={isBusy}
                          >
                            {installBusy ? (
                              <>
                                <MakoCompactSpinner />{" "}
                                {t("FLATPAK_UPDATING_BTN", "Updating...")}
                              </>
                            ) : (
                              <>
                                <FaDownload />{" "}
                                {t("FLATPAK_UPDATE_BTN", "Update")}
                              </>
                            )}
                          </ButtonItem>
                        </div>
                      )}
                      <div
                        className={`Mako_BrandButton${installed ? " Mako_BrandButton--danger" : ""}`}
                        style={{ flex: 1, minWidth: 0 }}
                      >
                        <ButtonItem
                          layout="below"
                          onClick={() =>
                            handleRuntimePrimaryAction(
                              runtime.version,
                              installed,
                            )
                          }
                          disabled={isBusy}
                        >
                          {uninstallBusy ? (
                            <>
                              <MakoCompactSpinner />{" "}
                              {t("FLATPAK_UNINSTALLING_BTN", "Uninstalling...")}
                            </>
                          ) : installBusy && !installed ? (
                            <>
                              <MakoCompactSpinner />{" "}
                              {t("FLATPAK_INSTALLING_BTN", "Installing...")}
                            </>
                          ) : installed ? (
                            <>
                              <FaTrash />{" "}
                              {t("FLATPAK_UNINSTALL_BTN", "Uninstall")}
                            </>
                          ) : (
                            <>
                              <FaDownload />{" "}
                              {t("FLATPAK_INSTALL_BTN", "Install")}
                            </>
                          )}
                        </ButtonItem>
                      </div>
                    </div>
                  </div>
                );
              })
            ) : (
              <div style={makoPanelItemStyle}>
                <div
                  style={{
                    display: "flex",
                    alignItems: "flex-start",
                    gap: "10px",
                  }}
                >
                  <FaTimes
                    style={{
                      color: "#c89558",
                      flex: "0 0 16px",
                      marginTop: "2px",
                    }}
                  />
                  <div>
                    <div style={{ color: "#edf8fb", fontWeight: 600 }}>
                      {t("FLATPAK_ERROR", "Error")}
                    </div>
                    <div
                      style={{
                        marginTop: "3px",
                        color: "#b9cbd0",
                        fontSize: "12px",
                        overflowWrap: "anywhere",
                      }}
                    >
                      {extensionStatus?.error ||
                        t(
                          "FLATPAK_ERROR_STATUS",
                          "Failed to check extension status",
                        )}
                    </div>
                  </div>
                </div>
              </div>
            )}

            <div
              style={{
                ...makoPanelSectionHeaderStyle,
                borderTop: makoPanelDivider,
              }}
            >
              {t("FLATPAK_APPS_TITLE", "Flatpak Applications")}
            </div>
            <div
              style={{
                ...makoPanelItemStyle,
                color: "#b9cbd0",
                fontSize: "12px",
                lineHeight: 1.4,
              }}
            >
              <div
                style={{
                  color: "#edf8fb",
                  fontSize: "13px",
                  fontWeight: 600,
                  marginBottom: "4px",
                }}
              >
                {t("FLATPAK_PREPARE_APPLICATION", "Prepare an application")}
              </div>
              {t(
                "FLATPAK_PREPARE_APPLICATION_DESC",
                "Install its matching runtime extension, then prepare only that app here. For Heroic, use the full Wrapper command path shown below in each game you want to enable. Preparing Heroic does not enable frame generation globally.",
              )}
            </div>

            {flatpakApps && flatpakApps.success ? (
              flatpakApps.apps.length > 0 ? (
                flatpakApps.apps.map((app) => {
                  const hasOverrides =
                    app.has_filesystem_override &&
                    app.has_wrapper_override &&
                    app.has_required_env_override !== false;
                  const partialOverrides =
                    app.has_filesystem_override ||
                    app.has_wrapper_override ||
                    app.has_env_override;
                  const appBusy = operationInProgress === `app-${app.app_id}`;

                  let statusColor = "#c89558";
                  let statusText = t(
                    "FLATPAK_STATUS_NO_OVERRIDES",
                    "No overrides",
                  );

                  if (hasOverrides) {
                    statusColor = "#65b9c9";
                    statusText = t("FLATPAK_STATUS_CONFIGURED", "Prepared");
                  } else if (partialOverrides) {
                    statusColor = "#d58a39";
                    statusText = t("FLATPAK_STATUS_PARTIAL", "Partial");
                  }

                  const appError = appErrors[app.app_id];
                  const description =
                    PER_GAME_WRAPPER_FLATPAK_APP_IDS.some(
                      (appId) => appId === app.app_id,
                    )
                      ? t(
                          "FLATPAK_HEROIC_APP_DESC",
                          "{app_id} - {status}. Per game: Settings > Advanced > enter this in Heroic's first Wrapper field: {wrapper_path}; leave Arguments empty.",
                          {
                            app_id: app.app_id,
                            status: statusText,
                            wrapper_path: app.wrapper_path,
                          },
                        )
                      : `${app.app_id} - ${statusText}`;

                  return (
                    <div
                      key={app.app_id}
                      style={{
                        ...makoPanelItemStyle,
                        display: "flex",
                        alignItems: "center",
                        gap: "10px",
                      }}
                    >
                      <FaCog
                        style={{
                          color: appError ? "#d96d79" : statusColor,
                          flex: "0 0 16px",
                        }}
                      />
                      <div style={{ flex: 1, minWidth: 0 }}>
                        <div
                          style={{
                            color: "#edf8fb",
                            fontWeight: 600,
                            overflowWrap: "anywhere",
                          }}
                        >
                          {app.app_name || app.app_id}
                        </div>
                        <div
                          style={{
                            marginTop: "3px",
                            color: "#b9cbd0",
                            fontSize: "12px",
                            lineHeight: 1.35,
                            overflowWrap: "anywhere",
                          }}
                        >
                          {description}
                        </div>
                        {appError && (
                          <div
                            style={{
                              marginTop: "5px",
                              color: "#ff9b9b",
                              fontSize: "12px",
                              lineHeight: 1.35,
                              overflowWrap: "anywhere",
                            }}
                          >
                            {appError}
                          </div>
                        )}
                      </div>
                      <div
                        aria-busy={appBusy}
                        style={{
                          flex: "0 0 auto",
                          position: "relative",
                        }}
                      >
                        <Toggle
                          value={hasOverrides}
                          onChange={() => {
                            if (!appBusy) void handleAppOverrideToggle(app);
                          }}
                        />
                        {appBusy && (
                          <div
                            role="status"
                            style={{
                              position: "absolute",
                              inset: 0,
                              display: "flex",
                              alignItems: "center",
                              justifyContent: "center",
                              pointerEvents: "none",
                              background: "rgba(8, 39, 56, 0.78)",
                            }}
                          >
                            <MakoCompactSpinner />
                          </div>
                        )}
                      </div>
                    </div>
                  );
                })
              ) : (
                <div style={makoPanelItemStyle}>
                  <div style={{ color: "#edf8fb", fontWeight: 600 }}>
                    {t("FLATPAK_NO_APPS", "No Flatpak Apps Found")}
                  </div>
                  <div
                    style={{
                      marginTop: "3px",
                      color: "#b9cbd0",
                      fontSize: "12px",
                    }}
                  >
                    {t(
                      "FLATPAK_NO_APPS_DESC",
                      "No Flatpak applications are currently installed",
                    )}
                  </div>
                </div>
              )
            ) : (
              <div style={makoPanelItemStyle}>
                <div style={{ color: "#edf8fb", fontWeight: 600 }}>
                  {t("FLATPAK_ERROR", "Error")}
                </div>
                <div
                  style={{
                    marginTop: "3px",
                    color: "#b9cbd0",
                    fontSize: "12px",
                    overflowWrap: "anywhere",
                  }}
                >
                  {flatpakApps?.error ||
                    t(
                      "FLATPAK_ERROR_APPS",
                      "Failed to load Flatpak applications",
                    )}
                </div>
              </div>
            )}

            <div
              style={{
                ...makoPanelSectionHeaderStyle,
                borderTop: makoPanelDivider,
              }}
            >
              {t(
                "FLATPAK_STEAM_CONFIG_TITLE",
                "Optional Steam Flatpak shortcuts",
              )}
            </div>
            <div
              style={{
                ...makoPanelItemStyle,
                display: "flex",
                flexDirection: "column",
              }}
            >
              <div
                style={{
                  fontWeight: "bold",
                  marginBottom: "8px",
                  color: "#fff",
                }}
              >
                {t(
                  "FLATPAK_STEAM_CONFIG_HEADER",
                  "Configure Steam Flatpak Shortcuts",
                )}
              </div>
              <div
                style={{
                  fontSize: "0.9em",
                  lineHeight: "1.4",
                  marginBottom: "8px",
                }}
              >
                {t(
                  "FLATPAK_STEAM_CONFIG_DESC",
                  "Only use these target instructions for a Flatpak shortcut inside Steam. Heroic users should prepare Heroic above, then set the MAKO wrapper in the chosen game's Advanced settings.",
                )}
              </div>
              <div
                style={{
                  fontSize: "0.9em",
                  lineHeight: "1.4",
                  marginBottom: "12px",
                  color: "#ffa500",
                }}
              >
                <strong>{t("FLATPAK_IMPORTANT_LABEL", "IMPORTANT:")}</strong>{" "}
                {t(
                  "FLATPAK_STEAM_CONFIG_IMPORTANT",
                  "Set this in TARGET (NOT LAUNCH OPTIONS)",
                )}
              </div>

              {instructionSteps.map((step) => (
                <Focusable
                  key={step.id}
                  focusWithinClassName="gpfocuswithin"
                  onActivate={() => {}}
                  style={focusableInstructionStyle}
                >
                  <div style={{ fontWeight: "bold" }}>{step.title}</div>
                  <div style={commandStyle}>{step.command}</div>
                </Focusable>
              ))}
            </div>
          </div>

          {/* Close Button */}
          <DialogControlsSection>
            <PanelSectionRow>
              <div className="Mako_BrandButton">
                <ButtonItem layout="below" onClick={closeModal}>
                  {t("FLATPAK_CLOSE", "Close")}
                </ButtonItem>
              </div>
            </PanelSectionRow>
          </DialogControlsSection>
        </Focusable>
      </DialogBody>
    </ModalRoot>
  );
};
