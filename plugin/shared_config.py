"""
Shared configuration schema constants.

This file contains the canonical configuration schema that should be used
by both Python and TypeScript code. Any changes to the configuration
structure should be made here first.
"""

from typing import Dict, Literal, TypedDict, Union
from enum import Enum
from shared_flatpak_runtimes import SUPPORTED_FLATPAK_RUNTIME_VERSIONS


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
# Flatpak frontends whose games start in a child compatibility environment and
# therefore require MAKO's wrapper to be configured per game rather than on the
# launcher process itself.
PER_GAME_WRAPPER_FLATPAK_APP_IDS = (
    "com.heroicgameslauncher.hgl",
    "net.lutris.Lutris",
)

# Cross-language validation limits. Keep the deliberately narrower Decky UI
# ceiling separate from the canonical profile validation range.
BASE_FPS_CAP_MIN = 0
BASE_FPS_CAP_MAX = 240
BASE_FPS_CAP_UI_MAX = 120
TARGET_FPS_MIN = 30
TARGET_FPS_MAX = 240
ADAPTIVE_MAX_MULTIPLIER_MIN = 2
ADAPTIVE_MAX_MULTIPLIER_MAX = 5
ADAPTIVE_MINIMUM_BASE_FPS = 10
ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_AUTO = "auto"
ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_LOW = "low"
ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_MEDIUM = "medium"
ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_HIGH = "high"
ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_VERY_HIGH = "very-high"
ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_VALUES = (
    ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_AUTO,
    ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_LOW,
    ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_MEDIUM,
    ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_HIGH,
    ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_VERY_HIGH,
)
GAMESCOPE_VRR_MODE_FOLLOW_STEAM = "follow-steam"
GAMESCOPE_VRR_MODE_ON = "on"
GAMESCOPE_VRR_MODE_OFF = "off"
GAMESCOPE_VRR_MODE_VALUES = (
    GAMESCOPE_VRR_MODE_FOLLOW_STEAM,
    GAMESCOPE_VRR_MODE_ON,
    GAMESCOPE_VRR_MODE_OFF,
)
DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_MIN = 0.1
DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_MAX = 3
DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_VALUES = (
    0.1,
    0.2,
    0.25,
    0.5,
    0.75,
    1.0,
    1.5,
    2.0,
    3.0,
)
FLOW_SCALE_MIN = 0.25
FLOW_SCALE_MAX = 1.0
ULTRA_PERFORMANCE_FLOW_SCALE = 0.7
SCALING_FACTOR_MIN = 1.0
SCALING_FACTOR_MAX = 2.0
SCALING_SHARPNESS_MIN = 0.0
SCALING_SHARPNESS_MAX = 1.0
SCALING_METHOD_NATIVE = "native"
SCALING_METHOD_MAKO = "mako"
SCALING_METHOD_LS1 = "ls1"
SCALING_METHOD_LS1_PERFORMANCE = "ls1-performance"
SCALING_METHOD_VALUES = (
    SCALING_METHOD_NATIVE,
    SCALING_METHOD_MAKO,
    SCALING_METHOD_LS1,
    SCALING_METHOD_LS1_PERFORMANCE,
)
FIXED_MULTIPLIER_MIN = 2
FIXED_MULTIPLIER_UI_MIN = FIXED_MULTIPLIER_MIN
FIXED_MULTIPLIER_UI_MAX = 5
FRAME_GENERATION_REFRESH_THRESHOLD_MIN = 0
FRAME_GENERATION_REFRESH_THRESHOLD_MAX = 240
FRAME_GENERATION_REFRESH_THRESHOLD_UI_MIN = 30
FRAME_GENERATION_REFRESH_THRESHOLD_PRESET = 60

# Stable persisted values for the mutually exclusive post-process layer.
# Decky 2.2 stored Gamescope WSI in this released selector. Retain that exact
# value only as an upgrade token; it is not accepted by the current selector.
EXTERNAL_VULKAN_LAYER_NONE = ""
EXTERNAL_VULKAN_LAYER_GAMESCOPE_WSI = "gamescope-wsi"
EXTERNAL_VULKAN_LAYER_MANGOHUD = "mangohud"
EXTERNAL_VULKAN_LAYER_VKBASALT = "vkbasalt"
EXTERNAL_VULKAN_LAYER_VALUES = (
    EXTERNAL_VULKAN_LAYER_NONE,
    EXTERNAL_VULKAN_LAYER_MANGOHUD,
    EXTERNAL_VULKAN_LAYER_VKBASALT,
)

# Decky-owned vkBasalt controls. The Default profile merges them into
# vkBasalt's normal global file; every saved profile uses an isolated file.
VKBASALT_SHARPENING_NONE = "none"
VKBASALT_SHARPENING_CAS = "cas"
VKBASALT_SHARPENING_DLS = "dls"
VKBASALT_SHARPENING_VALUES = (
    VKBASALT_SHARPENING_NONE,
    VKBASALT_SHARPENING_CAS,
    VKBASALT_SHARPENING_DLS,
)
VKBASALT_ANTIALIASING_NONE = "none"
VKBASALT_ANTIALIASING_FXAA = "fxaa"
VKBASALT_ANTIALIASING_SMAA = "smaa"
VKBASALT_ANTIALIASING_VALUES = (
    VKBASALT_ANTIALIASING_NONE,
    VKBASALT_ANTIALIASING_FXAA,
    VKBASALT_ANTIALIASING_SMAA,
)
VKBASALT_SHADER_NONE = "none"
VKBASALT_SHADER_VIBRANCE = "vibrance"
VKBASALT_SHADER_CURVES = "curves"
VKBASALT_SHADER_DEBAND = "deband"
VKBASALT_SHADER_TECHNICOLOR = "technicolor"
VKBASALT_SHADER_SEPIA = "sepia"
VKBASALT_SHADER_MONOCHROME = "monochrome"
VKBASALT_SHADER_VIGNETTE = "vignette"
VKBASALT_SHADER_HDR_LOOK = "hdr_look"
VKBASALT_SHADER_COLOURFULNESS = "colourfulness"
VKBASALT_SHADER_TECHNICOLOR2 = "technicolor2"
VKBASALT_SHADER_DPX = "dpx"
VKBASALT_SHADER_BLEACH_BYPASS = "bleach_bypass"
VKBASALT_SHADER_NOIR = "noir"
VKBASALT_SHADER_FILM_GRAIN = "film_grain"
VKBASALT_SHADER_CARTOON = "cartoon"
VKBASALT_SHADER_NOSTALGIA = "nostalgia"
VKBASALT_SHADER_CHROMATIC_ABERRATION = "chromatic_aberration"
VKBASALT_SHADER_CLARITY = "clarity"
VKBASALT_SHADER_LEVELS_PLUS = "levels_plus"
VKBASALT_SHADER_VALUES = (
    VKBASALT_SHADER_NONE,
    VKBASALT_SHADER_HDR_LOOK,
    VKBASALT_SHADER_CLARITY,
    VKBASALT_SHADER_LEVELS_PLUS,
    VKBASALT_SHADER_VIBRANCE,
    VKBASALT_SHADER_COLOURFULNESS,
    VKBASALT_SHADER_CURVES,
    VKBASALT_SHADER_DEBAND,
    VKBASALT_SHADER_TECHNICOLOR2,
    VKBASALT_SHADER_DPX,
    VKBASALT_SHADER_BLEACH_BYPASS,
    VKBASALT_SHADER_NOIR,
    VKBASALT_SHADER_TECHNICOLOR,
    VKBASALT_SHADER_MONOCHROME,
    VKBASALT_SHADER_SEPIA,
    VKBASALT_SHADER_FILM_GRAIN,
    VKBASALT_SHADER_VIGNETTE,
    VKBASALT_SHADER_CARTOON,
    VKBASALT_SHADER_NOSTALGIA,
    VKBASALT_SHADER_CHROMATIC_ABERRATION,
)
VKBASALT_STRENGTH_MIN = 0.0
VKBASALT_STRENGTH_MAX = 1.0


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
        "description": "select FP16 for supported LSFG model and MAKO Scaler arithmetic; false selects FP32; LS1 always uses FP32",
        "location": "global"
    },

    "scaling_enabled": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "restart-bound scaling engine switch independent of Gamescope WSI compatibility",
        "location": "toml"
    },

    "scaling_method": {
        "fieldType": ConfigFieldType.STRING,
        "default": SCALING_METHOD_LS1,
        "description": "live spatial selection inside a provisioned engine: Native Resolution, MAKO Scaler, LS1 Quality, or LS1 Performance",
        "location": "toml"
    },

    "scaling_factor": {
        "fieldType": ConfigFieldType.FLOAT,
        "default": 1.5,
        "description": "output scaling factor from 1.0x to 2.0x",
        "location": "toml"
    },

    "scaling_supersampling": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "allow quality supersampling beyond the proven display target while retaining Vulkan and memory safety ceilings",
        "location": "toml"
    },

    "scaling_sharpness": {
        "fieldType": ConfigFieldType.FLOAT,
        "default": 0.8,
        "description": "scaling sharpness from zero to one",
        "location": "toml"
    },

    "frame_generation_provisioned": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": True,
        "description": "restart-bound Frame Generation provisioning switch; disable to omit LSFG interop and backend resources",
        "location": "toml"
    },

    "frame_generation_enabled": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": True,
        "description": "live Frame Generation execution switch represented by 0x in the factor control",
        "location": "toml"
    },

    "frame_generation_refresh_threshold": {
        "fieldType": ConfigFieldType.INTEGER,
        "default": 0,
        "description": "pause frame generation at or below a confirmed Gamescope refresh rate; zero disables the guard",
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
        "description": "start at a half-target real FPS cap and let Smooth Cadence align validated integer-ratio rungs",
        "location": "toml"
    },

    "adaptive_fractional_real_frame_priority": {
        "fieldType": ConfigFieldType.STRING,
        "default": ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY_AUTO,
        "description": "optional Fractional Adaptive real-frame cap selected from cadence-friendly target ratios",
        "location": "toml"
    },

    "target_fps": {
        "fieldType": ConfigFieldType.INTEGER,
        "default": 90,
        "description": "target displayed framerate for adaptive frame generation",
        "location": "toml"
    },

    "adaptive_target_refresh_rate": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "follow confirmed display refresh in Adaptive while retaining Target FPS as the fallback",
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
        "description": "prefer an even display-divisor cadence; may lower real-frame cadence and increase input lag",
        "location": "toml"
    },

    "gamescope_vrr_mode": {
        "fieldType": ConfigFieldType.STRING,
        "default": GAMESCOPE_VRR_MODE_FOLLOW_STEAM,
        "description": "live Gamescope VRR preference during a matched MAKO game; follow Steam by default",
        "location": "toml"
    },

    "dynamic_cadence_recovery": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "mode-independent compatibility recovery for native frame-rate switches; Fixed follows confirmed refresh, enabling clears both base FPS caps, and choosing an incompatible preset or cap disables recovery",
        "location": "toml"
    },

    "dynamic_cadence_probe_interval_seconds": {
        "fieldType": ConfigFieldType.FLOAT,
        "default": 2.0,
        "description": "seconds between Dynamic Cadence Recovery probes; shorter intervals react faster but can make brief pacing hitches more frequent",
        "location": "toml"
    },

    "ultra_performance": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "restart-bound preset that may improve frame-generation performance by up to 30% in favourable GPU-limited scenarios with 70% flow scale, the lighter FG model and active-policy resource allocation while respecting the global precision choice; compatible controls remain available after startup",
        "location": "toml"
    },

    "flow_scale": {
        "fieldType": ConfigFieldType.FLOAT,
        "default": 0.8,
        "description": "adjust Frame Generation motion-estimation resolution; lower values reduce GPU work and higher values favour quality",
        "location": "toml"
    },

    "performance_mode": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "use a lighter Frame Generation model to reduce GPU work at the cost of more visual artifacts",
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
        "description": "troubleshooting: prevent MAKO Renderer loading on the next game launch",
        "location": "script"
    },

    # Native per-profile precision choice; private resources can change live.
    "hdr_reduced_precision": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": True,
        "description": "live approximate HDR10 frame generation and compact 10-bit PQ shader/scaling buffers; off restores full precision",
        "location": "toml"
    },

    # Restart-only opt-in. Keep existing profiles on SDR by default.
    "disable_hdr_exposure": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": True,
        "description": "disable game HDR exposure on the next launch",
        "location": "script"
    },

    "gamescope_wsi_compatibility": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "enable the restart-bound Gamescope WSI compatibility layer independently of scaling",
        "location": "script"
    },

    "swapchain_image_count_compatibility": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "preserve the application's requested swapchain image minimum for games that cannot start with generated-output headroom",
        "location": "profile"
    },

    "external_vulkan_layer": {
        "fieldType": ConfigFieldType.STRING,
        "default": "",
        "description": "optional guarded post-process Vulkan layer: MangoHud or vkBasalt",
        "location": "script"
    },

    "vkbasalt_sharpening": {
        "fieldType": ConfigFieldType.STRING,
        "default": VKBASALT_SHARPENING_CAS,
        "description": "MAKO-managed vkBasalt sharpening effect: none, CAS, or DLS",
        "location": "script"
    },

    "vkbasalt_sharpness": {
        "fieldType": ConfigFieldType.FLOAT,
        "default": 0.5,
        "description": "MAKO-managed vkBasalt CAS or DLS sharpening strength",
        "location": "script"
    },

    "vkbasalt_dls_denoise": {
        "fieldType": ConfigFieldType.FLOAT,
        "default": 0.2,
        "description": "MAKO-managed vkBasalt DLS denoise strength",
        "location": "script"
    },

    "vkbasalt_antialiasing": {
        "fieldType": ConfigFieldType.STRING,
        "default": VKBASALT_ANTIALIASING_NONE,
        "description": "MAKO-managed vkBasalt anti-aliasing effect: none, FXAA, or SMAA",
        "location": "script"
    },

    "vkbasalt_shader": {
        "fieldType": ConfigFieldType.STRING,
        "default": VKBASALT_SHADER_NONE,
        "description": "Ordered colon-separated bundled effects or custom/<profile effect name>, or none",
        "location": "script"
    },

    "vkbasalt_manage_custom_shaders": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "Use the UI effect selection for custom shaders after the first explicit effect edit; false preserves legacy advanced chains",
        "location": "script"
    },

    # Unsupported controls are intentionally omitted from the current schema.

    "disable_steamdeck_mode": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "disable Steam Deck mode (unlocks hidden settings in some games)",
        "location": "script"
    },

    "disable_steam_overlay": {
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": True,
        "description": "disable Steam overlay hooks at launch to improve smoothness; may affect Steam Input and overlay features; restart required",
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
