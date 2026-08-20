"""Configuration service for MAKO Renderer TOML and Decky profiles."""

import json
from pathlib import Path
import re
import shlex
from typing import Dict, Any, Optional

from .build_flavor import LOCAL_DEVELOPMENT_BUILD
from .base_service import BaseService
from .config_schema import (
    ConfigurationManager,
    CONFIG_SCHEMA,
    SCRIPT_ONLY_FIELDS,
    ProfileData,
    DEFAULT_PROFILE_NAME,
)
from .config_schema_generated import (
    ConfigurationData,
    DISABLE_HDR_EXPOSURE,
    get_script_generation_logic,
)
from .constants import (
    ARMADA_DEVICE_ENV,
    ARMADA_GAME_LAUNCH,
    COMPETING_LSFG_DISABLE_ENVS,
    DXVK_HDR_ENV,
    FLATPAK_IMPLICIT_LAYER_DIR,
    GAMESCOPE_WSI_DISABLE_ENV,
    GAMESCOPE_WSI_ENABLE_ENV,
    HDR_EXPOSURE_DISABLE_ENV,
    MAKO_LAYER_DISABLE_ENV,
    MAKO_LAYER_ENABLE_ENV,
    PRESENT_ACQUIRE_TIMEOUT_MS,
)
from .process_detection import (
    detect_processes_for_steam_app,
    is_matchable_process_name,
)
from .types import ConfigurationResponse, ProfilesResponse, ProfileResponse


class ConfigurationService(BaseService):
    """Service for managing MAKO Renderer TOML configuration."""

    _WRAPPER_FORMAT_MARKER = "# mako-wrapper-format: 42"
    _HOST_COMPATIBILITY_MARKER = (
        "# mako-host-compatibility: aarch64-passthrough-v1"
    )
    _WRAPPER_PROFILE_SETTINGS_VERSION = 1
    _PROFILE_METADATA_VERSION = 1
    _REQUIRED_WRAPPER_EXPORTS = (
        "export MAKO_PRESENT_ACQUIRE_TIMEOUT_MS=",
        "export MAKO_PRESENT_DIAGNOSTICS=",
        f"export {MAKO_LAYER_ENABLE_ENV}=1",
        *(f"export {variable}=1" for variable in COMPETING_LSFG_DISABLE_ENVS),
        f"export {GAMESCOPE_WSI_DISABLE_ENV}=1",
        f"unset {GAMESCOPE_WSI_ENABLE_ENV}",
        "export VK_IMPLICIT_LAYER_PATH=",
        "unset VK_ADD_IMPLICIT_LAYER_PATH",
        "export MAKO_PROFILE_FALLBACK=",
        "mako_diagnostics_default=",
    )
    _OBSOLETE_WRAPPER_EXPORTS = (
        "DXVK_FRAME_RATE",
        "PROTON_USE_WOW64",
        "MAKO_PRESENT_RECOVERY_RECREATE",
        "MAKO_EXPERIMENTAL_HDR",
        "VK_INSTANCE_LAYERS",
    )

    def __init__(
            self,
            logger: Optional[Any] = None,
            development_build: Optional[bool] = None):
        super().__init__(logger=logger)
        self.development_build = (
            LOCAL_DEVELOPMENT_BUILD
            if development_build is None
            else development_build
        )

    def _diagnostics_default_marker(self) -> str:
        return "# development presentation diagnostics default: disabled"

    @staticmethod
    def _wrapper_settings_defaults() -> Dict[str, Any]:
        return {
            field_name: CONFIG_SCHEMA[field_name].default
            for field_name in SCRIPT_ONLY_FIELDS
        }

    @staticmethod
    def _normalize_wrapper_settings(raw_settings: Dict[str, Any]) -> Dict[str, Any]:
        """Allowlist current wrapper settings without polluting engine TOML.

        Removed and unknown fields are intentionally discarded so profile data
        can never create an environment export unless the current schema and
        wrapper generator both support it.
        """
        candidate = ConfigurationManager.get_defaults()
        candidate.update({
            field_name: raw_settings[field_name]
            for field_name in SCRIPT_ONLY_FIELDS
            if field_name in raw_settings
        })
        validated = ConfigurationManager.validate_config(candidate)
        # HDR remains an engine foundation in this release, not a supported
        # Decky launch mode. Override old per-profile opt-ins as well as new UI
        # writes so the generated wrapper always retains the proven SDR path.
        validated[DISABLE_HDR_EXPOSURE] = True
        return {
            field_name: validated[field_name]
            for field_name in SCRIPT_ONLY_FIELDS
        }

    def _read_wrapper_profile_settings(self) -> Dict[str, Dict[str, Any]]:
        """Read persisted per-profile launcher settings, falling back safely."""
        if not self.wrapper_profile_settings_path.exists():
            return {}

        try:
            raw_data = json.loads(
                self.wrapper_profile_settings_path.read_text(encoding="utf-8")
            )
            if not isinstance(raw_data, dict):
                raise ValueError("wrapper settings must be a JSON object")
            if raw_data.get("version") != self._WRAPPER_PROFILE_SETTINGS_VERSION:
                raise ValueError("unsupported wrapper settings version")
            raw_profiles = raw_data.get("profiles", {})
            if not isinstance(raw_profiles, dict):
                raise ValueError("wrapper settings profiles must be an object")
            settings: Dict[str, Dict[str, Any]] = {}
            for profile_name, raw_settings in raw_profiles.items():
                if isinstance(profile_name, str) and isinstance(raw_settings, dict):
                    settings[profile_name] = self._normalize_wrapper_settings(raw_settings)
            return settings
        except (OSError, IOError, ValueError, TypeError, json.JSONDecodeError) as error:
            self.log.warning(
                "Ignoring invalid per-profile wrapper settings at %s: %s",
                self.wrapper_profile_settings_path,
                error,
            )
            return {}

    def _write_wrapper_profile_settings(
            self, profile_settings: Dict[str, Dict[str, Any]]) -> None:
        normalized_profiles = {
            profile_name: self._normalize_wrapper_settings(settings)
            for profile_name, settings in profile_settings.items()
        }
        payload = {
            "version": self._WRAPPER_PROFILE_SETTINGS_VERSION,
            "profiles": normalized_profiles,
        }
        self.config_dir.mkdir(parents=True, exist_ok=True)
        self._write_file(
            self.wrapper_profile_settings_path,
            json.dumps(payload, indent=2, sort_keys=True) + "\n",
            0o644,
        )

    def _wrapper_settings_for_profile(
            self,
            profile_name: str,
            profile_settings: Dict[str, Dict[str, Any]] = None,
    ) -> Dict[str, Any]:
        settings = self._wrapper_settings_defaults()
        stored_settings = (profile_settings or self._read_wrapper_profile_settings()).get(profile_name)
        if stored_settings:
            settings.update(stored_settings)
        return self._normalize_wrapper_settings(settings)

    @staticmethod
    def _processes_for_config(config: Dict[str, Any]) -> list[str]:
        active_in = config.get("active_in", "")
        if isinstance(active_in, (list, tuple)):
            values = active_in
        else:
            values = str(active_in).split(",")
        return [str(value).strip() for value in values if str(value).strip()]

    @classmethod
    def _default_profile_metadata(
            cls, profile_data: ProfileData) -> Dict[str, Dict[str, Any]]:
        metadata: Dict[str, Dict[str, Any]] = {}
        for profile_name, config in profile_data["profiles"].items():
            processes = cls._processes_for_config(config)
            metadata[profile_name] = {
                "display_name": "Default" if profile_name == DEFAULT_PROFILE_NAME else profile_name,
                "kind": (
                    "default" if profile_name == DEFAULT_PROFILE_NAME
                    else "process" if processes
                    else "manual"
                ),
                "steam_app_id": None,
                "captured_processes": [],
            }
        return metadata

    def _write_profile_metadata(
            self, metadata: Dict[str, Dict[str, Any]]) -> None:
        payload = {
            "version": self._PROFILE_METADATA_VERSION,
            "profiles": metadata,
        }
        self.config_dir.mkdir(parents=True, exist_ok=True)
        self._write_file(
            self.profile_metadata_path,
            json.dumps(payload, indent=2, sort_keys=True) + "\n",
            0o644,
        )

    def _read_profile_metadata(
            self, profile_data: ProfileData = None) -> Dict[str, Dict[str, Any]]:
        profile_data = profile_data or self._get_profile_data()
        if not self.profile_metadata_path.exists():
            return self._default_profile_metadata(profile_data)

        payload = json.loads(self.profile_metadata_path.read_text(encoding="utf-8"))
        if not isinstance(payload, dict):
            raise ValueError("profile metadata must be a JSON object")
        if payload.get("version") != self._PROFILE_METADATA_VERSION:
            raise ValueError("unsupported profile metadata version")
        raw_profiles = payload.get("profiles")
        if not isinstance(raw_profiles, dict):
            raise ValueError("profile metadata profiles must be an object")

        defaults = self._default_profile_metadata(profile_data)
        metadata: Dict[str, Dict[str, Any]] = {}
        for profile_name in profile_data["profiles"]:
            fallback = defaults[profile_name]
            raw_entry = raw_profiles.get(profile_name, {})
            if not isinstance(raw_entry, dict):
                raw_entry = {}
            steam_app_id = raw_entry.get("steam_app_id")
            raw_captured = raw_entry.get("captured_processes", [])
            if not isinstance(raw_captured, list):
                raw_captured = []
            metadata[profile_name] = {
                "display_name": str(
                    raw_entry.get("display_name") or fallback["display_name"]
                ),
                "kind": str(raw_entry.get("kind") or fallback["kind"]),
                "steam_app_id": (
                    str(steam_app_id).strip() if steam_app_id is not None else None
                ) or None,
                "captured_processes": [
                    str(process).strip()
                    for process in raw_captured
                    if str(process).strip()
                ],
            }
        return metadata

    def migrate_profile_metadata_if_needed(self) -> bool:
        """Create/synchronise the first public game/process profile model."""
        profile_data = self._get_profile_data()
        metadata = self._read_profile_metadata(profile_data)
        expected_payload = {
            "version": self._PROFILE_METADATA_VERSION,
            "profiles": metadata,
        }
        if self.profile_metadata_path.exists():
            current_payload = json.loads(
                self.profile_metadata_path.read_text(encoding="utf-8")
            )
            if current_payload == expected_payload:
                return False
        self._write_profile_metadata(metadata)
        return True

    def sanitize_captured_processes_if_needed(self) -> bool:
        """Remove shared launcher/helper names captured by older builds."""
        profile_data = self._get_profile_data()
        self.migrate_profile_metadata_if_needed()
        metadata = self._read_profile_metadata(profile_data)
        changed = False

        for profile_name, entry in metadata.items():
            captured = entry.get("captured_processes", [])
            safe_captured = [
                process_name
                for process_name in captured
                if is_matchable_process_name(process_name)
            ]
            if safe_captured == captured:
                continue

            unsafe_names = {
                process_name.casefold()
                for process_name in captured
                if not is_matchable_process_name(process_name)
            }
            config = profile_data["profiles"][profile_name]
            config["active_in"] = ", ".join(
                process_name
                for process_name in self._processes_for_config(config)
                if process_name.casefold() not in unsafe_names
            )
            entry["captured_processes"] = safe_captured
            changed = True

        if not changed:
            return False

        self._save_profile_data(profile_data)
        self._write_profile_metadata(metadata)
        script_result = self.update_mako_script_from_profile_data(profile_data)
        if not script_result["success"]:
            raise OSError(
                script_result.get("error")
                or "could not update launch wrapper"
            )
        return True

    def _profile_details(
            self,
            profile_data: ProfileData,
            metadata: Dict[str, Dict[str, Any]],
    ) -> list[Dict[str, Any]]:
        return [
            {
                "profile_name": profile_name,
                "display_name": metadata[profile_name]["display_name"],
                "kind": metadata[profile_name]["kind"],
                "steam_app_id": metadata[profile_name]["steam_app_id"],
                "processes": self._processes_for_config(config),
            }
            for profile_name, config in profile_data["profiles"].items()
        ]

    def _config_for_profile(
            self,
            profile_data: ProfileData,
            profile_name: str,
            profile_settings: Dict[str, Dict[str, Any]] = None,
    ) -> ConfigurationData:
        """Merge MAKO Renderer TOML, global, and Decky wrapper fields for one profile."""
        config = dict(
            profile_data["profiles"].get(
                profile_name, ConfigurationManager.get_defaults()
            )
        )
        config.update(profile_data["global_config"])
        config.update(self._wrapper_settings_for_profile(profile_name, profile_settings))
        return ConfigurationManager.validate_config(config)

    def migrate_wrapper_profile_settings_if_needed(self) -> bool:
        """Preserve old current-wrapper compatibility settings on first upgrade.

        Older releases stored these values only in the generated launcher. That
        launcher represented the selected profile, so it can be imported without
        guessing settings for any other profile.
        """
        if self.wrapper_profile_settings_path.exists() or not self.mako_script_path.exists():
            return False

        try:
            script_content = self.mako_script_path.read_text(encoding="utf-8")
            # Format 32 contains a branch for every profile. It is not a safe
            # source from which to reconstruct a missing settings database.
            # Only the pre-profile wrappers represented one selected profile.
            if self._WRAPPER_FORMAT_MARKER in script_content:
                return False
            script_values = ConfigurationManager.parse_script_content(script_content)
            profile_data = self._get_profile_data()
            self._write_wrapper_profile_settings({
                profile_data["current_profile"]: self._normalize_wrapper_settings(script_values)
            })
            self.log.info(
                "Migrated wrapper-only settings into profile '%s'",
                profile_data["current_profile"],
            )
            return True
        except (OSError, IOError, ValueError, TypeError) as error:
            self.log.warning("Could not migrate wrapper-only profile settings: %s", error)
            return False

    def migrate_legacy_base_fps_caps_if_needed(self) -> bool:
        """Move the former DXVK-only wrapper cap into engine profiles.

        Wrapper format 27 stored ``dxvk_frame_rate`` outside TOML and exported
        ``DXVK_FRAME_RATE``. Format 28 uses the engine's backend-independent
        ``base_fps_cap`` instead. Preserve a non-zero cap once, then remove the
        legacy export so DirectX games are not limited twice.
        """
        raw_profiles: Dict[str, Dict[str, Any]] = {}
        legacy_artifact_found = False
        if self.wrapper_profile_settings_path.exists():
            try:
                payload = json.loads(
                    self.wrapper_profile_settings_path.read_text(encoding="utf-8")
                )
                stored_profiles = payload.get("profiles", {})
                if isinstance(stored_profiles, dict):
                    for profile_name, raw_settings in stored_profiles.items():
                        if isinstance(profile_name, str) and isinstance(raw_settings, dict):
                            raw_profiles[profile_name] = dict(raw_settings)
                            legacy_artifact_found = (
                                "dxvk_frame_rate" in raw_settings or
                                legacy_artifact_found
                            )
            except (OSError, IOError, ValueError, TypeError, json.JSONDecodeError) as error:
                self.log.warning(
                    "Could not inspect legacy Base FPS Cap settings: %s", error
                )

        profile_data = self._get_profile_data()
        legacy_caps: Dict[str, int] = {}
        for profile_name, raw_settings in raw_profiles.items():
            raw_cap = raw_settings.pop("dxvk_frame_rate", 0)
            try:
                cap = int(raw_cap)
            except (TypeError, ValueError):
                continue
            if 0 < cap <= 240:
                legacy_caps[profile_name] = cap

        if self.mako_script_path.exists():
            try:
                script_content = self.mako_script_path.read_text(encoding="utf-8")
                match = re.search(
                    r"^\s*export\s+DXVK_FRAME_RATE=(\d+)\s*$",
                    script_content,
                    flags=re.MULTILINE,
                )
                if match:
                    legacy_artifact_found = True
                    cap = int(match.group(1))
                    if 0 < cap <= 240:
                        legacy_caps.setdefault(
                            profile_data["current_profile"], cap
                        )
            except (OSError, IOError, ValueError):
                pass

        profile_changed = False
        for profile_name, cap in legacy_caps.items():
            profile = profile_data["profiles"].get(profile_name)
            if profile is None or profile.get("base_fps_cap", 0) != 0:
                continue
            profile["base_fps_cap"] = cap
            profile_changed = True

        if not legacy_artifact_found and not profile_changed:
            return False

        if profile_changed:
            self._save_profile_data(profile_data)
        if self.wrapper_profile_settings_path.exists() or raw_profiles:
            self._write_wrapper_profile_settings(raw_profiles)
        result = self.update_mako_script_from_profile_data(profile_data)
        if not result["success"]:
            raise OSError(result.get("error") or "could not migrate Base FPS Cap")
        return True

    @staticmethod
    def _has_active_in(config: ConfigurationData) -> bool:
        """Return whether an engine profile can select itself by process name."""
        active_in = config.get("active_in", "")
        if isinstance(active_in, (list, tuple)):
            return bool(active_in)
        return bool(str(active_in).strip())

    @classmethod
    def _profile_selection_lines(
            cls,
            profile_name: str,
            config: ConfigurationData,
            automatic_matching_enabled: bool = None,
    ) -> list[str]:
        """Keep the renderer active while allowing automatic live matching.

        A caller-provided ``MAKO_PROFILE`` deliberately overrides both Decky's
        selected profile and mako's ``active_in`` matching. Without an explicit
        override, expose Decky's selected profile only as a fallback. The
        renderer checks executable/process matches first, so a profile captured
        while the game is running can replace this fallback without restarting.
        """
        if automatic_matching_enabled is None:
            automatic_matching_enabled = cls._has_active_in(config)

        matching_comment = (
            "# MAKO Renderer prefers active_in matches and uses this profile only as a fallback."
            if automatic_matching_enabled
            else "# Keep the default renderer context active so a newly captured profile can take over live."
        )
        return [
            matching_comment,
            "# A caller-provided MAKO_PROFILE remains an explicit hard override.",
            'if [ -z "${MAKO_PROFILE:-}" ]; then',
            f"    export MAKO_PROFILE_FALLBACK={shlex.quote(profile_name)}",
            "fi",
        ]

    def get_config(self) -> ConfigurationResponse:
        """Read current TOML configuration merged with launch script environment variables

        Returns:
            ConfigurationResponse with current configuration or error
        """
        try:
            self.migrate_wrapper_profile_settings_if_needed()
            profile_data = self._get_profile_data()
            config = self._config_for_profile(
                profile_data, profile_data["current_profile"]
            )

            return self._success_response(ConfigurationResponse, config=config)

        except (OSError, IOError) as e:
            error_msg = f"Error reading MAKO Renderer config: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(ConfigurationResponse, str(e), config=None)
        except Exception as e:
            error_msg = f"Error parsing config file: {str(e)}"
            self.log.error(error_msg)
            from .dll_detection import DllDetectionService
            dll_service = DllDetectionService(self.log)
            config = ConfigurationManager.get_defaults_with_dll_detection(dll_service)
            return self._success_response(ConfigurationResponse,
                                        f"Using default configuration due to parse error: {str(e)}",
                                        config=config)

    def get_profile_config(self, profile_name: str) -> ConfigurationResponse:
        """Read one saved profile without changing the runtime selection."""
        try:
            self.migrate_wrapper_profile_settings_if_needed()
            profile_data = self._get_profile_data()
            if profile_name not in profile_data["profiles"]:
                return self._error_response(
                    ConfigurationResponse,
                    f"Profile '{profile_name}' does not exist",
                    config=None,
                )

            config = self._config_for_profile(profile_data, profile_name)
            return self._success_response(
                ConfigurationResponse,
                f"Profile '{profile_name}' retrieved successfully",
                config=config,
            )
        except (OSError, IOError, ValueError, TypeError, json.JSONDecodeError) as error:
            self.log.error("Error reading profile '%s': %s", profile_name, error)
            return self._error_response(
                ConfigurationResponse,
                str(error),
                config=None,
            )

    def update_config_from_dict(self, config: ConfigurationData) -> ConfigurationResponse:
        """Update TOML configuration from configuration dictionary (eliminates parameter duplication)

        Args:
            config: Complete configuration data dictionary

        Returns:
            ConfigurationResponse with success status
        """
        try:
            profile_data = self._get_profile_data()
            current_profile = profile_data["current_profile"]

            return self.update_profile_config(current_profile, config)

        except (OSError, IOError) as e:
            error_msg = f"Error updating MAKO Renderer config: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(ConfigurationResponse, str(e), config=None)
        except ValueError as e:
            error_msg = f"Invalid configuration arguments: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(ConfigurationResponse, str(e), config=None)

    def update_mako_script(self, config: ConfigurationData) -> ConfigurationResponse:
        """Update the isolated per-game launch script with current configuration

        Args:
            config: Configuration data to apply to the script

        Returns:
            ConfigurationResponse indicating success or failure
        """
        try:
            script_content = self._generate_script_content(config)

            self._write_file(self.mako_script_path, script_content, 0o755)

            self.log.info(f"Updated MAKO launch script at {self.mako_script_path}")

            return self._success_response(ConfigurationResponse,
                                        "Launch script updated successfully",
                                        config=config)

        except Exception as e:
            error_msg = f"Error updating launch script: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(ConfigurationResponse, str(e), config=None)

    def remove_legacy_vkbasalt_exports(self) -> bool:
        """Remove obsolete plugin-managed vkBasalt exports.

        Layer discovery is now preserved, so a separately configured vkBasalt
        installation can work normally. Older plugin versions managed these
        variables directly; remove those stale exports during migration.
        """
        if not self.mako_script_path.exists():
            return False

        legacy_exports = {"DISABLE_VKBASALT", "ENABLE_VKBASALT"}
        existing_lines = self.mako_script_path.read_text(encoding="utf-8").splitlines()
        cleaned_lines = []
        removed = False

        for line in existing_lines:
            stripped = line.strip()
            if stripped.startswith("export "):
                variable = stripped[len("export "):].split("=", 1)[0].strip()
                if variable in legacy_exports:
                    removed = True
                    continue
            cleaned_lines.append(line)

        if removed:
            self._write_file(self.mako_script_path, "\n".join(cleaned_lines) + "\n", 0o755)
        return removed

    def enforce_unsupported_host_passthrough_if_needed(self) -> bool:
        """Replace an existing wrapper with a configuration-free safe bypass.

        This is called only after the installation service proves the packaged
        Renderer is incompatible with the native host. It runs before profile
        migrations so a stale pre-boundary wrapper can never expose MAKO while
        parsing or migration is failing.
        """
        if not self.mako_script_path.exists():
            return False

        lines = [
            "#!/bin/bash",
            self._WRAPPER_FORMAT_MARKER,
            self._HOST_COMPATIBILITY_MARKER,
            "# MAKO Renderer is unavailable for this native host in this release.",
        ]
        lines.extend(self._generate_unsupported_host_passthrough_lines())
        content = "\n".join(lines) + "\n"
        try:
            if self.mako_script_path.read_text(encoding="utf-8") == content:
                return False
        except OSError:
            pass
        self._write_file(self.mako_script_path, content, 0o755)
        return True

    def _generate_script_content(self, config: ConfigurationData) -> str:
        """Generate the content for the isolated per-game launch script

        Args:
            config: Configuration data to apply to the script

        Returns:
            The complete script content as a string
        """
        lines = [
            "#!/bin/bash",
            self._WRAPPER_FORMAT_MARKER,
            self._diagnostics_default_marker(),
            "# mako launch script generated by MAKO Decky",
            "# This script sets up the environment for mako to work with the plugin configuration",
        ]

        lines.extend(self._generate_host_compatibility_guard_lines())
        lines.extend(self._script_configuration_lines(config))
        lines.extend(self._generate_layer_environment_lines())
        lines.extend(self._profile_selection_lines(DEFAULT_PROFILE_NAME, config))
        lines.append('exec "$@"')

        return "\n".join(lines) + "\n"

    def _generate_script_content_for_profile(self, profile_data: ProfileData) -> str:
        """Generate the isolated per-game launch script with profile support

        Args:
            profile_data: Profile data containing current profile and configurations

        Returns:
            The complete script content as a string
        """
        current_profile = profile_data["current_profile"]
        fallback_profile = (
            DEFAULT_PROFILE_NAME
            if DEFAULT_PROFILE_NAME in profile_data["profiles"]
            else current_profile
        )
        fallback_config = self._config_for_profile(
            profile_data, fallback_profile
        )
        automatic_matching_enabled = any(
            self._has_active_in(profile_config)
            for profile_config in profile_data["profiles"].values()
        )

        lines = [
            "#!/bin/bash",
            self._WRAPPER_FORMAT_MARKER,
            self._diagnostics_default_marker(),
            f"# Current profile: {current_profile}",
        ]

        lines.extend(self._generate_host_compatibility_guard_lines())
        lines.extend(self._wrapper_profile_configuration_lines(profile_data))
        lines.extend(self._generate_layer_environment_lines())
        # A low-priority default keeps the layer active for an unsaved game.
        # Once capture adds Active In, the running process match supersedes it.
        lines.extend(self._profile_selection_lines(
            fallback_profile,
            fallback_config,
            automatic_matching_enabled,
        ))
        lines.append('exec "$@"')

        return "\n".join(lines) + "\n"

    def _wrapper_profile_configuration_lines(
            self, profile_data: ProfileData) -> list[str]:
        """Select launcher-only settings by explicit profile or Steam app ID."""
        current_profile = profile_data["current_profile"]
        profile_settings = self._read_wrapper_profile_settings()
        metadata = self._read_profile_metadata(profile_data)
        app_profiles = [
            (entry.get("steam_app_id"), profile_name)
            for profile_name, entry in metadata.items()
            if re.fullmatch(r"\d+", str(entry.get("steam_app_id") or ""))
        ]
        process_profiles = [
            (profile_name, self._processes_for_config(config))
            for profile_name, config in profile_data["profiles"].items()
            if profile_name != DEFAULT_PROFILE_NAME
            and self._processes_for_config(config)
        ]

        lines = [
            'mako_wrapper_profile="${MAKO_PROFILE:-}"',
            "mako_wrapper_profile_from_identity=0",
            'mako_wrapper_app_id="${SteamAppId:-${SteamGameId:-${STEAM_COMPAT_APP_ID:-}}}"',
            'if [ -z "$mako_wrapper_profile" ]; then',
            '    if [ -n "$mako_wrapper_app_id" ]; then',
            '        case "$mako_wrapper_app_id" in',
        ]
        for app_id, profile_name in app_profiles:
            lines.extend([
                f"            {app_id})",
                f"                mako_wrapper_profile={shlex.quote(profile_name)}",
                "                mako_wrapper_profile_from_identity=1",
                "                ;;",
            ])
        lines.extend([
            "            *)",
            f"                mako_wrapper_profile={shlex.quote(DEFAULT_PROFILE_NAME if DEFAULT_PROFILE_NAME in profile_data['profiles'] else current_profile)}",
            "                ;;",
            "        esac",
            "    else",
            '        case " $* " in',
        ])
        for profile_name, processes in process_profiles:
            patterns = "|".join(
                f"*{shlex.quote(process_name)}*" for process_name in processes
            )
            lines.extend([
                f"            {patterns})",
                f"                mako_wrapper_profile={shlex.quote(profile_name)}",
                "                mako_wrapper_profile_from_identity=1",
                "                ;;",
            ])
        lines.extend([
            "            *)",
            f"                mako_wrapper_profile={shlex.quote(current_profile)}",
            "                ;;",
            "        esac",
            "    fi",
            "fi",
            'case "$mako_wrapper_profile" in',
        ])

        for profile_name in profile_data["profiles"]:
            config = self._config_for_profile(
                profile_data, profile_name, profile_settings
            )
            lines.append(f"    {shlex.quote(profile_name)})")
            lines.extend(
                f"        {line}" for line in self._script_configuration_lines(config)
            )
            lines.append("        ;;")

        fallback_config = self._config_for_profile(
            profile_data, current_profile, profile_settings
        )
        lines.append("    *)")
        lines.extend(
            f"        {line}" for line in self._script_configuration_lines(fallback_config)
        )
        lines.extend([
            "        ;;",
            "esac",
            'if [ "$mako_wrapper_profile_from_identity" = "1" ]; then',
            '    export MAKO_PROFILE="$mako_wrapper_profile"',
            "fi",
        ])
        return lines

    @classmethod
    def _script_configuration_lines(cls, config: ConfigurationData) -> list[str]:
        """Generate wrapper settings without repeating forced compatibility exports."""
        lines = get_script_generation_logic()(config)
        for line in cls._hdr_activation_lines(config):
            if line not in lines:
                lines.append(line)
        return lines

    @staticmethod
    def _hdr_activation_lines(config: Dict[str, Any]) -> list[str]:
        """Keep the packaged Decky launcher on its proven SDR contract.

        The engine contains HDR colour-pipeline groundwork, but cross-game HDR
        activation and presentation are unavailable in the current Decky
        release. Remove inherited DXVK HDR exposure while MAKO enforces its
        supported SDR processing and presentation boundary.
        """
        del config
        return [
            f"export {HDR_EXPOSURE_DISABLE_ENV}=1",
            f"unset {DXVK_HDR_ENV}",
        ]

    def _generate_layer_environment_lines(self) -> list[str]:
        """Activate MAKO through its deterministic Vulkan discovery boundary.

        The same wrapper is used in Steam launch options and as Heroic's
        per-game wrapper command. Give the Vulkan loader one deterministic
        implicit-layer directory before it constructs the chain: the mounted
        MAKO extension in Flatpak, or Decky's private MAKO manifest directory
        on the host. This restores the v2 SDR boundary that is proven to
        intercept Wine's swapchain without Gamescope WSI, Steam's Vulkan
        Fossilize/overlay layers, or system-wide ordering changing the dispatch
        chain. The Gamescope compositor and Steam/Game Mode UI remain outside
        this application layer chain. The explicit LSFG, Gamescope, and HDR
        guards provide defence in depth.
        """
        diagnostics_log_path = self.config_dir / "present-diagnostics.log"
        return [
            f'export MAKO_PRESENT_ACQUIRE_TIMEOUT_MS="${{MAKO_PRESENT_ACQUIRE_TIMEOUT_MS:-{PRESENT_ACQUIRE_TIMEOUT_MS}}}"',
            # Presentation logging is intentionally opt-in for every build.
            # Slow-path records are synchronous and can distort the timing
            # problem being measured when a compositor is already congested.
            'export MAKO_PRESENT_DIAGNOSTICS="${MAKO_PRESENT_DIAGNOSTICS:-0}"',
            f"export {MAKO_LAYER_ENABLE_ENV}=1",
            *(f"export {variable}=1" for variable in COMPETING_LSFG_DISABLE_ENVS),
            f"export {GAMESCOPE_WSI_DISABLE_ENV}=1",
            f"unset {GAMESCOPE_WSI_ENABLE_ENV}",
            f"if [ -d {shlex.quote(FLATPAK_IMPLICIT_LAYER_DIR)} ]; then",
            f"    mako_implicit_layer_path={shlex.quote(FLATPAK_IMPLICIT_LAYER_DIR)}",
            "else",
            f"    mako_implicit_layer_path={shlex.quote(str(self.local_share_dir))}",
            "fi",
            'export VK_IMPLICIT_LAYER_PATH="$mako_implicit_layer_path"',
            "unset VK_ADD_IMPLICIT_LAYER_PATH",
            f"export MAKO_CONFIG={shlex.quote(str(self.config_file_path))}",
            "# Heroic can discard a game's stderr. Capture opt-in engine diagnostics here instead.",
            f"mako_diagnostics_default={shlex.quote(str(diagnostics_log_path))}",
            'if [ "${MAKO_PRESENT_DIAGNOSTICS:-0}" != "0" ]; then',
            '    mako_diagnostics_log="${MAKO_PRESENT_DIAGNOSTICS_LOG:-$mako_diagnostics_default}"',
            '    if : > "$mako_diagnostics_log" 2>/dev/null; then',
            '        exec 2>> "$mako_diagnostics_log"',
            "    fi",
            "fi",
        ]

    def migrate_launch_script_if_needed(self) -> bool:
        """Upgrade an installed generated wrapper without touching user data.

        Format 42 makes unsupported native AArch64 hosts an early passthrough:
        MAKO remains disabled while Armada's required game launcher is
        preserved. Format 41 restores v2's private SDR manifest directory on native Steam,
        preventing Steam's Vulkan Fossilize/overlay layers from bypassing MAKO's
        device and swapchain hooks. Format 40 briefly selected the standard
        per-user directory. Format 39 restores deterministic implicit-layer
        selection so MAKO remains the swapchain interceptor when Gamescope WSI
        is absent.
        Format 38 introduced the direct Gamescope presentation guard. It
        suppresses Gamescope WSI's conflicting upper FIFO policy while the
        regular compositor remains active. Format 37 restored the DXVK
        environment policy then in use.
        Format 36 makes high-volume presentation diagnostics opt-in in local
        development builds as well as published builds while preserving the
        Gamescope WSI and DXVK policies then in use. Format 35 keeps
        23.08/24.08 Flatpak loaders working while using
        additive discovery where the loader supports it. Format 34 preserves
        normal implicit Vulkan layers and disables only competing LSFG
        implementations. Format 33 keeps an unsaved game's
        renderer context active with a low-priority default profile so a newly
        captured process can take over live. Format 32 selects per-game
        compatibility settings from persistent Steam app identity before
        launch. Format 31 removes duplicate generated compatibility exports.
        Format 30 introduced a build-flavour-aware presentation-diagnostics
        default. Current wrappers keep every build quiet unless the caller
        explicitly enables diagnostics.
        Format 29 preserves a caller-provided profile for per-shortcut selection.
        Format 28 moved Base FPS Cap from the DXVK-only wrapper export into the
        engine. Regenerate any older or incomplete wrapper while retaining user
        profiles and rejecting obsolete environment exports.
        """
        if not self.mako_script_path.exists():
            return False

        try:
            current_content = self.mako_script_path.read_text(encoding="utf-8")
            wrapper_is_current = (
                self._WRAPPER_FORMAT_MARKER in current_content
                and self._HOST_COMPATIBILITY_MARKER in current_content
                and self._diagnostics_default_marker() in current_content
                and all(
                    export in current_content
                    for export in self._REQUIRED_WRAPPER_EXPORTS
                )
                and not any(
                    export in current_content
                    for export in self._OBSOLETE_WRAPPER_EXPORTS
                )
            )
            if wrapper_is_current:
                return False

            profile_data = self._get_profile_data()
            result = self.update_mako_script_from_profile_data(profile_data)
            if not result["success"]:
                raise OSError(result.get("error") or "could not refresh launch wrapper")

            self.log.info("Upgraded installed MAKO launch wrapper to format 42")
            return True
        except OSError:
            raise
        except Exception as error:
            raise OSError(f"Could not upgrade MAKO Renderer launch wrapper: {error}") from error

    @staticmethod
    def _generate_unsupported_host_passthrough_lines(
            indent: str = "") -> list[str]:
        """Disable MAKO and preserve Armada's launcher exactly once."""
        device_env = ARMADA_DEVICE_ENV.as_posix()
        game_launch = ARMADA_GAME_LAUNCH.as_posix()
        return [
            f"{indent}unset {MAKO_LAYER_ENABLE_ENV}",
            f"{indent}export {MAKO_LAYER_DISABLE_ENV}=1",
            f'{indent}armada_game_launch="{game_launch}"',
            f'{indent}if [ -f "{device_env}" ] && [ -x "$armada_game_launch" ]; then',
            f'{indent}    for argument in "$@"; do',
            f'{indent}        if [ "$argument" = "$armada_game_launch" ]; then',
            f'{indent}            exec "$@"',
            f"{indent}        fi",
            f"{indent}    done",
            f'{indent}    exec "$armada_game_launch" "$@"',
            f"{indent}fi",
            f'{indent}exec "$@"',
        ]

    @staticmethod
    def _generate_host_compatibility_guard_lines() -> list[str]:
        """Bypass MAKO before any exports on unsupported AArch64 hosts.

        Current release packages contain only native x86_64 Renderer payloads.
        Keep games launchable through Armada/FEX without exposing that x86
        layer to a native AArch64 Vulkan stack. The root-owned Armada marker
        handles translated ``uname`` results; the architecture check covers an
        ordinary native AArch64 shell. This branch executes before MAKO, Vulkan
        path, diagnostics, HDR, or competing-layer variables are changed.
        """
        device_env = ARMADA_DEVICE_ENV.as_posix()
        return [
            ConfigurationService._HOST_COMPATIBILITY_MARKER,
            'mako_native_arch="$(uname -m 2>/dev/null || true)"',
            f'if [ -f "{device_env}" ] || [ "$mako_native_arch" = "aarch64" ] || [ "$mako_native_arch" = "arm64" ]; then',
            "    # This release has no validated native AArch64 Renderer.",
            *ConfigurationService._generate_unsupported_host_passthrough_lines(
                "    "
            ),
            "fi",
        ]

    def _get_profile_data(self) -> ProfileData:
        """Get current profile data from config file"""
        if not self.config_file_path.exists():
            from .dll_detection import DllDetectionService
            dll_service = DllDetectionService(self.log)
            default_config = ConfigurationManager.get_defaults_with_dll_detection(dll_service)
            return ProfileData(
                current_profile=DEFAULT_PROFILE_NAME,
                profiles={DEFAULT_PROFILE_NAME: default_config},
                global_config={
                    "dll": default_config.get("dll", ""),
                    "allow_fp16": default_config.get("allow_fp16", True)
                }
            )

        content = self.config_file_path.read_text(encoding='utf-8')
        return ConfigurationManager.parse_toml_content_multi_profile(content)

    def _save_profile_data(self, profile_data: ProfileData) -> None:
        """Save profile data to config file"""
        toml_content = ConfigurationManager.generate_toml_content_multi_profile(profile_data)

        self.config_dir.mkdir(parents=True, exist_ok=True)

        self._write_file(self.config_file_path, toml_content, 0o644)

    def get_profiles(self) -> ProfilesResponse:
        """Get list of all profiles and current profile

        Returns:
            ProfilesResponse with profile list and current profile
        """
        try:
            profile_data = self._get_profile_data()
            self.migrate_profile_metadata_if_needed()
            metadata = self._read_profile_metadata(profile_data)

            return self._success_response(ProfilesResponse,
                                        "Profiles retrieved successfully",
                                        profiles=list(profile_data["profiles"].keys()),
                                        current_profile=profile_data["current_profile"],
                                        profile_details=self._profile_details(
                                            profile_data, metadata
                                        ))

        except Exception as e:
            error_msg = f"Error getting profiles: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(ProfilesResponse, str(e),
                                       profiles=None, current_profile=None,
                                       profile_details=None)

    def create_profile(self, profile_name: str, source_profile: str = None) -> ProfileResponse:
        """Create a new profile

        Args:
            profile_name: Name for the new profile (spaces will be converted to dashes)
            source_profile: Optional source profile to copy from (default: current profile)

        Returns:
            ProfileResponse with success status and the normalized profile name
        """
        try:
            self.migrate_wrapper_profile_settings_if_needed()
            profile_data = self._get_profile_data()
            self.migrate_profile_metadata_if_needed()
            metadata = self._read_profile_metadata(profile_data)

            if not source_profile:
                source_profile = profile_data["current_profile"]

            # Get the normalized name that will be used for storage
            normalized_name = ConfigurationManager.normalize_profile_name(profile_name)

            new_profile_data = ConfigurationManager.create_profile(profile_data, profile_name, source_profile)
            profile_settings = self._read_wrapper_profile_settings()
            profile_settings[normalized_name] = dict(
                self._wrapper_settings_for_profile(source_profile, profile_settings)
            )
            metadata[normalized_name] = {
                "display_name": profile_name.strip(),
                "kind": "process",
                "steam_app_id": None,
                "captured_processes": [],
            }
            self._save_profile_data(new_profile_data)
            self._write_wrapper_profile_settings(profile_settings)
            self._write_profile_metadata(metadata)

            self.log.info(f"Created profile '{normalized_name}' from '{source_profile}'")

            # Return the normalized name so frontend can use the actual stored name
            return self._success_response(ProfileResponse,
                                        f"Profile '{normalized_name}' created successfully",
                                        profile_name=normalized_name)

        except ValueError as e:
            error_msg = f"Invalid profile operation: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(ProfileResponse, str(e), profile_name=None)
        except Exception as e:
            error_msg = f"Error creating profile: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(ProfileResponse, str(e), profile_name=None)

    def delete_profile(self, profile_name: str) -> ProfileResponse:
        """Delete a profile

        Args:
            profile_name: Name of the profile to delete

        Returns:
            ProfileResponse with success status
        """
        try:
            self.migrate_wrapper_profile_settings_if_needed()
            profile_data = self._get_profile_data()
            self.migrate_profile_metadata_if_needed()
            metadata = self._read_profile_metadata(profile_data)
            profile_settings = self._read_wrapper_profile_settings()
            new_profile_data = ConfigurationManager.delete_profile(profile_data, profile_name)
            profile_settings.pop(profile_name, None)
            metadata.pop(profile_name, None)
            self._save_profile_data(new_profile_data)
            if self.wrapper_profile_settings_path.exists() or profile_settings:
                self._write_wrapper_profile_settings(profile_settings)
            self._write_profile_metadata(metadata)

            script_result = self.update_mako_script_from_profile_data(new_profile_data)
            if not script_result["success"]:
                self.log.warning(f"Failed to update launch script: {script_result['error']}")

            self.log.info(f"Deleted profile '{profile_name}'")

            return self._success_response(ProfileResponse,
                                        f"Profile '{profile_name}' deleted successfully",
                                        profile_name=profile_name,
                                        current_profile=new_profile_data["current_profile"])

        except ValueError as e:
            error_msg = f"Invalid profile operation: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(ProfileResponse, str(e), profile_name=None)
        except Exception as e:
            error_msg = f"Error deleting profile: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(ProfileResponse, str(e), profile_name=None)

    def rename_profile(self, old_name: str, new_name: str) -> ProfileResponse:
        """Rename a profile

        Args:
            old_name: Current profile name
            new_name: New profile name (spaces will be converted to dashes)

        Returns:
            ProfileResponse with success status and the normalized profile name
        """
        try:
            self.migrate_wrapper_profile_settings_if_needed()
            profile_data = self._get_profile_data()
            self.migrate_profile_metadata_if_needed()
            metadata = self._read_profile_metadata(profile_data)

            # Get the normalized name that will be used for storage
            normalized_name = ConfigurationManager.normalize_profile_name(new_name)

            new_profile_data = ConfigurationManager.rename_profile(profile_data, old_name, new_name)
            profile_settings = self._read_wrapper_profile_settings()
            if old_name in profile_settings:
                profile_settings[normalized_name] = profile_settings.pop(old_name)
            if old_name in metadata:
                metadata[normalized_name] = metadata.pop(old_name)
                metadata[normalized_name]["display_name"] = new_name.strip()
            self._save_profile_data(new_profile_data)
            if self.wrapper_profile_settings_path.exists() or profile_settings:
                self._write_wrapper_profile_settings(profile_settings)
            self._write_profile_metadata(metadata)

            script_result = self.update_mako_script_from_profile_data(new_profile_data)
            if not script_result["success"]:
                self.log.warning(f"Failed to update launch script: {script_result['error']}")

            self.log.info(f"Renamed profile '{old_name}' to '{normalized_name}'")

            # Return the normalized name so frontend can use the actual stored name
            return self._success_response(ProfileResponse,
                                        f"Profile renamed from '{old_name}' to '{normalized_name}' successfully",
                                        profile_name=normalized_name)

        except ValueError as e:
            error_msg = f"Invalid profile operation: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(ProfileResponse, str(e), profile_name=None)
        except Exception as e:
            error_msg = f"Error renaming profile: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(ProfileResponse, str(e), profile_name=None)

    def capture_game_profile(
            self,
            app_id: str,
            display_name: str,
            source_profile: str = None,
    ) -> ProfileResponse:
        """Create or refresh a persistent profile for one running Steam app."""
        normalized_app_id = str(app_id).strip()
        friendly_name = str(display_name).strip()
        if not re.fullmatch(r"\d+", normalized_app_id) or not friendly_name:
            return self._error_response(
                ProfileResponse,
                "The running game identity is incomplete",
                profile_name=None,
                profile=None,
            )

        processes = [
            process_name
            for process_name in detect_processes_for_steam_app(normalized_app_id)
            if is_matchable_process_name(process_name)
        ]
        if not processes:
            return self._error_response(
                ProfileResponse,
                "No game process was detected yet. Wait until gameplay has loaded, then try again.",
                profile_name=None,
                profile=None,
            )

        try:
            self.migrate_wrapper_profile_settings_if_needed()
            profile_data = self._get_profile_data()
            self.migrate_profile_metadata_if_needed()
            metadata = self._read_profile_metadata(profile_data)
            profile_settings = self._read_wrapper_profile_settings()

            target_profile = next((
                profile_name
                for profile_name, entry in metadata.items()
                if entry.get("steam_app_id") == normalized_app_id
            ), None)

            detected_names = {name.casefold() for name in processes}
            if target_profile is None:
                for profile_name, config in profile_data["profiles"].items():
                    if profile_name == DEFAULT_PROFILE_NAME:
                        continue
                    configured_names = {
                        name.casefold() for name in self._processes_for_config(config)
                    }
                    if configured_names & detected_names:
                        target_profile = profile_name
                        break

            created = target_profile is None
            if created:
                source = (
                    source_profile
                    if source_profile in profile_data["profiles"]
                    else profile_data["current_profile"]
                )
                base_name = ConfigurationManager.normalize_profile_name(friendly_name)
                if not ConfigurationManager.validate_profile_name(base_name):
                    base_name = f"game-{normalized_app_id}"
                if base_name == DEFAULT_PROFILE_NAME:
                    base_name = f"{base_name}-game"
                target_profile = base_name
                suffix = 2
                while target_profile in profile_data["profiles"]:
                    target_profile = f"{base_name}-{suffix}"
                    suffix += 1
                profile_data = ConfigurationManager.create_profile(
                    profile_data, target_profile, source
                )
                profile_settings[target_profile] = dict(
                    self._wrapper_settings_for_profile(source, profile_settings)
                )

            existing_processes = self._processes_for_config(
                profile_data["profiles"][target_profile]
            )
            previous_captured = {
                name.casefold()
                for name in metadata.get(target_profile, {}).get(
                    "captured_processes", []
                )
            }
            # Refresh automatically captured identities, while retaining any
            # aliases the user added manually in Matched Processes.
            merged_processes = [
                name for name in existing_processes
                if name.casefold() not in previous_captured
            ]
            known = {name.casefold() for name in merged_processes}
            for process_name in processes:
                if process_name.casefold() not in known:
                    merged_processes.append(process_name)
                    known.add(process_name.casefold())
            profile_data["profiles"][target_profile]["active_in"] = ", ".join(
                merged_processes
            )
            profile_data = ConfigurationManager.set_current_profile(
                profile_data, target_profile
            )
            metadata[target_profile] = {
                "display_name": friendly_name,
                "kind": "game",
                "steam_app_id": normalized_app_id,
                "captured_processes": processes,
            }

            self._save_profile_data(profile_data)
            self._write_wrapper_profile_settings(profile_settings)
            self._write_profile_metadata(metadata)
            script_result = self.update_mako_script_from_profile_data(profile_data)
            if not script_result["success"]:
                raise OSError(script_result.get("error") or "could not update launch wrapper")

            detail = next(
                item for item in self._profile_details(profile_data, metadata)
                if item["profile_name"] == target_profile
            )
            action = "created" if created else "updated"
            return self._success_response(
                ProfileResponse,
                f"Profile for '{friendly_name}' {action} successfully",
                profile_name=target_profile,
                profile=detail,
            )
        except (OSError, IOError, ValueError, TypeError, json.JSONDecodeError) as error:
            self.log.error("Error capturing game profile: %s", error)
            return self._error_response(
                ProfileResponse,
                str(error),
                profile_name=None,
                profile=None,
            )

    def set_current_profile(self, profile_name: str) -> ProfileResponse:
        """Set the current active profile

        Args:
            profile_name: Name of the profile to set as current

        Returns:
            ProfileResponse with success status
        """
        try:
            self.migrate_wrapper_profile_settings_if_needed()
            profile_data = self._get_profile_data()

            new_profile_data = ConfigurationManager.set_current_profile(profile_data, profile_name)

            self._save_profile_data(new_profile_data)

            script_result = self.update_mako_script_from_profile_data(new_profile_data)
            if not script_result["success"]:
                self.log.warning(f"Failed to update launch script: {script_result['error']}")

            self.log.info(f"Set current profile to '{profile_name}'")

            return self._success_response(ProfileResponse,
                                        f"Current profile set to '{profile_name}' successfully",
                                        profile_name=profile_name)

        except ValueError as e:
            error_msg = f"Invalid profile operation: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(ProfileResponse, str(e), profile_name=None)
        except Exception as e:
            error_msg = f"Error setting current profile: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(ProfileResponse, str(e), profile_name=None)

    def sync_current_profile(self, app_id: str = "") -> ProfileResponse:
        """Select the saved profile matching a live Steam app process.

        A Steam running-app record can outlive its game process briefly. Never
        retain or select a game profile from the app ID alone: require at least
        one live process carrying that ID, then prefer the previously captured
        app profile and fall back to matching its configured process aliases.
        With no live match, restore the default profile.
        """
        try:
            self.migrate_wrapper_profile_settings_if_needed()
            profile_data = self._get_profile_data()
            self.migrate_profile_metadata_if_needed()
            metadata = self._read_profile_metadata(profile_data)

            normalized_app_id = str(app_id or "").strip()
            detected_processes = []
            if re.fullmatch(r"\d+", normalized_app_id) and normalized_app_id != "0":
                detected_processes = detect_processes_for_steam_app(
                    normalized_app_id
                )

            target_profile = DEFAULT_PROFILE_NAME
            if detected_processes:
                target_profile = next((
                    profile_name
                    for profile_name, entry in metadata.items()
                    if profile_name != DEFAULT_PROFILE_NAME
                    and entry.get("steam_app_id") == normalized_app_id
                ), None)

                if target_profile is None:
                    detected_names = {
                        process_name.casefold()
                        for process_name in detected_processes
                    }
                    target_profile = next((
                        profile_name
                        for profile_name, config in profile_data["profiles"].items()
                        if profile_name != DEFAULT_PROFILE_NAME
                        and not metadata.get(profile_name, {}).get("steam_app_id")
                        and detected_names & {
                            process_name.casefold()
                            for process_name in self._processes_for_config(config)
                        }
                    ), DEFAULT_PROFILE_NAME)

            changed = profile_data["current_profile"] != target_profile
            if changed:
                profile_data = ConfigurationManager.set_current_profile(
                    profile_data, target_profile
                )
                self._save_profile_data(profile_data)
                script_result = self.update_mako_script_from_profile_data(
                    profile_data
                )
                if not script_result["success"]:
                    raise OSError(
                        script_result.get("error")
                        or "could not update launch wrapper"
                    )
                self.log.info(
                    "Automatically selected profile '%s' for app '%s'",
                    target_profile,
                    normalized_app_id or "none",
                )

            detail = next(
                item for item in self._profile_details(profile_data, metadata)
                if item["profile_name"] == target_profile
            )
            return self._success_response(
                ProfileResponse,
                (
                    f"Selected profile '{target_profile}'"
                    if changed
                    else f"Profile '{target_profile}' is already selected"
                ),
                profile_name=target_profile,
                profile=detail,
                changed=changed,
                game_running=bool(detected_processes),
            )
        except (OSError, IOError, ValueError, TypeError, json.JSONDecodeError) as error:
            self.log.error("Error synchronising current profile: %s", error)
            return self._error_response(
                ProfileResponse,
                str(error),
                profile_name=None,
                profile=None,
                changed=False,
                game_running=None,
            )

    def update_profile_config(self, profile_name: str, config: ConfigurationData) -> ConfigurationResponse:
        """Update configuration for a specific profile

        Args:
            profile_name: Name of the profile to update
            config: Configuration data to apply

        Returns:
            ConfigurationResponse with success status
        """
        try:
            self.migrate_wrapper_profile_settings_if_needed()
            profile_data = self._get_profile_data()

            if profile_name not in profile_data["profiles"]:
                return self._error_response(ConfigurationResponse,
                                          f"Profile '{profile_name}' does not exist",
                                          config=None)

            # Update the profile's config
            profile_data["profiles"][profile_name] = config

            # Update global config fields if they're in the config
            for field_name in ["dll", "allow_fp16"]:
                if field_name in config:
                    profile_data["global_config"][field_name] = config[field_name]
            profile_settings = self._read_wrapper_profile_settings()
            profile_settings[profile_name] = self._normalize_wrapper_settings(config)
            self._save_profile_data(profile_data)
            self._write_wrapper_profile_settings(profile_settings)

            # The wrapper embeds compatibility settings for every saved
            # profile, not only the currently active renderer profile.
            script_result = self.update_mako_script_from_profile_data(profile_data)
            if not script_result["success"]:
                self.log.warning(f"Failed to update launch script: {script_result['error']}")

            field_values = ", ".join(f"{k}={repr(v)}" for k, v in config.items())
            self.log.info(f"Updated profile '{profile_name}' configuration: {field_values}")

            return self._success_response(ConfigurationResponse,
                                        f"Profile '{profile_name}' configuration updated successfully",
                                        config=config)

        except Exception as e:
            error_msg = f"Error updating profile configuration: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(ConfigurationResponse, str(e), config=None)

    def update_mako_script_from_profile_data(self, profile_data: ProfileData) -> ConfigurationResponse:
        """Update the isolated per-game launch script from profile data

        Args:
            profile_data: Profile data to apply to the script

        Returns:
            ConfigurationResponse indicating success or failure
        """
        try:
            script_content = self._generate_script_content_for_profile(profile_data)

            # Write the script file
            self._write_file(self.mako_script_path, script_content, 0o755)

            self.log.info(f"Updated MAKO launch script at {self.mako_script_path} for profile '{profile_data['current_profile']}'")

            # Get current profile config for response
            current_config = self._config_for_profile(
                profile_data, profile_data["current_profile"]
            )

            return self._success_response(ConfigurationResponse,
                                        "Launch script updated successfully",
                                        config=current_config)

        except Exception as e:
            error_msg = f"Error updating launch script: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(ConfigurationResponse, str(e), config=None)
