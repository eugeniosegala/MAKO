// src/config/generatedConfigSchema.ts
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
export const FRAME_GENERATION_ENABLED = "frame_generation_enabled" as const;
export const BASE_FPS_CAP = "base_fps_cap" as const;
export const MULTIPLIER = "multiplier" as const;
export const ADAPTIVE = "adaptive" as const;
export const ADAPTIVE_AUTO_BASE_FPS_CAP = "adaptive_auto_base_fps_cap" as const;
export const TARGET_FPS = "target_fps" as const;
export const ADAPTIVE_MAX_MULTIPLIER = "adaptive_max_multiplier" as const;
export const ADAPTIVE_STABLE_CADENCE = "adaptive_stable_cadence" as const;
export const FLOW_SCALE = "flow_scale" as const;
export const PERFORMANCE_MODE = "performance_mode" as const;
export const PACING = "pacing" as const;
export const ACTIVE_IN = "active_in" as const;
export const GPU = "gpu" as const;
export const DISABLE_MAKO = "disable_mako" as const;
export const DISABLE_HDR_EXPOSURE = "disable_hdr_exposure" as const;
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
  frame_generation_enabled: {
    name: "frame_generation_enabled",
    fieldType: ConfigFieldType.BOOLEAN,
    default: true,
    description: "live on/off switch; leave on for fixed or adaptive generation, off stops both modes"
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
    default: false,
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
  flow_scale: {
    name: "flow_scale",
    fieldType: ConfigFieldType.FLOAT,
    default: 0.9,
    description: "change the flow scale"
  },
  performance_mode: {
    name: "performance_mode",
    fieldType: ConfigFieldType.BOOLEAN,
    default: false,
    description: "use a lighter FG model to reduce GPU overhead, at the cost of more visual artifacts"
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
    description: "troubleshooting: prevent MAKO Renderer loading after restart"
  },
  disable_hdr_exposure: {
    name: "disable_hdr_exposure",
    fieldType: ConfigFieldType.BOOLEAN,
    default: true,
    description: "required SDR safety boundary while HDR is unavailable"
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
    description: "force ALSA for this game after restart; disable to restore Steam and Proton audio defaults"
  },
};

// Type-safe configuration data structure
export interface ConfigurationData {
  dll: string;
  allow_fp16: boolean;
  frame_generation_enabled: boolean;
  base_fps_cap: number;
  multiplier: number;
  adaptive: boolean;
  adaptive_auto_base_fps_cap: boolean;
  target_fps: number;
  adaptive_max_multiplier: number;
  adaptive_stable_cadence: boolean;
  flow_scale: number;
  performance_mode: boolean;
  pacing: string;
  active_in: string;
  gpu: string;
  disable_mako: boolean;
  disable_hdr_exposure: boolean;
  disable_steamdeck_mode: boolean;
  enable_zink: boolean;
  force_alsa_audio: boolean;
}

// Helper functions
export function getFieldNames(): string[] {
  return Object.keys(CONFIG_SCHEMA);
}

export function getDefaults(): ConfigurationData {
  return {
    dll: "",
    allow_fp16: true,
    frame_generation_enabled: true,
    base_fps_cap: 0,
    multiplier: 2,
    adaptive: false,
    adaptive_auto_base_fps_cap: false,
    target_fps: 90,
    adaptive_max_multiplier: 3,
    adaptive_stable_cadence: true,
    flow_scale: 0.9,
    performance_mode: false,
    pacing: "none",
    active_in: "",
    gpu: "",
    disable_mako: false,
    disable_hdr_exposure: true,
    disable_steamdeck_mode: false,
    enable_zink: false,
    force_alsa_audio: false,
  };
}

export function getFieldTypes(): Record<string, ConfigFieldType> {
  return {
    dll: ConfigFieldType.STRING,
    allow_fp16: ConfigFieldType.BOOLEAN,
    frame_generation_enabled: ConfigFieldType.BOOLEAN,
    base_fps_cap: ConfigFieldType.INTEGER,
    multiplier: ConfigFieldType.INTEGER,
    adaptive: ConfigFieldType.BOOLEAN,
    adaptive_auto_base_fps_cap: ConfigFieldType.BOOLEAN,
    target_fps: ConfigFieldType.INTEGER,
    adaptive_max_multiplier: ConfigFieldType.INTEGER,
    adaptive_stable_cadence: ConfigFieldType.BOOLEAN,
    flow_scale: ConfigFieldType.FLOAT,
    performance_mode: ConfigFieldType.BOOLEAN,
    pacing: ConfigFieldType.STRING,
    active_in: ConfigFieldType.STRING,
    gpu: ConfigFieldType.STRING,
    disable_mako: ConfigFieldType.BOOLEAN,
    disable_hdr_exposure: ConfigFieldType.BOOLEAN,
    disable_steamdeck_mode: ConfigFieldType.BOOLEAN,
    enable_zink: ConfigFieldType.BOOLEAN,
    force_alsa_audio: ConfigFieldType.BOOLEAN,
  };
}
