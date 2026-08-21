"""
Shared configuration schema constants.

This file contains the canonical configuration schema that should be used
by both Python and TypeScript code. Any changes to the configuration
structure should be made here first.
"""

from typing import Dict, Any, Union
from enum import Enum


class ConfigFieldType(str, Enum):
    """Configuration field types - must match TypeScript enum"""
    BOOLEAN = "boolean"
    INTEGER = "integer"
    FLOAT = "float"
    STRING = "string"


CONFIG_SCHEMA_DEF = {
    "dll": {
        "name": "dll",
        "fieldType": ConfigFieldType.STRING,
        "default": "",
        "description": "optional full path to Lossless.dll; leave blank for automatic discovery",
        "location": "global"
    },

    "allow_fp16": {
        "name": "allow_fp16",
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": True,
        "description": "allow FP16 acceleration (disable on older NVIDIA GPUs)",
        "location": "global"
    },

    "frame_generation_enabled": {
        "name": "frame_generation_enabled",
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": True,
        "description": "live on/off switch; leave on for fixed or adaptive generation, off stops both modes",
        "location": "toml"
    },

    "base_fps_cap": {
        "name": "base_fps_cap",
        "fieldType": ConfigFieldType.INTEGER,
        "default": 0,
        "description": "backend-independent real framerate cap applied before frame generation",
        "location": "toml"
    },

    "multiplier": {
        "name": "multiplier",
        "fieldType": ConfigFieldType.INTEGER,
        "default": 2,
        "description": "change the fps multiplier",
        "location": "toml"
    },

    "adaptive": {
        "name": "adaptive",
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "dynamically vary generated frames to approach a target framerate",
        "location": "toml"
    },

    "adaptive_auto_base_fps_cap": {
        "name": "adaptive_auto_base_fps_cap",
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": True,
        "description": "automatically cap real FPS at half the target for steadier 2x frame generation",
        "location": "toml"
    },

    "target_fps": {
        "name": "target_fps",
        "fieldType": ConfigFieldType.INTEGER,
        "default": 90,
        "description": "target displayed framerate for adaptive frame generation",
        "location": "toml"
    },

    "adaptive_max_multiplier": {
        "name": "adaptive_max_multiplier",
        "fieldType": ConfigFieldType.INTEGER,
        "default": 3,
        "description": "ceiling for generated frames in adaptive mode",
        "location": "toml"
    },

    "adaptive_stable_cadence": {
        "name": "adaptive_stable_cadence",
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": True,
        "description": "prefer smoother constant interpolation; may lower real-frame cadence and increase input lag",
        "location": "toml"
    },

    "flow_scale": {
        "name": "flow_scale",
        "fieldType": ConfigFieldType.FLOAT,
        "default": 0.9,
        "description": "change the flow scale",
        "location": "toml"
    },

    "performance_mode": {
        "name": "performance_mode",
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "use a lighter FG model to reduce GPU overhead, at the cost of more visual artifacts",
        "location": "toml"
    },

    "pacing": {
        "name": "pacing",
        "fieldType": ConfigFieldType.STRING,
        "default": "none",
        "description": "frame pacing mode (currently only 'none' supported)",
        "location": "toml"
    },

    "active_in": {
        "name": "active_in",
        "fieldType": ConfigFieldType.STRING,
        "default": "",
        "description": "optional executable or process names, separated by commas",
        "location": "profile"
    },

    "gpu": {
        "name": "gpu",
        "fieldType": ConfigFieldType.STRING,
        "default": "",
        "description": "optional GPU name, vendor:device ID, or PCI bus ID",
        "location": "profile"
    },

    "disable_mako": {
        "name": "disable_mako",
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "troubleshooting: prevent MAKO Renderer loading after restart",
        "location": "script"
    },

    # HDR frame generation is still under active development. The Decky .25
    # package deliberately locks this safety boundary on so existing profiles
    # cannot opt into the unfinished transport accidentally.
    "disable_hdr_exposure": {
        "name": "disable_hdr_exposure",
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": True,
        "description": "required SDR safety boundary while HDR is unavailable",
        "location": "script"
    },

    "external_vulkan_layer": {
        "name": "external_vulkan_layer",
        "fieldType": ConfigFieldType.STRING,
        "default": "",
        "description": "optional guarded host Vulkan layer: gamescope-wsi, mangohud, or vkbasalt",
        "location": "script"
    },

    # Unsupported controls are intentionally omitted from the current schema.

    "disable_steamdeck_mode": {
        "name": "disable_steamdeck_mode",
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "disable Steam Deck mode (unlocks hidden settings in some games)",
        "location": "script"
    },

    "enable_zink": {
        "name": "enable_zink",
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "Enable Zink (Vulkan-based OpenGL implementation) for OpenGL games",
        "location": "script"
    },

    "force_alsa_audio": {
        "name": "force_alsa_audio",
        "fieldType": ConfigFieldType.BOOLEAN,
        "default": False,
        "description": "may improve compatibility with modes such as Zink and reduce audio stuttering or sudden loud sounds; restart required",
        "location": "script"
    }
}


def get_field_names() -> list[str]:
    """Get ordered list of configuration field names"""
    return list(CONFIG_SCHEMA_DEF.keys())


def get_defaults() -> Dict[str, Union[bool, int, float, str]]:
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
