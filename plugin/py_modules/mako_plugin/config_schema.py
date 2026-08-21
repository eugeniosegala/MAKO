"""MAKO Renderer configuration and Decky profile management."""

import json
import logging
import re
import tomllib
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, TypedDict, Union, cast

from shared_config import (
    ADAPTIVE_MAX_MULTIPLIER_MAX,
    ADAPTIVE_MAX_MULTIPLIER_MIN,
    BASE_FPS_CAP_MAX,
    BASE_FPS_CAP_MIN,
    CONFIG_SCHEMA_DEF,
    DEFAULT_PROFILE_NAME,
    EXTERNAL_VULKAN_LAYER_VALUES,
    FIXED_MULTIPLIER_MIN,
    FLOW_SCALE_MAX,
    FLOW_SCALE_MIN,
    PROFILE_KIND_DEFAULT,
    PROFILE_KIND_GAME,
    PROFILE_KIND_MANUAL,
    PROFILE_KIND_PROCESS,
    PROFILE_KIND_VALUES,
    TARGET_FPS_MAX,
    TARGET_FPS_MIN,
    ConfigFieldType,
    get_defaults,
)
from .config_schema_generated import ConfigurationData, get_script_generation_logic, get_script_parsing_logic


@dataclass
class ConfigField:
    name: str
    field_type: ConfigFieldType
    default: Union[bool, int, float, str]
    description: str


CONFIG_SCHEMA: Dict[str, ConfigField] = {
    name: ConfigField(
        name=name,
        field_type=ConfigFieldType(definition["fieldType"]),
        default=definition["default"],
        description=definition["description"],
    )
    for name, definition in CONFIG_SCHEMA_DEF.items()
}
GLOBAL_SECTION_FIELDS = {
    name for name, definition in CONFIG_SCHEMA_DEF.items()
    if definition["location"] == "global"
}
SCRIPT_ONLY_FIELDS = {
    name for name, definition in CONFIG_SCHEMA_DEF.items()
    if definition["location"] == "script"
}
PROFILE_TOML_FIELDS = {
    name for name, definition in CONFIG_SCHEMA_DEF.items()
    if definition["location"] in {"toml", "profile"}
}
CONFIG_FORMAT_VERSION = 2
LEGACY_CONFIG_FORMAT_VERSION = 1
CURRENT_PROFILE_COMMENT_KEY = "decky-current-profile"
CURRENT_PROFILE_COMMENT = re.compile(
    rf'^\s*#\s*{re.escape(CURRENT_PROFILE_COMMENT_KEY)}\s*=\s*"([^"]+)"\s*$'
)


class ProfileData(TypedDict):
    current_profile: str
    profiles: Dict[str, ConfigurationData]
    global_config: Dict[str, Any]


def _toml_string(value: str) -> str:
    return json.dumps(value)


class ConfigurationManager:
    """Read and write the configuration format accepted by MAKO Renderer."""

    @staticmethod
    def get_defaults() -> ConfigurationData:
        return cast(ConfigurationData, dict(get_defaults()))

    @staticmethod
    def get_defaults_with_dll_detection(dll_detection_service=None) -> ConfigurationData:
        defaults = ConfigurationManager.get_defaults()
        if dll_detection_service:
            try:
                result = dll_detection_service.check_lossless_scaling_dll()
                if result.get("detected") and result.get("path"):
                    defaults["dll"] = result["path"]
            except (OSError, IOError, KeyError, TypeError) as error:
                logging.getLogger(__name__).debug("DLL detection failed: %s", error)
        return defaults

    @staticmethod
    def _copy_profiles(
            profiles: Dict[str, ConfigurationData],
    ) -> Dict[str, ConfigurationData]:
        """Copy the profile collection without retaining mutable config aliases."""
        return {
            name: cast(ConfigurationData, dict(config))
            for name, config in profiles.items()
        }

    @staticmethod
    def get_field_names() -> list[str]:
        return list(CONFIG_SCHEMA)

    @staticmethod
    def get_field_types() -> Dict[str, ConfigFieldType]:
        return {name: field.field_type for name, field in CONFIG_SCHEMA.items()}

    @staticmethod
    def validate_config(config: Dict[str, Any]) -> ConfigurationData:
        """Return current-schema values and deliberately discard unknown keys.

        Unknown or removed profile options must never become active implicitly.
        A setting whose meaning changes belongs in an explicit migration before
        canonical validation, rather than being retained as untyped state.
        """
        validated: Dict[str, Any] = {}
        for name, field in CONFIG_SCHEMA.items():
            value = config.get(name, field.default)
            if field.field_type == ConfigFieldType.BOOLEAN:
                if isinstance(value, str):
                    value = value.lower() in {"true", "1", "yes", "on"}
                else:
                    value = bool(value)
            elif field.field_type == ConfigFieldType.INTEGER:
                value = int(value)
            elif field.field_type == ConfigFieldType.FLOAT:
                value = float(value)
            else:
                value = str(value)
            validated[name] = value

        if validated["multiplier"] < FIXED_MULTIPLIER_MIN:
            raise ValueError(
                f"multiplier must be {FIXED_MULTIPLIER_MIN} or greater"
            )
        if not BASE_FPS_CAP_MIN <= validated["base_fps_cap"] <= BASE_FPS_CAP_MAX:
            raise ValueError(
                "base_fps_cap must be between "
                f"{BASE_FPS_CAP_MIN} and {BASE_FPS_CAP_MAX}"
            )
        if not TARGET_FPS_MIN <= validated["target_fps"] <= TARGET_FPS_MAX:
            raise ValueError(
                f"target_fps must be between {TARGET_FPS_MIN} and {TARGET_FPS_MAX}"
            )
        if not (
            ADAPTIVE_MAX_MULTIPLIER_MIN
            <= validated["adaptive_max_multiplier"]
            <= ADAPTIVE_MAX_MULTIPLIER_MAX
        ):
            raise ValueError(
                "adaptive_max_multiplier must be between "
                f"{ADAPTIVE_MAX_MULTIPLIER_MIN} and "
                f"{ADAPTIVE_MAX_MULTIPLIER_MAX}"
            )
        if not FLOW_SCALE_MIN <= validated["flow_scale"] <= FLOW_SCALE_MAX:
            raise ValueError(
                f"flow_scale must be between {FLOW_SCALE_MIN} and {FLOW_SCALE_MAX}"
            )
        if validated["pacing"] != CONFIG_SCHEMA["pacing"].default:
            raise ValueError("only pacing = 'none' is currently available")
        external_vulkan_layer = validated["external_vulkan_layer"].strip().lower()
        if external_vulkan_layer not in EXTERNAL_VULKAN_LAYER_VALUES:
            raise ValueError(
                "external_vulkan_layer must be empty, 'gamescope-wsi', "
                "'mangohud', or 'vkbasalt'"
            )
        validated["external_vulkan_layer"] = external_vulkan_layer
        return cast(ConfigurationData, validated)

    @staticmethod
    def _config_from_profile(profile: Dict[str, Any], global_config: Dict[str, Any]) -> ConfigurationData:
        config = ConfigurationManager.get_defaults()
        for field in PROFILE_TOML_FIELDS | SCRIPT_ONLY_FIELDS:
            if field not in profile:
                continue
            value = profile[field]
            if field == "active_in" and isinstance(value, list):
                value = ", ".join(str(item) for item in value)
            config[field] = value
        for field in GLOBAL_SECTION_FIELDS:
            if field in global_config:
                config[field] = global_config[field]
        return ConfigurationManager.validate_config(config)

    @staticmethod
    def generate_toml_content(config: ConfigurationData) -> str:
        data: ProfileData = {
            "current_profile": DEFAULT_PROFILE_NAME,
            "profiles": {DEFAULT_PROFILE_NAME: config},
            "global_config": {field: config[field] for field in GLOBAL_SECTION_FIELDS},
        }
        return ConfigurationManager.generate_toml_content_multi_profile(data)

    @staticmethod
    def generate_toml_content_multi_profile(profile_data: ProfileData) -> str:
        global_config = profile_data["global_config"]
        lines = [
            f"version = {CONFIG_FORMAT_VERSION}",
            f"# {CURRENT_PROFILE_COMMENT_KEY} = "
            f"{_toml_string(profile_data['current_profile'])}",
            "",
            "[global]",
        ]
        if global_config.get("dll"):
            lines.append(f"dll = {_toml_string(str(global_config['dll']))}")
        lines.append(f"allow_fp16 = {str(bool(global_config.get('allow_fp16', True))).lower()}")

        for profile_name, raw_config in profile_data["profiles"].items():
            config = ConfigurationManager.validate_config({**raw_config, **global_config})
            lines.extend(["", "[[profile]]", f"name = {_toml_string(profile_name)}"])
            active_in = [entry.strip() for entry in config["active_in"].split(",") if entry.strip()]
            if len(active_in) == 1:
                lines.append(f"active_in = {_toml_string(active_in[0])}")
            elif active_in:
                lines.append("active_in = [" + ", ".join(_toml_string(entry) for entry in active_in) + "]")
            if config["gpu"]:
                lines.append(f"gpu = {_toml_string(config['gpu'])}")
            lines.extend([
                f"frame_generation_enabled = {str(config['frame_generation_enabled']).lower()}",
                f"base_fps_cap = {config['base_fps_cap']}",
                f"multiplier = {config['multiplier']}",
                f"adaptive = {str(config['adaptive']).lower()}",
                f"adaptive_auto_base_fps_cap = {str(config['adaptive_auto_base_fps_cap']).lower()}",
                f"target_fps = {config['target_fps']}",
                f"adaptive_max_multiplier = {config['adaptive_max_multiplier']}",
                f"adaptive_stable_cadence = {str(config['adaptive_stable_cadence']).lower()}",
                f"flow_scale = {config['flow_scale']}",
                f"performance_mode = {str(config['performance_mode']).lower()}",
                "pacing = 'none'",
            ])
        return "\n".join(lines) + "\n"

    @staticmethod
    def _profile_data_from_previous_schema(data: Dict[str, Any]) -> ProfileData:
        old_global = data.get("global", {})
        global_config = {
            "dll": old_global.get("dll", ""),
            "allow_fp16": not bool(old_global.get("no_fp16", False)),
        }
        profiles: Dict[str, ConfigurationData] = {}
        for game in data.get("game", []):
            name = str(game.get("exe", DEFAULT_PROFILE_NAME))
            migrated_profile = dict(game)
            migrated_profile["multiplier"] = max(
                FIXED_MULTIPLIER_MIN,
                int(migrated_profile.get(
                    "multiplier", FIXED_MULTIPLIER_MIN
                )),
            )
            profiles[name] = ConfigurationManager._config_from_profile(migrated_profile, global_config)
        if not profiles:
            profiles[DEFAULT_PROFILE_NAME] = ConfigurationManager.get_defaults()
        current = str(old_global.get("current_profile", DEFAULT_PROFILE_NAME))
        if current not in profiles:
            current = next(iter(profiles))
        return ProfileData(current_profile=current, profiles=profiles, global_config=global_config)

    @staticmethod
    def parse_toml_content_multi_profile(content: str) -> ProfileData:
        """Parse supported config data without rewriting the source file.

        Unknown keys in a supported format version are inert. They disappear
        only when a MAKO editor or migration later writes canonical config.
        """
        data = tomllib.loads(content)
        if data.get("version") == LEGACY_CONFIG_FORMAT_VERSION:
            return ConfigurationManager._profile_data_from_previous_schema(data)
        if data.get("version") != CONFIG_FORMAT_VERSION:
            raise ValueError("unsupported MAKO Renderer configuration version")
        global_config = dict(data.get("global", {}))
        global_config = {
            "dll": str(global_config.get("dll", "")),
            "allow_fp16": bool(global_config.get("allow_fp16", True)),
        }
        profiles: Dict[str, ConfigurationData] = {}
        for profile in data.get("profile", []):
            name = str(profile.get("name", DEFAULT_PROFILE_NAME))
            profiles[name] = ConfigurationManager._config_from_profile(profile, global_config)
        if not profiles:
            profiles[DEFAULT_PROFILE_NAME] = ConfigurationManager.get_defaults()
        current = DEFAULT_PROFILE_NAME if DEFAULT_PROFILE_NAME in profiles else next(iter(profiles))
        for line in content.splitlines():
            match = CURRENT_PROFILE_COMMENT.match(line)
            if match and match.group(1) in profiles:
                current = match.group(1)
                break
        return ProfileData(current_profile=current, profiles=profiles, global_config=global_config)

    @staticmethod
    def parse_toml_content(content: str) -> ConfigurationData:
        profile_data = ConfigurationManager.parse_toml_content_multi_profile(content)
        return profile_data["profiles"][profile_data["current_profile"]]

    @staticmethod
    def parse_script_content(script_content: str) -> Dict[str, Union[bool, int, str]]:
        return get_script_parsing_logic()(script_content.splitlines())

    @staticmethod
    def merge_config_with_script(toml_config: ConfigurationData, script_values: Dict[str, Union[bool, int, str]]) -> ConfigurationData:
        merged = dict(toml_config)
        for field in SCRIPT_ONLY_FIELDS:
            if field in script_values:
                merged[field] = script_values[field]
        return cast(ConfigurationData, merged)

    @staticmethod
    def normalize_profile_name(profile_name: str) -> str:
        return re.sub(r"\s+", "-", profile_name.strip()).strip("-")

    @staticmethod
    def validate_profile_name(profile_name: str) -> bool:
        normalized = ConfigurationManager.normalize_profile_name(profile_name)
        return bool(normalized) and not any(char in '\t\n\r\'"\\/$|&;()<>{}[]`*?' for char in normalized) and normalized.lower() not in {"global", "profile"}

    @staticmethod
    def create_profile(profile_data: ProfileData, profile_name: str, source_profile: str = None) -> ProfileData:
        if not ConfigurationManager.validate_profile_name(profile_name):
            raise ValueError(f"Invalid profile name: {profile_name}")
        name = ConfigurationManager.normalize_profile_name(profile_name)
        if name in profile_data["profiles"]:
            raise ValueError(f"Profile '{name}' already exists")
        source = source_profile if source_profile in profile_data["profiles"] else profile_data["current_profile"]
        profiles = ConfigurationManager._copy_profiles(profile_data["profiles"])
        profiles[name] = cast(ConfigurationData, dict(profiles[source]))
        return ProfileData(current_profile=profile_data["current_profile"], profiles=profiles, global_config=dict(profile_data["global_config"]))

    @staticmethod
    def delete_profile(profile_data: ProfileData, profile_name: str) -> ProfileData:
        if profile_name == DEFAULT_PROFILE_NAME:
            raise ValueError("Cannot delete the default profile")
        if profile_name not in profile_data["profiles"]:
            raise ValueError(f"Profile '{profile_name}' does not exist")
        profiles = ConfigurationManager._copy_profiles(profile_data["profiles"])
        del profiles[profile_name]
        current = profile_data["current_profile"]
        if current == profile_name:
            current = DEFAULT_PROFILE_NAME if DEFAULT_PROFILE_NAME in profiles else next(iter(profiles))
        return ProfileData(current_profile=current, profiles=profiles, global_config=dict(profile_data["global_config"]))

    @staticmethod
    def rename_profile(profile_data: ProfileData, old_name: str, new_name: str) -> ProfileData:
        if old_name == DEFAULT_PROFILE_NAME:
            raise ValueError("Cannot rename the default profile")
        if old_name not in profile_data["profiles"] or not ConfigurationManager.validate_profile_name(new_name):
            raise ValueError("Invalid profile rename")
        normalized = ConfigurationManager.normalize_profile_name(new_name)
        if normalized in profile_data["profiles"]:
            raise ValueError(f"Profile '{normalized}' already exists")
        profiles = {
            normalized if name == old_name else name: config
            for name, config in ConfigurationManager._copy_profiles(
                profile_data["profiles"]
            ).items()
        }
        current = normalized if profile_data["current_profile"] == old_name else profile_data["current_profile"]
        return ProfileData(current_profile=current, profiles=profiles, global_config=dict(profile_data["global_config"]))

    @staticmethod
    def set_current_profile(profile_data: ProfileData, profile_name: str) -> ProfileData:
        if profile_name not in profile_data["profiles"]:
            raise ValueError(f"Profile '{profile_name}' does not exist")
        return ProfileData(
            current_profile=profile_name,
            profiles=ConfigurationManager._copy_profiles(
                profile_data["profiles"]
            ),
            global_config=dict(profile_data["global_config"]),
        )
