import { currentRelease } from "virtual:mako-release-info";
import type {
  CustomShaderEffect,
  ProfileDetails,
  RemotePlayResult,
} from "../api/makoApi";
import { localDevelopmentBuildInfo } from "../config/devBuildInfo.generated";
import { getDefaults, type ConfigurationData } from "../config/configSchema";
import {
  EMPTY_RUNTIME_SCALING_UI_STATE,
  type RuntimeScalingUiState,
} from "../utils/runtimeScalingUtils";

/**
 * What the panel last showed with MAKO Renderer installed, so reopening it
 * starts from that layout instead of an empty first render that jumps once the
 * backend answers. The saved profile list and Remote Play status are shared;
 * the editing session, its
 * settings and live status are kept for each of the most recent running apps
 * (or none), so moving between games and Steam's menus restores each one.
 *
 * The snapshot is only ever displayed. The panel still runs every backend read
 * and their results replace it; installation actions wait for the real check,
 * settings edits wait for the real profile load and automatic model fallbacks
 * wait for both. It belongs to one plugin build, is dropped when Decky unloads
 * the plugin (update, reinstall, removal or reload) and around every Renderer
 * install, update and removal, and a check that started before such a change
 * can no longer write one.
 */

const STORAGE_KEY = "mako-panel-snapshot";
const SCHEMA = 2;
const REMEMBERED_APPS = 8;

export interface InstallationSnapshot {
  engineUpdateRequired: boolean;
  installedEngineVersion: string | null;
  expectedEngineVersion: string | null;
}

export interface SessionSnapshot {
  profile: string;
  // The running app at the time, which must still be running to restore it.
  appId: string | null;
  gameRunning: boolean;
  remotePlayRunning: boolean;
}

export interface ConfigSnapshot {
  profile: string;
  powerMode: string;
  separatePowerModes: boolean;
  powerSource: string;
  config: ConfigurationData;
  vkBasaltConfigPath: string;
  customShaderEffects: CustomShaderEffect[];
}

export interface RuntimeSnapshot {
  profile: string;
  state: RuntimeScalingUiState;
}

export interface ProfilesSnapshot {
  profiles: string[];
  details: ProfileDetails[];
}

export interface PanelSnapshot {
  installation: InstallationSnapshot;
  profiles: ProfilesSnapshot | null;
  remotePlay: RemotePlayResult | null;
  session: SessionSnapshot | null;
  config: ConfigSnapshot | null;
  runtime: RuntimeSnapshot | null;
}

/**
 * A part left undefined keeps what was remembered; null forgets it. Settings
 * and live status belong to the session's app, so they are only recorded
 * with a session.
 */
export interface PanelView {
  installation: InstallationSnapshot;
  profiles?: ProfilesSnapshot | null;
  remotePlay?: RemotePlayResult | null;
  session?: SessionSnapshot | null;
  config?: ConfigSnapshot | null;
  runtime?: RuntimeSnapshot | null;
}

interface AppSnapshot {
  session: SessionSnapshot;
  config: ConfigSnapshot | null;
  runtime: RuntimeSnapshot | null;
}

interface StoredPanel {
  installation: InstallationSnapshot;
  profiles: ProfilesSnapshot | null;
  remotePlay: RemotePlayResult | null;
  // Most recent first.
  apps: AppSnapshot[];
}

let remembered: StoredPanel | null | undefined;
let generation = 0;
// What storage holds when known, so unchanged views are not written again.
let written: string | null | undefined;

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);
const isText = (value: unknown): value is string => typeof value === "string";
const isName = (value: unknown): value is string =>
  isText(value) && value.length > 0;
const textOrNull = (value: unknown) => (isText(value) ? value : null);

function buildIdentity() {
  return JSON.stringify([
    currentRelease.version,
    localDevelopmentBuildInfo?.generatedAt ?? null,
  ]);
}

function readInstallation(value: unknown): InstallationSnapshot | null {
  if (!isRecord(value) || typeof value.engineUpdateRequired !== "boolean")
    return null;
  return {
    engineUpdateRequired: value.engineUpdateRequired,
    installedEngineVersion: textOrNull(value.installedEngineVersion),
    expectedEngineVersion: textOrNull(value.expectedEngineVersion),
  };
}

function readSession(value: unknown): SessionSnapshot | null {
  if (
    !isRecord(value) ||
    !isName(value.profile) ||
    !(value.appId === null || isName(value.appId)) ||
    typeof value.gameRunning !== "boolean" ||
    typeof value.remotePlayRunning !== "boolean"
  )
    return null;
  return {
    profile: value.profile,
    appId: value.appId,
    gameRunning: value.gameRunning,
    remotePlayRunning: value.remotePlayRunning,
  };
}

function isShaderEffect(value: unknown): value is CustomShaderEffect {
  return (
    isRecord(value) &&
    isText(value.id) &&
    isText(value.name) &&
    isText(value.path)
  );
}

function readConfig(value: unknown): ConfigSnapshot | null {
  if (
    !isRecord(value) ||
    !isName(value.profile) ||
    !isName(value.powerMode) ||
    typeof value.separatePowerModes !== "boolean" ||
    !isText(value.powerSource) ||
    !isRecord(value.config) ||
    !isText(value.vkBasaltConfigPath) ||
    !Array.isArray(value.customShaderEffects) ||
    !value.customShaderEffects.every(isShaderEffect)
  )
    return null;
  return {
    profile: value.profile,
    powerMode: value.powerMode,
    separatePowerModes: value.separatePowerModes,
    powerSource: value.powerSource,
    config: {
      ...getDefaults(),
      ...(value.config as Partial<ConfigurationData>),
    },
    vkBasaltConfigPath: value.vkBasaltConfigPath,
    customShaderEffects: value.customShaderEffects,
  };
}

function isProfileDetails(value: unknown): value is ProfileDetails {
  return (
    isRecord(value) &&
    isName(value.profile_name) &&
    isText(value.display_name) &&
    isText(value.kind) &&
    (value.steam_app_id === null || isText(value.steam_app_id)) &&
    Array.isArray(value.processes) &&
    value.processes.every(isText)
  );
}

function readProfiles(value: unknown): ProfilesSnapshot | null {
  if (
    !isRecord(value) ||
    !Array.isArray(value.profiles) ||
    !value.profiles.length ||
    !value.profiles.every(isName) ||
    !Array.isArray(value.details) ||
    !value.details.every(isProfileDetails)
  )
    return null;
  return { profiles: value.profiles, details: value.details };
}

function readRemotePlay(value: unknown): RemotePlayResult | null {
  if (
    !isRecord(value) ||
    !isText(value.message) ||
    !(value.error === null || isText(value.error)) ||
    !isText(value.profile_name) ||
    !Array.isArray(value.pids) ||
    !value.pids.every((pid) => typeof pid === "number") ||
    ![
      value.success,
      value.installed,
      value.managed,
      value.available,
      value.running,
      value.frame_generation_active,
      value.conflict,
    ].every((flag) => typeof flag === "boolean")
  )
    return null;
  return value as unknown as RemotePlayResult;
}

function readApp(value: unknown): AppSnapshot | null {
  const session = isRecord(value) ? readSession(value.session) : null;
  if (!isRecord(value) || !session) return null;
  return {
    session,
    config: readConfig(value.config),
    runtime: readRuntime(value.runtime),
  };
}

function readRuntime(value: unknown): RuntimeSnapshot | null {
  if (!isRecord(value) || !isName(value.profile) || !isRecord(value.state))
    return null;
  return {
    profile: value.profile,
    state: {
      ...EMPTY_RUNTIME_SCALING_UI_STATE,
      ...(value.state as Partial<RuntimeScalingUiState>),
    },
  };
}

function load(): StoredPanel | null {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return null;
    const record: unknown = JSON.parse(raw);
    const installation = isRecord(record)
      ? readInstallation(record.installation)
      : null;
    if (
      !isRecord(record) ||
      record.schema !== SCHEMA ||
      record.build !== buildIdentity() ||
      !installation
    ) {
      localStorage.removeItem(STORAGE_KEY);
      return null;
    }
    const apps = Array.isArray(record.apps)
      ? record.apps
          .map(readApp)
          .filter((app): app is AppSnapshot => app !== null)
      : [];
    return {
      installation,
      profiles: readProfiles(record.profiles),
      remotePlay: readRemotePlay(record.remotePlay),
      apps: apps.filter(
        (app, index) =>
          apps.findIndex(
            (other) => other.session.appId === app.session.appId,
          ) === index,
      ),
    };
  } catch {
    return null;
  }
}

function rememberedPanel() {
  if (remembered === undefined) remembered = load();
  return remembered;
}

/**
 * The parts of the remembered panel that still describe the current state:
 * the session last shown with the running app (or with none), and its
 * settings and live status only for the profile it was editing.
 */
export function restorePanel(appId: string | null): PanelSnapshot | null {
  const stored = rememberedPanel();
  if (!stored) return null;
  const app = stored.apps.find((entry) => entry.session.appId === appId);
  const session = app?.session ?? null;
  return {
    installation: stored.installation,
    profiles: stored.profiles,
    remotePlay: stored.remotePlay,
    session,
    config:
      session && app?.config?.profile === session.profile ? app.config : null,
    runtime:
      session &&
      (session.gameRunning || session.remotePlayRunning) &&
      app?.runtime?.profile === session.profile
        ? app.runtime
        : null,
  };
}

/** Identifies the remembered state that a starting backend check may update. */
export function panelGeneration() {
  return generation;
}

/**
 * Remembers what an installed panel shows, or forgets it when `view` is null.
 * A view from a check that started before the last forgetPanel() is ignored.
 */
export function rememberPanel(view: PanelView | null, viewGeneration: number) {
  if (viewGeneration !== generation) return;
  const previous = rememberedPanel();
  let snapshot: StoredPanel | null = null;
  if (view) {
    let apps = previous?.apps ?? [];
    const session = view.session;
    if (session === null) apps = [];
    if (session) {
      const kept = apps.find((app) => app.session.appId === session.appId);
      const app: AppSnapshot = {
        session,
        config:
          view.config === undefined ? (kept?.config ?? null) : view.config,
        runtime:
          view.runtime === undefined ? (kept?.runtime ?? null) : view.runtime,
      };
      apps = [app, ...apps.filter((entry) => entry !== kept)].slice(
        0,
        REMEMBERED_APPS,
      );
    }
    snapshot = {
      installation: view.installation,
      profiles:
        view.profiles === undefined
          ? (previous?.profiles ?? null)
          : view.profiles,
      remotePlay:
        view.remotePlay === undefined
          ? (previous?.remotePlay ?? null)
          : view.remotePlay,
      apps,
    };
  }
  remembered = snapshot;
  const serialized = snapshot
    ? JSON.stringify({ schema: SCHEMA, build: buildIdentity(), ...snapshot })
    : null;
  if (serialized === written) return;
  written = serialized;
  try {
    if (serialized) localStorage.setItem(STORAGE_KEY, serialized);
    else localStorage.removeItem(STORAGE_KEY);
  } catch {
    // Without storage the snapshot still lasts until Decky unloads the plugin.
  }
}

/** Drops the snapshot and every view from a check that started before now. */
export function forgetPanel() {
  generation += 1;
  remembered = null;
  written = null;
  try {
    localStorage.removeItem(STORAGE_KEY);
  } catch {
    // Nothing was stored.
  }
}

/** Clears the module state between tests; storage is cleared separately. */
export function resetPanelSnapshotForTests() {
  remembered = undefined;
  generation = 0;
  written = undefined;
}
