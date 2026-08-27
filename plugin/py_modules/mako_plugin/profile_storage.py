"""Canonical Decky profile sidecars and merged profile views.

MAKO Renderer TOML remains owned by :mod:`config_schema`. This module owns the
Decky-only profile metadata and wrapper-setting sidecars that accompany it.
Keeping these operations independent from RPC orchestration makes their
allowlist, fallback, and serialization contracts reusable without teaching the
generated launch-wrapper code how files are stored.
"""

import json
from pathlib import Path
from typing import Any, Callable, Dict, Optional, TypedDict, cast

from .config_schema import (
    CONFIG_SCHEMA,
    DEFAULT_PROFILE_NAME,
    PROFILE_KIND_DEFAULT,
    PROFILE_KIND_MANUAL,
    PROFILE_KIND_PROCESS,
    SCRIPT_ONLY_FIELDS,
    ConfigurationManager,
    ProfileData,
)
from .config_schema_generated import (
    ConfigurationData,
    DISABLE_HDR_EXPOSURE,
    WrapperSettingsData,
)
from .types import ProfileDetails


class ProfileMetadataEntry(TypedDict):
    """Canonical persisted metadata for one profile."""

    display_name: str
    kind: str
    steam_app_id: Optional[str]
    captured_processes: list[str]


ProfileMetadata = Dict[str, ProfileMetadataEntry]
WrapperProfileSettings = Dict[str, WrapperSettingsData]
ManagedTextWriter = Callable[[Path, str, int], bool]
NormalizeWrapperSettings = Callable[[Dict[str, Any]], WrapperSettingsData]
WrapperSettingsDefaults = Callable[[], WrapperSettingsData]
ProcessesForConfig = Callable[[Dict[str, Any]], list[str]]
DefaultProfileMetadata = Callable[[ProfileData], ProfileMetadata]
WrapperSettingsForProfile = Callable[
    [str, WrapperProfileSettings],
    WrapperSettingsData,
]


def profile_metadata_entry(
        display_name: str,
        kind: str,
        steam_app_id: Optional[str] = None,
        captured_processes: list[str] | None = None,
) -> ProfileMetadataEntry:
    """Create one normalized metadata entry with all persisted fields."""
    return {
        "display_name": display_name,
        "kind": kind,
        "steam_app_id": steam_app_id,
        "captured_processes": list(captured_processes or []),
    }


def metadata_steam_app_id(
        metadata: ProfileMetadata,
        profile_name: str,
) -> Optional[str]:
    entry = metadata.get(profile_name)
    return entry.get("steam_app_id") if entry else None


def metadata_captured_processes(
        metadata: ProfileMetadata,
        profile_name: str,
) -> list[str]:
    entry = metadata.get(profile_name)
    return list(entry.get("captured_processes", [])) if entry else []


def replace_captured_processes(
        entry: ProfileMetadataEntry,
        processes: list[str],
) -> None:
    entry["captured_processes"] = list(processes)


def rename_profile_metadata(
        metadata: ProfileMetadata,
        old_name: str,
        new_name: str,
        display_name: str,
) -> None:
    if old_name not in metadata:
        return
    metadata[new_name] = metadata.pop(old_name)
    metadata[new_name]["display_name"] = display_name


def wrapper_settings_defaults() -> WrapperSettingsData:
    """Return current defaults for fields stored only in the wrapper sidecar."""
    return cast(WrapperSettingsData, {
        field_name: CONFIG_SCHEMA[field_name].default
        for field_name in SCRIPT_ONLY_FIELDS
    })


def normalize_wrapper_settings(
        raw_settings: Dict[str, Any],
) -> WrapperSettingsData:
    """Allowlist current wrapper settings without polluting Renderer TOML.

    Removed and unknown fields are intentionally discarded so profile data can
    never create an environment export unless the current schema and wrapper
    generator both support it.
    """
    candidate = ConfigurationManager.get_defaults()
    candidate.update({
        field_name: raw_settings[field_name]
        for field_name in SCRIPT_ONLY_FIELDS
        if field_name in raw_settings
    })
    validated = ConfigurationManager.validate_config(candidate)
    # HDR remains an engine foundation in this release, not a supported Decky
    # launch mode. Override both old opt-ins and new UI writes.
    validated[DISABLE_HDR_EXPOSURE] = True
    return cast(WrapperSettingsData, {
        field_name: validated[field_name]
        for field_name in SCRIPT_ONLY_FIELDS
    })


def read_wrapper_profile_settings(
        path: Path,
        version: int,
        logger: Any,
        normalize_settings: NormalizeWrapperSettings = normalize_wrapper_settings,
) -> WrapperProfileSettings:
    """Read persisted per-profile launcher settings, falling back safely."""
    if not path.exists():
        return {}

    try:
        raw_data = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(raw_data, dict):
            raise ValueError("wrapper settings must be a JSON object")
        if raw_data.get("version") != version:
            raise ValueError("unsupported wrapper settings version")
        raw_profiles = raw_data.get("profiles", {})
        if not isinstance(raw_profiles, dict):
            raise ValueError("wrapper settings profiles must be an object")
        settings: WrapperProfileSettings = {}
        for profile_name, raw_settings in raw_profiles.items():
            if isinstance(profile_name, str) and isinstance(raw_settings, dict):
                settings[profile_name] = normalize_settings(raw_settings)
        return settings
    except (
            OSError,
            IOError,
            ValueError,
            TypeError,
            json.JSONDecodeError,
    ) as error:
        logger.warning(
            "Ignoring invalid per-profile wrapper settings at %s: %s",
            path,
            error,
        )
        return {}


def write_wrapper_profile_settings(
        config_dir: Path,
        path: Path,
        version: int,
        profile_settings: WrapperProfileSettings,
        write_file: ManagedTextWriter,
        normalize_settings: NormalizeWrapperSettings = normalize_wrapper_settings,
) -> None:
    """Write a canonical, current-schema wrapper-settings sidecar."""
    normalized_profiles = {
        profile_name: normalize_settings(settings)
        for profile_name, settings in profile_settings.items()
    }
    payload = {
        "version": version,
        "profiles": normalized_profiles,
    }
    config_dir.mkdir(parents=True, exist_ok=True)
    write_file(
        path,
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        0o644,
    )


def wrapper_settings_for_profile(
        profile_name: str,
        profile_settings: WrapperProfileSettings,
        defaults_factory: WrapperSettingsDefaults = wrapper_settings_defaults,
        normalize_settings: NormalizeWrapperSettings = normalize_wrapper_settings,
) -> WrapperSettingsData:
    """Merge stored launcher settings onto current safe defaults."""
    settings = defaults_factory()
    stored_settings = profile_settings.get(profile_name)
    if stored_settings:
        settings.update(stored_settings)
    return normalize_settings(settings)


def processes_for_config(config: Dict[str, Any]) -> list[str]:
    """Return normalized process aliases from one Renderer profile."""
    active_in = config.get("active_in", "")
    if isinstance(active_in, (list, tuple)):
        values = active_in
    else:
        values = str(active_in).split(",")
    return [str(value).strip() for value in values if str(value).strip()]


def default_profile_metadata(
        profile_data: ProfileData,
        process_names: ProcessesForConfig = processes_for_config,
) -> ProfileMetadata:
    """Derive safe metadata for profiles without a persisted sidecar."""
    metadata: ProfileMetadata = {}
    for profile_name, config in profile_data["profiles"].items():
        processes = process_names(config)
        metadata[profile_name] = profile_metadata_entry(
            display_name=(
                "Default" if profile_name == DEFAULT_PROFILE_NAME
                else profile_name
            ),
            kind=(
                PROFILE_KIND_DEFAULT if profile_name == DEFAULT_PROFILE_NAME
                else PROFILE_KIND_PROCESS if processes
                else PROFILE_KIND_MANUAL
            ),
        )
    return metadata


def write_profile_metadata(
        config_dir: Path,
        path: Path,
        version: int,
        metadata: ProfileMetadata,
        write_file: ManagedTextWriter,
) -> None:
    """Write the canonical profile-identity sidecar."""
    payload = {
        "version": version,
        "profiles": metadata,
    }
    config_dir.mkdir(parents=True, exist_ok=True)
    write_file(
        path,
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        0o644,
    )


def read_profile_metadata(
        path: Path,
        version: int,
        profile_data: ProfileData,
        defaults_factory: DefaultProfileMetadata = default_profile_metadata,
) -> ProfileMetadata:
    """Read and normalize metadata against the canonical profile collection."""
    if not path.exists():
        return defaults_factory(profile_data)

    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise ValueError("profile metadata must be a JSON object")
    if payload.get("version") != version:
        raise ValueError("unsupported profile metadata version")
    raw_profiles = payload.get("profiles")
    if not isinstance(raw_profiles, dict):
        raise ValueError("profile metadata profiles must be an object")

    defaults = defaults_factory(profile_data)
    metadata: ProfileMetadata = {}
    for profile_name in profile_data["profiles"]:
        fallback = defaults[profile_name]
        raw_entry = raw_profiles.get(profile_name, {})
        if not isinstance(raw_entry, dict):
            raw_entry = {}
        steam_app_id = raw_entry.get("steam_app_id")
        raw_captured = raw_entry.get("captured_processes", [])
        if not isinstance(raw_captured, list):
            raw_captured = []
        metadata[profile_name] = profile_metadata_entry(
            display_name=str(
                raw_entry.get("display_name") or fallback["display_name"]
            ),
            kind=str(raw_entry.get("kind") or fallback["kind"]),
            steam_app_id=(
                str(steam_app_id).strip() if steam_app_id is not None else None
            ) or None,
            captured_processes=[
                str(process).strip()
                for process in raw_captured
                if str(process).strip()
            ],
        )
    return metadata


def profile_details(
        profile_data: ProfileData,
        metadata: ProfileMetadata,
        process_names: ProcessesForConfig = processes_for_config,
) -> list[ProfileDetails]:
    """Build the public profile summary without mutating stored selection."""
    return [
        {
            "profile_name": profile_name,
            "display_name": metadata[profile_name]["display_name"],
            "kind": metadata[profile_name]["kind"],
            "steam_app_id": metadata[profile_name]["steam_app_id"],
            "processes": process_names(config),
        }
        for profile_name, config in profile_data["profiles"].items()
    ]


def config_for_profile(
        profile_data: ProfileData,
        profile_name: str,
        profile_settings: WrapperProfileSettings,
        settings_for_profile: WrapperSettingsForProfile = wrapper_settings_for_profile,
) -> ConfigurationData:
    """Merge Renderer TOML, global, and Decky-only fields for one profile."""
    config = dict(
        profile_data["profiles"].get(
            profile_name,
            ConfigurationManager.get_defaults(),
        )
    )
    config.update(profile_data["global_config"])
    config.update(settings_for_profile(profile_name, profile_settings))
    return ConfigurationManager.validate_config(config)
