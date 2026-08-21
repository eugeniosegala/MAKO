import { callable } from "@decky/api";
import type {
  ConfigurationData,
  FlatpakRuntimeStatusField,
  FlatpakRuntimeVersion,
  ProfileKind,
} from "../config/configSchema";
export {
  DEFAULT_MAKO_WRAPPER_PATH,
  DEFAULT_STEAM_LAUNCH_OPTION,
} from "../config/runtimePaths";
export type Nullable<T> = T | null;

// Type definitions for API responses
export interface InstallationResult {
  success: boolean;
  error: Nullable<string>;
  message: string;
  removed_files?: Nullable<string[]>;
  flatpak_extensions_updated?: FlatpakRuntimeVersion[];
  flatpak_refresh_error?: string;
}

export interface InstallationStatus {
  installed: boolean;
  lib_exists: boolean;
  json_exists: boolean;
  script_exists: boolean;
  lib_path: string;
  json_path: string;
  script_path: string;
  installed_engine_version: Nullable<string>;
  expected_engine_version: Nullable<string>;
  engine_version_known: boolean;
  engine_update_required: boolean;
  host_architecture: Nullable<string>;
  host_architecture_supported: boolean;
  error: Nullable<string>;
}

export interface DllDetectionResult {
  detected: boolean;
  path: Nullable<string>;
  source: Nullable<string>;
  message: Nullable<string>;
  error: Nullable<string>;
}

export interface DllStatsResult {
  success: boolean;
  dll_path: Nullable<string>;
  dll_sha256: Nullable<string>;
  dll_source?: Nullable<string>;
  error: Nullable<string>;
}

// Use centralized configuration data type
export type MakoConfig = ConfigurationData;

export interface ConfigResult {
  success: boolean;
  config: Nullable<MakoConfig>;
  message: string;
  error: Nullable<string>;
}

export type ConfigUpdateResult = ConfigResult;

export interface ConfigSchemaResult {
  field_names: string[];
  field_types: Record<string, string>;
  defaults: ConfigurationData;
  profiles: string[];
  current_profile: string;
}

export interface LaunchOptionResult {
  launch_option: string;
  wrapper_path: string;
  instructions: string;
  explanation: string;
}

export interface FileContentResult {
  success: boolean;
  content?: Nullable<string>;
  path?: string;
  error?: Nullable<string>;
}

export interface FgmodCheckResult {
  success: boolean;
  exists: boolean;
  path?: string;
  error?: Nullable<string>;
}

// Flatpak management interfaces
export type FlatpakExtensionStatus = {
  success: boolean;
  message: string;
  error: Nullable<string>;
} & Record<FlatpakRuntimeStatusField, boolean>;

export interface FlatpakApp {
  app_id: string;
  app_name: string;
  wrapper_path: string;
  has_filesystem_override: boolean;
  has_wrapper_override: boolean;
  has_env_override: boolean;
  has_required_env_override: boolean;
}

export interface FlatpakAppInfo {
  success: boolean;
  message: string;
  error: Nullable<string>;
  apps: FlatpakApp[];
  total_apps: number;
}

export interface FlatpakOperationResult {
  success: boolean;
  message: string;
  error: Nullable<string>;
  app_id?: string;
  operation?: string;
}

// Profile management interfaces
export interface ProfileDetails {
  profile_name: string;
  display_name: string;
  kind: ProfileKind | string;
  steam_app_id: string | null;
  processes: string[];
}

export interface ProfilesResult {
  success: boolean;
  profiles: Nullable<string[]>;
  current_profile: Nullable<string>;
  profile_details: Nullable<ProfileDetails[]>;
  message: string;
  error: Nullable<string>;
}

export interface ProfileResult {
  success: boolean;
  profile_name?: Nullable<string>;
  current_profile?: Nullable<string>;
  profile?: Nullable<ProfileDetails>;
  changed?: Nullable<boolean>;
  game_running?: Nullable<boolean>;
  message: string;
  error: Nullable<string>;
}

export function configFailureResult(error: string): ConfigResult {
  return { success: false, config: null, message: "", error };
}

export function profilesFailureResult(error: string): ProfilesResult {
  return {
    success: false,
    profiles: null,
    current_profile: null,
    profile_details: null,
    message: "",
    error,
  };
}

export function profileFailureResult(
  error: string,
  fields: Partial<Omit<ProfileResult, "success" | "message" | "error">> = {},
): ProfileResult {
  return { success: false, message: "", error, ...fields };
}

// API functions
export const installMako = callable<[], InstallationResult>("install_mako");
export const uninstallMako = callable<[], InstallationResult>("uninstall_mako");
export const checkMakoInstalled = callable<[], InstallationStatus>(
  "check_mako_installed",
);
export const checkLosslessScalingDll = callable<[], DllDetectionResult>(
  "check_lossless_scaling_dll",
);
export const getDllStats = callable<[], DllStatsResult>("get_dll_stats");
export const getMakoConfig = callable<[], ConfigResult>("get_mako_config");
export const getProfileConfig = callable<[string], ConfigResult>(
  "get_profile_config",
);
export const getConfigSchema = callable<[], ConfigSchemaResult>(
  "get_config_schema",
);
export const getLaunchOption = callable<[], LaunchOptionResult>(
  "get_launch_option",
);
export const getConfigFileContent = callable<[], FileContentResult>(
  "get_config_file_content",
);
export const getLaunchScriptContent = callable<[], FileContentResult>(
  "get_launch_script_content",
);
export const checkFgmodDirectory = callable<[], FgmodCheckResult>(
  "check_fgmod_directory",
);

// Flatpak management API functions
export const checkFlatpakExtensionStatus = callable<[], FlatpakExtensionStatus>(
  "check_flatpak_extension_status",
);
export const installFlatpakExtension = callable<
  [FlatpakRuntimeVersion],
  FlatpakOperationResult
>("install_flatpak_extension");
export const uninstallFlatpakExtension = callable<
  [FlatpakRuntimeVersion],
  FlatpakOperationResult
>("uninstall_flatpak_extension");
export const getFlatpakApps = callable<[], FlatpakAppInfo>("get_flatpak_apps");
export const setFlatpakAppOverride = callable<[string], FlatpakOperationResult>(
  "set_flatpak_app_override",
);
export const removeFlatpakAppOverride = callable<
  [string],
  FlatpakOperationResult
>("remove_flatpak_app_override");

// Updated config function using object-based configuration (single source of truth)
export const updateMakoConfig = callable<
  [ConfigurationData],
  ConfigUpdateResult
>("update_mako_config");

// Object-based configuration helper
export const updateMakoConfigFromObject = async (
  config: ConfigurationData,
): Promise<ConfigUpdateResult> => {
  return updateMakoConfig(config);
};

// Self-updater API functions
// Profile management API functions
export const getProfiles = callable<[], ProfilesResult>("get_profiles");
export const createProfile = callable<[string, string?], ProfileResult>(
  "create_profile",
);
export const deleteProfile = callable<[string], ProfileResult>(
  "delete_profile",
);
export const renameProfile = callable<[string, string], ProfileResult>(
  "rename_profile",
);
export const captureGameProfile = callable<
  [string, string, string?],
  ProfileResult
>("capture_game_profile");
export const setCurrentProfile = callable<[string], ProfileResult>(
  "set_current_profile",
);
export const syncCurrentProfile = callable<[string], ProfileResult>(
  "sync_current_profile",
);
export const updateProfileConfig = callable<
  [string, ConfigurationData],
  ConfigUpdateResult
>("update_profile_config");
