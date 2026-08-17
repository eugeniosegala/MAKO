import { FC, useState, useEffect, CSSProperties } from 'react';
import {
  ModalRoot,
  DialogBody,
  DialogHeader,
  DialogControlsSection,
  DialogControlsSectionHeader,
  ButtonItem,
  PanelSectionRow,
  Field,
  Toggle,
  Spinner,
  Focusable,
  showModal,
  ConfirmModal
} from '@decky/ui';
import { FaCheck, FaTimes, FaDownload, FaTrash, FaCog } from 'react-icons/fa';
import {
  checkFlatpakExtensionStatus,
  installFlatpakExtension,
  uninstallFlatpakExtension,
  getFlatpakApps,
  setFlatpakAppOverride,
  removeFlatpakAppOverride,
  FlatpakExtensionStatus,
  FlatpakApp,
  FlatpakAppInfo
} from '../api/makoApi';
import t from '../i18n/i18n';
import { showErrorToast, showSuccessToast } from '../utils/toastUtils';

interface FlatpaksModalProps {
  closeModal?: () => void;
}

export const FlatpaksModal: FC<FlatpaksModalProps> = ({ closeModal }) => {
  const [extensionStatus, setExtensionStatus] = useState<FlatpakExtensionStatus | null>(null);
  const [flatpakApps, setFlatpakApps] = useState<FlatpakAppInfo | null>(null);
  const [loading, setLoading] = useState(true);
  const [operationInProgress, setOperationInProgress] = useState<string | null>(null);
  const [appErrors, setAppErrors] = useState<Record<string, string>>({});

  const loadData = async () => {
    setLoading(true);
    try {
      const [statusResult, appsResult] = await Promise.all([
        checkFlatpakExtensionStatus(),
        getFlatpakApps()
      ]);

      setExtensionStatus(statusResult);
      setFlatpakApps(appsResult);
    } catch (error) {
      console.error('Error loading Flatpak data:', error);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadData();
  }, []);

  const handleExtensionOperation = async (operation: 'install' | 'uninstall', version: string) => {
    const operationId = `${operation}-${version}`;
    setOperationInProgress(operationId);

    try {
      const result = operation === 'install'
        ? await installFlatpakExtension(version)
        : await uninstallFlatpakExtension(version);

      if (result.success) {
        // Reload status after operation
        const newStatus = await checkFlatpakExtensionStatus();
        setExtensionStatus(newStatus);
        showSuccessToast(t('FLATPAK_EXTENSION_UPDATED', 'Flatpak extension updated'), result.message || `${version} ${t('FLATPAK_RUNTIME_EXTENSION_UPDATED', 'runtime extension updated')}`);
      } else {
        const action = operation === 'install'
          ? t('FLATPAK_INSTALL_ACTION', 'install')
          : t('FLATPAK_UNINSTALL_ACTION', 'uninstall');
        showErrorToast(t('FLATPAK_EXTENSION_FAILED', 'Flatpak extension failed'), result.error || result.message || `${t('FLATPAK_EXTENSION_ACTION_FAILED', 'Could not')} ${action} ${version} ${t('FLATPAK_RUNTIME_EXTENSION', 'runtime extension')}`);
      }
    } catch (error) {
      console.error(`Error ${operation}ing extension:`, error);
      showErrorToast(t('FLATPAK_EXTENSION_FAILED', 'Flatpak extension failed'), String(error));
    } finally {
      setOperationInProgress(null);
    }
  };

  const handleAppOverrideToggle = async (app: FlatpakApp) => {
    const hasOverrides = app.has_filesystem_override
      && app.has_wrapper_override
      && app.has_required_env_override !== false;
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
        showSuccessToast(t('FLATPAK_APPLICATION_UPDATED', 'Flatpak application updated'), result.message || `${app.app_name || app.app_id} ${t('FLATPAK_UPDATED', 'updated')}`);
      } else {
        setAppErrors((current) => ({
          ...current,
          [app.app_id]: result.error || result.message || `${t('FLATPAK_APPLICATION_ACTION_FAILED', 'Could not update')} ${app.app_name || app.app_id}`
        }));
      }
    } catch (error) {
      console.error('Error toggling app override:', error);
      setAppErrors((current) => ({ ...current, [app.app_id]: String(error) }));
    } finally {
      setOperationInProgress(null);
    }
  };

  const confirmOperation = (operation: () => void, title: string, description: string) => {
    showModal(
      <ConfirmModal
        strTitle={title}
        strDescription={description}
        onOK={operation}
        onCancel={() => {}}
      />
    );
  };

  const handleRuntimePrimaryAction = (version: string, installed: boolean) => {
    const operation: 'install' | 'uninstall' = installed ? 'uninstall' : 'install';
    const action = () => handleExtensionOperation(operation, version);

    if (operation === 'uninstall') {
      confirmOperation(
        action,
        t('FLATPAK_UNINSTALL_TITLE', 'Uninstall Runtime Extension'),
        `${t('FLATPAK_UNINSTALL_CONFIRM_PREFIX', 'Are you sure you want to uninstall the')} ${version} ${t('FLATPAK_UNINSTALL_CONFIRM_SUFFIX', 'runtime extension?')}`
      );
      return;
    }

    action();
  };

  if (loading) {
    return (
      <ModalRoot closeModal={closeModal}>
        <DialogHeader>{t('FLATPAK_MODAL_TITLE', 'Flatpak Extensions')}</DialogHeader>
        <DialogBody>
          <div style={{ display: 'flex', justifyContent: 'center', padding: '20px' }}>
            <Spinner />
          </div>
        </DialogBody>
      </ModalRoot>
    );
  }

  const instructionSteps = [
    {
      id: 'try-first',
      title: t('FLATPAK_STEP_TRY_FIRST', 'Try first:'),
      command: '~/.local/bin/mako-run'
    },
    {
      id: 'try-full-path',
      title: t('FLATPAK_STEP_TRY_FULL_PATH', "If that doesn't work, try full path:"),
      command: '/home/(username)/.local/bin/mako-run'
    },
    {
      id: 'final-result',
      title: t('FLATPAK_STEP_FINAL', 'Final result should look like:'),
      command: '~/.local/bin/mako-run "usr/bin/flatpak"'
    }
  ];

  const focusableInstructionStyle: CSSProperties = {
    padding: '10px',
    background: 'rgba(0, 0, 0, 0.3)',
    borderRadius: '6px',
    marginBottom: '12px'
  };

  const commandStyle: CSSProperties = {
    fontFamily: 'monospace',
    fontSize: '0.85em',
    background: 'rgba(0, 0, 0, 0.45)',
    padding: '8px',
    borderRadius: '4px',
    marginTop: '6px'
  };

  return (
    <ModalRoot closeModal={closeModal}>
      <DialogHeader>{t('FLATPAK_MODAL_TITLE', 'Flatpak Extensions')}</DialogHeader>
      <DialogBody>
        <Focusable flow-children="vertical">
          {/* Extension Status Section */}
          <DialogControlsSection>
            <DialogControlsSectionHeader>{t('FLATPAK_RUNTIME_INSTALLER', 'Runtime Extension Installer')}</DialogControlsSectionHeader>

            {extensionStatus && extensionStatus.success ? (
              <>
                {[
                  { version: '23.08', label: t('FLATPAK_RUNTIME_23', 'Runtime 23.08'), installed: extensionStatus.installed_23_08 },
                  { version: '24.08', label: t('FLATPAK_RUNTIME_24', 'Runtime 24.08'), installed: extensionStatus.installed_24_08 },
                  { version: '25.08', label: t('FLATPAK_RUNTIME_25', 'Runtime 25.08'), installed: extensionStatus.installed_25_08 }
                ].map((runtime) => {
                  const isBusy = operationInProgress === `install-${runtime.version}` || operationInProgress === `uninstall-${runtime.version}`;

                  return (
                    <div key={runtime.version}>
                      <PanelSectionRow>
                        <Field
                          label={runtime.label}
                          description={runtime.installed ? t('FLATPAK_INSTALLED', 'Installed') : t('FLATPAK_NOT_INSTALLED', 'Not installed')}
                          icon={runtime.installed ? <FaCheck style={{ color: 'green' }} /> : <FaTimes style={{ color: 'red' }} />}
                        />
                      </PanelSectionRow>
                      <PanelSectionRow>
                        <ButtonItem
                          layout="below"
                          onClick={() => handleRuntimePrimaryAction(runtime.version, runtime.installed)}
                          disabled={isBusy}
                        >
                          {isBusy ? <Spinner /> : runtime.installed ? <><FaTrash /> {t('FLATPAK_UNINSTALL_BTN', 'Uninstall')}</> : <><FaDownload /> {t('FLATPAK_INSTALL_BTN', 'Install')}</>}
                        </ButtonItem>
                      </PanelSectionRow>
                      {runtime.installed && (
                        <PanelSectionRow>
                          <ButtonItem
                            layout="below"
                            onClick={() => handleExtensionOperation('install', runtime.version)}
                            disabled={isBusy}
                          >
                            {operationInProgress === `install-${runtime.version}` ? <Spinner /> : <><FaDownload /> {t('FLATPAK_UPDATE_BTN', 'Update')}</>}
                          </ButtonItem>
                        </PanelSectionRow>
                      )}
                    </div>
                  );
                })}
              </>
            ) : (
              <PanelSectionRow>
                <Field
                  label={t('FLATPAK_ERROR', 'Error')}
                  description={extensionStatus?.error || t('FLATPAK_ERROR_STATUS', 'Failed to check extension status')}
                  icon={<FaTimes style={{color: 'red'}} />}
                />
              </PanelSectionRow>
            )}
          </DialogControlsSection>

          {/* Flatpak Apps Section */}
          <DialogControlsSection>
            <DialogControlsSectionHeader>{t('FLATPAK_APPS_TITLE', 'Flatpak Applications')}</DialogControlsSectionHeader>
            <PanelSectionRow>
              <Field
                label={t('FLATPAK_PREPARE_APPLICATION', 'Prepare an application')}
                description={t('FLATPAK_PREPARE_APPLICATION_DESC', "Install its matching runtime extension, then prepare only that app here. For Heroic, use the full Wrapper command path shown below in each game you want to enable. Preparing Heroic does not enable frame generation globally.")}
              />
            </PanelSectionRow>

            {flatpakApps && flatpakApps.success ? (
              flatpakApps.apps.length > 0 ? (
                flatpakApps.apps.map((app) => {
                  const hasOverrides = app.has_filesystem_override
                    && app.has_wrapper_override
                    && app.has_required_env_override !== false;
                  const partialOverrides = app.has_filesystem_override || app.has_wrapper_override || app.has_env_override;

                  let statusColor = 'red';
                  let statusText = t('FLATPAK_STATUS_NO_OVERRIDES', 'No overrides');

                  if (hasOverrides) {
                    statusColor = 'green';
                    statusText = t('FLATPAK_STATUS_CONFIGURED', 'Prepared');
                  } else if (partialOverrides) {
                    statusColor = 'orange';
                    statusText = t('FLATPAK_STATUS_PARTIAL', 'Partial');
                  }

                  const appError = appErrors[app.app_id];

                  return (
                    <PanelSectionRow key={app.app_id}>
                      <Field
                        label={app.app_name || app.app_id}
                        description={app.app_id === 'com.heroicgameslauncher.hgl'
                          ? t(
                            'FLATPAK_HEROIC_APP_DESC',
                            '{app_id} - {status}. Per game: Settings > Advanced > enter this in Heroic\'s first Wrapper field: {wrapper_path}; leave Arguments empty.',
                            { app_id: app.app_id, status: statusText, wrapper_path: app.wrapper_path }
                          )
                          : `${app.app_id} - ${statusText}`}
                        icon={<FaCog style={{color: appError ? '#f44336' : statusColor}} />}
                      >
                        <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'flex-end', gap: '8px', maxWidth: '100%' }}>
                          {appError && (
                            <div
                              style={{
                                color: '#ff9b9b',
                                fontSize: '0.82em',
                                lineHeight: '1.35',
                                maxWidth: '260px',
                                overflowWrap: 'anywhere',
                                textAlign: 'left'
                              }}
                            >
                              {appError}
                            </div>
                          )}
                          <Toggle
                            value={hasOverrides}
                            onChange={() => handleAppOverrideToggle(app)}
                            disabled={operationInProgress === `app-${app.app_id}`}
                          />
                        </div>
                      </Field>
                    </PanelSectionRow>
                  );
                })
              ) : (
                <PanelSectionRow>
                  <Field
                  label={t('FLATPAK_NO_APPS', 'No Flatpak Apps Found')}
                  description={t('FLATPAK_NO_APPS_DESC', 'No Flatpak applications are currently installed')}
                  />
                </PanelSectionRow>
              )
            ) : (
              <PanelSectionRow>
                <Field
                  label={t('FLATPAK_ERROR', 'Error')}
                  description={flatpakApps?.error || t('FLATPAK_ERROR_APPS', 'Failed to load Flatpak applications')}
                  icon={<FaTimes style={{color: 'red'}} />}
                />
              </PanelSectionRow>
            )}
          </DialogControlsSection>

          {/* Steam Configuration Instructions */}
          <DialogControlsSection>
            <DialogControlsSectionHeader>{t('FLATPAK_STEAM_CONFIG_TITLE', 'Optional Steam Flatpak shortcuts')}</DialogControlsSectionHeader>
            <div
              style={{
                padding: '12px',
                background: 'rgba(255, 255, 255, 0.1)',
                borderRadius: '8px',
                margin: '8px 0',
                display: 'flex',
                flexDirection: 'column'
              }}
            >
              <div style={{ fontWeight: 'bold', marginBottom: '8px', color: '#fff' }}>
                {t('FLATPAK_STEAM_CONFIG_HEADER', 'Configure Steam Flatpak Shortcuts')}
              </div>
              <div style={{ fontSize: '0.9em', lineHeight: '1.4', marginBottom: '8px' }}>
                {t('FLATPAK_STEAM_CONFIG_DESC', "Only use these target instructions for a Flatpak shortcut inside Steam. Heroic users should prepare Heroic above, then set the existing experimental wrapper in the chosen game's Advanced settings.")}
              </div>
              <div style={{ fontSize: '0.9em', lineHeight: '1.4', marginBottom: '12px', color: '#ffa500' }}>
                <strong>{t('FLATPAK_IMPORTANT_LABEL', 'IMPORTANT:')}</strong> {t('FLATPAK_STEAM_CONFIG_IMPORTANT', 'Set this in TARGET (NOT LAUNCH OPTIONS)')}
              </div>

              {instructionSteps.map((step) => (
                <Focusable
                  key={step.id}
                  focusWithinClassName="gpfocuswithin"
                  onActivate={() => {}}
                  style={focusableInstructionStyle}
                >
                  <div style={{ fontWeight: 'bold' }}>{step.title}</div>
                  <div style={commandStyle}>{step.command}</div>
                </Focusable>
              ))}

            </div>
          </DialogControlsSection>

          {/* Close Button */}
          <DialogControlsSection>
            <PanelSectionRow>
              <ButtonItem
                layout="below"
                onClick={closeModal}
              >
                {t('FLATPAK_CLOSE', 'Close')}
              </ButtonItem>
            </PanelSectionRow>
          </DialogControlsSection>
        </Focusable>
      </DialogBody>
    </ModalRoot>
  );
};
