"""
Shared configuration schema constants.

This file contains the canonical configuration schema that should be used
by both Python and TypeScript code. Any changes to the configuration
structure should be made here first.
"""

from typing import Dict, Literal, TypedDict, Union
from enum import Enum


# Stable built-in profile identifier shared by the backend and generated
# frontend contract. This is persisted on disk and must not be renamed without
# a migration.
DEFAULT_PROFILE_NAME = "mako"
# Stable install-relative launcher path. Backends resolve it against Decky's
# actual user home; the frontend uses it only for its pre-RPC fallback text.
MAKO_WRAPPER_RELATIVE_PATH = ".local/bin/mako-run"
# Persisted profile categories shared by metadata writers and frontend RPC UX.
PROFILE_KIND_DEFAULT = "default"
PROFILE_KIND_GAME = "game"
PROFILE_KIND_PROCESS = "process"
PROFILE_KIND_MANUAL = "manual"
PROFILE_KIND_VALUES = (
    PROFILE_KIND_DEFAULT,
    PROFILE_KIND_GAME,
    PROFILE_KIND_PROCESS,
    PROFILE_KIND_MANUAL,
)
# Ordered release matrix used by backend bundles and generated frontend status.
SUPPORTED_FLATPAK_RUNTIME_VERSIONS = ("23.08", "24.08", "25.08")
# Flatpak frontends whose games start in a child compatibility environment and
# therefore require MAKO's wrapper to be configured per game rather than on the
# launcher process itself.
PER_GAME_WRAPPER_FLATPAK_APP_IDS = ("com.heroicgameslauncher.hgl",)

# Cross-language validation limits. Keep the deliberately narrower Decky UI
# ceiling separate from the canonical profile validation range.
BASE_FPS_CAP_MIN = 0
BASE_FPS_CAP_MAX = 240
BASE_FPS_CAP_UI_MAX = 60
TARGET_FPS_MIN = 30
TARGET_FPS_MAX = 240
ADAPTIVE_MAX_MULTIPLIER_MIN = 2
ADAPTIVE_MAX_MULTIPLIER_MAX = 4
ADAPTIVE_MINIMUM_BASE_FPS = 10
FLOW_SCALE_MIN = 0.25
FLOW_SCALE_MAX = 1.0
FIXED_MULTIPLIER_MIN = 2
FIXED_MULTIPLIER_UI_MIN = FIXED_MULTIPLIER_MIN
FIXED_MULTIPLIER_UI_MAX = 4

# Stable persisted values for the mutually exclusive optional Vulkan layer.
EXTERNAL_VULKAN_LAYER_NONE = ""
EXTERNAL_VULKAN_LAYER_GAMESCOPE_WSI = "gamescope-wsi"
EXTERNAL_VULKAN_LAYER_MANGOHUD = "mangohud"
EXTERNAL_VULKAN_LAYER_VKBASALT = "vkbasalt"
EXTERNAL_VULKAN_LAYER_VALUES = (
    EXTERNAL_VULKAN_LAYER_NONE,
    EXTERNAL_VULKAN_LAYER_GAMESCOPE_WSI,
    EXTERNAL_VULKAN_LAYER_MANGOHUD,
    EXTERNAL_VULKAN_LAYER_VKBASALT,
)


class ConfigFieldType(str, Enum):
    """Configuration field types - must match TypeScript enum"""
    BOOLEAN = "boolean"
    INTEGER = "integer"
    FLOAT = "float"
    STRING = "string"


ConfigValue = Union[bool, int, float, str]
ConfigFieldLocation = Literal["global", "toml", "profile", "script"]


class ConfigFieldDefinition(TypedDict):
    """One canonical configuration field owned by this shared schema."""

    fieldType: ConfigFieldType
    default: ConfigValue
    description: str
    location: ConfigFieldLocation


CONFIG_SCHEMA_DEF: Dict[str, ConfigFieldDefinition] = {
    "dll": {
        "fieldType": ConfigFieldType.STRING,
        "default": "",
        "description": "optional full path to Lossless.dll; leave blank for automatic discovery",
        "location": "global"
    },

    "allow_fp16": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": True,
        "description": "allow FP16 acceleration (disable on older NVIDIA GPUs)",
        "location": "global"
    },

    "frame_generation_enabled": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": True,
        "description": "live on/off switch; leave on for fixed or adaptive generation, off stops both modes",
        "location": "toml"
    },

    "base_fps_cap": {
        "fieldType": ConfigFieldType.INTEGER,
        "default": 0,
        "description": "backend-independent real framerate cap applied before frame generation",
        "location": "toml"
    },

    "multiplier": {
        "fieldType": ConfigFieldType.INTEGER,
        "default": 2,
        "description": "change the fps multiplier",
        "location": "toml"
    },

    "adaptive": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "dynamically vary generated frames to approach a target framerate",
        "location": "toml"
    },

    "adaptive_auto_base_fps_cap": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": True,
        "description": "automatically cap real FPS at half the target for steadier 2x frame generation",
        "location": "toml"
    },

    "target_fps": {
        "fieldType": ConfigFieldType.INTEGER,
        "default": 90,
        "description": "target displayed framerate for adaptive frame generation",
        "location": "toml"
    },

    "adaptive_max_multiplier": {
        "fieldType": ConfigFieldType.INTEGER,
        "default": 3,
        "description": "ceiling for generated frames in adaptive mode",
        "location": "toml"
    },

    "adaptive_stable_cadence": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": True,
        "description": "prefer smoother constant interpolation; may lower real-frame cadence and increase input lag",
        "location": "toml"
    },

    "dynamic_cadence_recovery": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "mode-independent compatibility recovery for native frame-rate switches; Fixed follows confirmed refresh, enabling clears both base FPS caps, and choosing an incompatible preset or cap disables recovery",
        "location": "toml"
    },

    "flow_scale": {
        "fieldType": ConfigFieldType.FLOAT,
        "default": 0.9,
        "description": "change the flow scale",
        "location": "toml"
    },

    "performance_mode": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "use a lighter FG model to reduce GPU overhead, at the cost of more visual artifacts",
        "location": "toml"
    },

    "pacing": {
        "fieldType": ConfigFieldType.STRING,
        "default": "none",
        "description": "frame pacing mode (currently only 'none' supported)",
        "location": "toml"
    },

    "active_in": {
        "fieldType": ConfigFieldType.STRING,
        "default": "",
        "description": "optional executable or process names, separated by commas",
        "location": "profile"
    },

    "gpu": {
        "fieldType": ConfigFieldType.STRING,
        "default": "",
        "description": "optional GPU name, vendor:device ID, or PCI bus ID",
        "location": "profile"
    },

    "disable_mako": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "troubleshooting: prevent MAKO Renderer loading after restart",
        "location": "script"
    },

    # HDR frame generation is still under active development. The Decky .25
    # package deliberately locks this safety boundary on so existing profiles
    # cannot opt into the unfinished transport accidentally.
    "disable_hdr_exposure": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": True,
        "description": "required SDR safety boundary while HDR is unavailable",
        "location": "script"
    },

    "external_vulkan_layer": {
        "fieldType": ConfigFieldType.STRING,
        "default": "",
        "description": "optional guarded host Vulkan layer: gamescope-wsi, mangohud, or vkbasalt",
        "location": "script"
    },

    # Unsupported controls are intentionally omitted from the current schema.

    "disable_steamdeck_mode": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "disable Steam Deck mode (unlocks hidden settings in some games)",
        "location": "script"
    },

    "enable_zink": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "Enable Zink (Vulkan-based OpenGL implementation) for OpenGL games",
        "location": "script"
    },

    "force_alsa_audio": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "may improve compatibility with modes such as Zink and reduce audio stuttering or sudden loud sounds; restart required",
        "location": "script"
    }
}


def get_field_names() -> list[str]:
    """Get ordered list of configuration field names"""
    return list(CONFIG_SCHEMA_DEF.keys())


def get_defaults() -> Dict[str, ConfigValue]:
    """Get default configuration values"""
    return {
        field_name: field_def["default"]
        for field_name, field_def in CONFIG_SCHEMA_DEF.items()
    }


def get_field_types() -> Dict[str, str]:
    """Get field type mapping"""
    return {
        field_name: field_def["fieldType"].value
        for field_name, field_def in CONFIG_SCHEMA_DEF.items()
    }
