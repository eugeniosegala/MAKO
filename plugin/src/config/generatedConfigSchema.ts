// src/config/generatedConfigSchema.ts
// Stable cross-language profile contract
export const DEFAULT_PROFILE_NAME = "mako" as const;
export const MAKO_WRAPPER_RELATIVE_PATH = ".local/bin/mako-run" as const;
export const PER_GAME_WRAPPER_FLATPAK_APP_IDS = [
  "com.heroicgameslauncher.hgl",
] as const;
export const PROFILE_KIND_DEFAULT = "default" as const;
export const PROFILE_KIND_GAME = "game" as const;
export const PROFILE_KIND_PROCESS = "process" as const;
export const PROFILE_KIND_MANUAL = "manual" as const;
export const PROFILE_KIND_VALUES = [
  PROFILE_KIND_DEFAULT,
  PROFILE_KIND_GAME,
  PROFILE_KIND_PROCESS,
  PROFILE_KIND_MANUAL,
] as const;
export type ProfileKind = (typeof PROFILE_KIND_VALUES)[number];

// Ordered Flatpak runtime contract generated from shared_config.py
export const SUPPORTED_FLATPAK_RUNTIMES = [
  { version: "23.08", statusField: "installed_23_08", i18nKey: "FLATPAK_RUNTIME_VERSION" },
  { version: "24.08", statusField: "installed_24_08", i18nKey: "FLATPAK_RUNTIME_VERSION" },
  { version: "25.08", statusField: "installed_25_08", i18nKey: "FLATPAK_RUNTIME_VERSION" },
] as const;
export type FlatpakRuntimeVersion =
  (typeof SUPPORTED_FLATPAK_RUNTIMES)[number]["version"];
export type FlatpakRuntimeStatusField =
  (typeof SUPPORTED_FLATPAK_RUNTIMES)[number]["statusField"];
export type FlatpakRuntimeI18nKey =
  (typeof SUPPORTED_FLATPAK_RUNTIMES)[number]["i18nKey"];

// Shared backend validation and Decky UI limits
export const BASE_FPS_CAP_MIN = 0 as const;
export const BASE_FPS_CAP_MAX = 240 as const;
export const BASE_FPS_CAP_UI_MAX = 120 as const;
export const TARGET_FPS_MIN = 30 as const;
export const TARGET_FPS_MAX = 240 as const;
export const ADAPTIVE_MAX_MULTIPLIER_MIN = 2 as const;
export const ADAPTIVE_MAX_MULTIPLIER_MAX = 4 as const;
export const ADAPTIVE_MINIMUM_BASE_FPS = 10 as const;
export const DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_MIN = 0.1 as const;
export const DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_MAX = 3 as const;
export const DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_VALUES = [
  0.1,
  0.2,
  0.25,
  0.5,
  0.75,
  1.0,
  1.5,
  2.0,
  3.0,
] as const;
export const FLOW_SCALE_MIN = 0.25 as const;
export const FLOW_SCALE_MAX = 1.0 as const;
export const SCALING_FACTOR_MIN = 1.0 as const;
export const SCALING_FACTOR_MAX = 2.0 as const;
export const SCALING_METHOD_NATIVE = "native" as const;
export const SCALING_METHOD_MAKO = "mako" as const;
export const SCALING_METHOD_LS1 = "ls1" as const;
export const SCALING_METHOD_LS1_PERFORMANCE = "ls1-performance" as const;
export const SCALING_METHOD_VALUES = [
  SCALING_METHOD_NATIVE,
  SCALING_METHOD_MAKO,
  SCALING_METHOD_LS1,
  SCALING_METHOD_LS1_PERFORMANCE,
] as const;
export type ScalingMethod = (typeof SCALING_METHOD_VALUES)[number];
export const SCALING_SHARPNESS_MIN = 0.0 as const;
export const SCALING_SHARPNESS_MAX = 1.0 as const;
export const ULTRA_PERFORMANCE_FLOW_SCALE = 0.75 as const;
export const FIXED_MULTIPLIER_MIN = 2 as const;
export const FIXED_MULTIPLIER_UI_MIN = 2 as const;
export const FIXED_MULTIPLIER_UI_MAX = 4 as const;
export const FRAME_GENERATION_REFRESH_THRESHOLD_MIN = 0 as const;
export const FRAME_GENERATION_REFRESH_THRESHOLD_MAX = 240 as const;
export const FRAME_GENERATION_REFRESH_THRESHOLD_UI_MIN = 30 as const;
export const FRAME_GENERATION_REFRESH_THRESHOLD_PRESET = 60 as const;

// Stable persisted values for the optional post-process Vulkan layer
export const EXTERNAL_VULKAN_LAYER_NONE = "" as const;
export const EXTERNAL_VULKAN_LAYER_MANGOHUD = "mangohud" as const;
export const EXTERNAL_VULKAN_LAYER_VKBASALT = "vkbasalt" as const;
export const EXTERNAL_VULKAN_LAYER_VALUES = [
  EXTERNAL_VULKAN_LAYER_NONE,
  EXTERNAL_VULKAN_LAYER_MANGOHUD,
  EXTERNAL_VULKAN_LAYER_VKBASALT,
] as const;
export type ExternalVulkanLayer =
  (typeof EXTERNAL_VULKAN_LAYER_VALUES)[number];

// Configuration field type enum - matches Python
export enum ConfigFieldType {
  BOOLEAN = "boolean",
  INTEGER = "integer",
  FLOAT = "float",
  STRING = "string"
}

// Field name constants for type-safe access
export const DLL = "dll" as const;
export const ALLOW_FP16 = "allow_fp16" as const;
export const SCALING_ENABLED = "scaling_enabled" as const;
export const SCALING_METHOD = "scaling_method" as const;
export const SCALING_FACTOR = "scaling_factor" as const;
export const SCALING_SHARPNESS = "scaling_sharpness" as const;
export const FRAME_GENERATION_ENABLED = "frame_generation_enabled" as const;
export const FRAME_GENERATION_REFRESH_THRESHOLD = "frame_generation_refresh_threshold" as const;
export const BASE_FPS_CAP = "base_fps_cap" as const;
export const MULTIPLIER = "multiplier" as const;
export const ADAPTIVE = "adaptive" as const;
export const ADAPTIVE_AUTO_BASE_FPS_CAP = "adaptive_auto_base_fps_cap" as const;
export const TARGET_FPS = "target_fps" as const;
export const ADAPTIVE_MAX_MULTIPLIER = "adaptive_max_multiplier" as const;
export const ADAPTIVE_STABLE_CADENCE = "adaptive_stable_cadence" as const;
export const DYNAMIC_CADENCE_RECOVERY = "dynamic_cadence_recovery" as const;
export const DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS = "dynamic_cadence_probe_interval_seconds" as const;
export const ULTRA_PERFORMANCE = "ultra_performance" as const;
export const FLOW_SCALE = "flow_scale" as const;
export const PERFORMANCE_MODE = "performance_mode" as const;
export const PACING = "pacing" as const;
export const ACTIVE_IN = "active_in" as const;
export const GPU = "gpu" as const;
export const DISABLE_MAKO = "disable_mako" as const;
export const DISABLE_HDR_EXPOSURE = "disable_hdr_exposure" as const;
export const GAMESCOPE_WSI_COMPATIBILITY = "gamescope_wsi_compatibility" as const;
export const EXTERNAL_VULKAN_LAYER = "external_vulkan_layer" as const;
export const DISABLE_STEAMDECK_MODE = "disable_steamdeck_mode" as const;
export const ENABLE_ZINK = "enable_zink" as const;
export const FORCE_ALSA_AUDIO = "force_alsa_audio" as const;

// Configuration field definition
export interface ConfigField {
  name: string;
  fieldType: ConfigFieldType;
  default: boolean | number | string;
  description: string;
}

// Configuration schema - auto-generated from Python
export const CONFIG_SCHEMA: Record<string, ConfigField> = {
  dll: {
    name: "dll",
    fieldType: ConfigFieldType.STRING,
    default: "",
    description: "optional full path to Lossless.dll; leave blank for automatic discovery"
  },
  allow_fp16: {
    name: "allow_fp16",
    fieldType: ConfigFieldType.BOOLEAN,
    default: true,
    description: "allow FP16 acceleration (disable on older NVIDIA GPUs)"
  },
  scaling_enabled: {
    name: "scaling_enabled",
    fieldType: ConfigFieldType.BOOLEAN,
    default: false,
    description: "restart-bound scaling engine switch that provisions the Gamescope WSI presentation path"
  },
  scaling_method: {
    name: "scaling_method",
    fieldType: ConfigFieldType.STRING,
    default: "mako",
    description: "live spatial selection: Native, MAKO, LS1 Quality, or LS1 Performance"
  },
  scaling_factor: {
    name: "scaling_factor",
    fieldType: ConfigFieldType.FLOAT,
    default: 1.5,
    description: "output scaling factor from 1.0x to 2.0x"
  },
  scaling_sharpness: {
    name: "scaling_sharpness",
    fieldType: ConfigFieldType.FLOAT,
    default: 0.5,
    description: "scaling sharpness from zero to one"
  },
  frame_generation_enabled: {
    name: "frame_generation_enabled",
    fieldType: ConfigFieldType.BOOLEAN,
    default: true,
    description: "on/off switch; leave on for fixed or adaptive generation, off stops both modes"
  },
  frame_generation_refresh_threshold: {
    name: "frame_generation_refresh_threshold",
    fieldType: ConfigFieldType.INTEGER,
    default: 0,
    description: "pause frame generation at or below a confirmed Gamescope refresh rate; zero disables the guard"
  },
  base_fps_cap: {
    name: "base_fps_cap",
    fieldType: ConfigFieldType.INTEGER,
    default: 0,
    description: "backend-independent real framerate cap applied before frame generation"
  },
  multiplier: {
    name: "multiplier",
    fieldType: ConfigFieldType.INTEGER,
    default: 2,
    description: "change the fps multiplier"
  },
  adaptive: {
    name: "adaptive",
    fieldType: ConfigFieldType.BOOLEAN,
    default: false,
    description: "dynamically vary generated frames to approach a target framerate"
  },
  adaptive_auto_base_fps_cap: {
    name: "adaptive_auto_base_fps_cap",
    fieldType: ConfigFieldType.BOOLEAN,
    default: true,
    description: "automatically cap real FPS at half the target for steadier 2x frame generation"
  },
  target_fps: {
    name: "target_fps",
    fieldType: ConfigFieldType.INTEGER,
    default: 90,
    description: "target displayed framerate for adaptive frame generation"
  },
  adaptive_max_multiplier: {
    name: "adaptive_max_multiplier",
    fieldType: ConfigFieldType.INTEGER,
    default: 3,
    description: "ceiling for generated frames in adaptive mode"
  },
  adaptive_stable_cadence: {
    name: "adaptive_stable_cadence",
    fieldType: ConfigFieldType.BOOLEAN,
    default: true,
    description: "prefer smoother constant interpolation; may lower real-frame cadence and increase input lag"
  },
  dynamic_cadence_recovery: {
    name: "dynamic_cadence_recovery",
    fieldType: ConfigFieldType.BOOLEAN,
    default: false,
    description: "mode-independent compatibility recovery for native frame-rate switches; Fixed follows confirmed refresh, enabling clears both base FPS caps, and choosing an incompatible preset or cap disables recovery"
  },
  dynamic_cadence_probe_interval_seconds: {
    name: "dynamic_cadence_probe_interval_seconds",
    fieldType: ConfigFieldType.FLOAT,
    default: 2.0,
    description: "seconds between Dynamic Cadence Recovery probes; shorter intervals react faster but can make brief pacing hitches more frequent"
  },
  ultra_performance: {
    name: "ultra_performance",
    fieldType: ConfigFieldType.BOOLEAN,
    default: false,
    description: "restart-bound preset that may improve frame-generation performance by up to 30% in favourable GPU-limited scenarios with 75% flow scale, the lighter FG model, FP16 when supported, and active-policy resource allocation; compatible controls remain available after startup"
  },
  flow_scale: {
    name: "flow_scale",
    fieldType: ConfigFieldType.FLOAT,
    default: 0.9,
    description: "change the flow scale through game-owned swapchain recreation"
  },
  performance_mode: {
    name: "performance_mode",
    fieldType: ConfigFieldType.BOOLEAN,
    default: false,
    description: "select a lighter FG model through game-owned swapchain recreation, reducing GPU overhead at the cost of more visual artifacts"
  },
  pacing: {
    name: "pacing",
    fieldType: ConfigFieldType.STRING,
    default: "none",
    description: "frame pacing mode (currently only 'none' supported)"
  },
  active_in: {
    name: "active_in",
    fieldType: ConfigFieldType.STRING,
    default: "",
    description: "optional executable or process names, separated by commas"
  },
  gpu: {
    name: "gpu",
    fieldType: ConfigFieldType.STRING,
    default: "",
    description: "optional GPU name, vendor:device ID, or PCI bus ID"
  },
  disable_mako: {
    name: "disable_mako",
    fieldType: ConfigFieldType.BOOLEAN,
    default: false,
    description: "troubleshooting: prevent MAKO Renderer loading on the next game launch"
  },
  disable_hdr_exposure: {
    name: "disable_hdr_exposure",
    fieldType: ConfigFieldType.BOOLEAN,
    default: true,
    description: "required SDR safety boundary while HDR is unavailable"
  },
  gamescope_wsi_compatibility: {
    name: "gamescope_wsi_compatibility",
    fieldType: ConfigFieldType.BOOLEAN,
    default: false,
    description: "enable the restart-bound Gamescope WSI compatibility layer independently of scaling"
  },
  external_vulkan_layer: {
    name: "external_vulkan_layer",
    fieldType: ConfigFieldType.STRING,
    default: "",
    description: "optional guarded post-process Vulkan layer: MangoHud or vkBasalt"
  },
  disable_steamdeck_mode: {
    name: "disable_steamdeck_mode",
    fieldType: ConfigFieldType.BOOLEAN,
    default: false,
    description: "disable Steam Deck mode (unlocks hidden settings in some games)"
  },
  enable_zink: {
    name: "enable_zink",
    fieldType: ConfigFieldType.BOOLEAN,
    default: false,
    description: "Enable Zink (Vulkan-based OpenGL implementation) for OpenGL games"
  },
  force_alsa_audio: {
    name: "force_alsa_audio",
    fieldType: ConfigFieldType.BOOLEAN,
    default: false,
    description: "may improve compatibility with modes such as Zink and reduce audio stuttering or sudden loud sounds; restart required"
  },
};

// Type-safe configuration data structure
export interface ConfigurationData {
  dll: string;
  allow_fp16: boolean;
  scaling_enabled: boolean;
  scaling_method: string;
  scaling_factor: number;
  scaling_sharpness: number;
  frame_generation_enabled: boolean;
  frame_generation_refresh_threshold: number;
  base_fps_cap: number;
  multiplier: number;
  adaptive: boolean;
  adaptive_auto_base_fps_cap: boolean;
  target_fps: number;
  adaptive_max_multiplier: number;
  adaptive_stable_cadence: boolean;
  dynamic_cadence_recovery: boolean;
  dynamic_cadence_probe_interval_seconds: number;
  ultra_performance: boolean;
  flow_scale: number;
  performance_mode: boolean;
  pacing: string;
  active_in: string;
  gpu: string;
  disable_mako: boolean;
  disable_hdr_exposure: boolean;
  gamescope_wsi_compatibility: boolean;
  external_vulkan_layer: string;
  disable_steamdeck_mode: boolean;
  enable_zink: boolean;
  force_alsa_audio: boolean;
}

// Validated partial profile update sent through Decky's RPC boundary
export type ConfigurationPatch = Partial<ConfigurationData>;

// Helper functions
export function getFieldNames(): string[] {
  return Object.keys(CONFIG_SCHEMA);
}

export function getDefaults(): ConfigurationData {
  return {
    dll: "",
    allow_fp16: true,
    scaling_enabled: false,
    scaling_method: "mako",
    scaling_factor: 1.5,
    scaling_sharpness: 0.5,
    frame_generation_enabled: true,
    frame_generation_refresh_threshold: 0,
    base_fps_cap: 0,
    multiplier: 2,
    adaptive: false,
    adaptive_auto_base_fps_cap: true,
    target_fps: 90,
    adaptive_max_multiplier: 3,
    adaptive_stable_cadence: true,
    dynamic_cadence_recovery: false,
    dynamic_cadence_probe_interval_seconds: 2.0,
    ultra_performance: false,
    flow_scale: 0.9,
    performance_mode: false,
    pacing: "none",
    active_in: "",
    gpu: "",
    disable_mako: false,
    disable_hdr_exposure: true,
    gamescope_wsi_compatibility: false,
    external_vulkan_layer: "",
    disable_steamdeck_mode: false,
    enable_zink: false,
    force_alsa_audio: false,
  };
}

export function getFieldTypes(): Record<string, ConfigFieldType> {
  return {
    dll: ConfigFieldType.STRING,
    allow_fp16: ConfigFieldType.BOOLEAN,
    scaling_enabled: ConfigFieldType.BOOLEAN,
    scaling_method: ConfigFieldType.STRING,
    scaling_factor: ConfigFieldType.FLOAT,
    scaling_sharpness: ConfigFieldType.FLOAT,
    frame_generation_enabled: ConfigFieldType.BOOLEAN,
    frame_generation_refresh_threshold: ConfigFieldType.INTEGER,
    base_fps_cap: ConfigFieldType.INTEGER,
    multiplier: ConfigFieldType.INTEGER,
    adaptive: ConfigFieldType.BOOLEAN,
    adaptive_auto_base_fps_cap: ConfigFieldType.BOOLEAN,
    target_fps: ConfigFieldType.INTEGER,
    adaptive_max_multiplier: ConfigFieldType.INTEGER,
    adaptive_stable_cadence: ConfigFieldType.BOOLEAN,
    dynamic_cadence_recovery: ConfigFieldType.BOOLEAN,
    dynamic_cadence_probe_interval_seconds: ConfigFieldType.FLOAT,
    ultra_performance: ConfigFieldType.BOOLEAN,
    flow_scale: ConfigFieldType.FLOAT,
    performance_mode: ConfigFieldType.BOOLEAN,
    pacing: ConfigFieldType.STRING,
    active_in: ConfigFieldType.STRING,
    gpu: ConfigFieldType.STRING,
    disable_mako: ConfigFieldType.BOOLEAN,
    disable_hdr_exposure: ConfigFieldType.BOOLEAN,
    gamescope_wsi_compatibility: ConfigFieldType.BOOLEAN,
    external_vulkan_layer: ConfigFieldType.STRING,
    disable_steamdeck_mode: ConfigFieldType.BOOLEAN,
    enable_zink: ConfigFieldType.BOOLEAN,
    force_alsa_audio: ConfigFieldType.BOOLEAN,
  };
}
