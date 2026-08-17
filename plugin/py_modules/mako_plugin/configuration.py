"""Configuration service for MAKO Renderer TOML and Decky profiles."""

import json
from pathlib import Path
import shlex
from typing import Dict, Any

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
    DISABLE_LSFGVK,
    DISABLE_HDR_EXPOSURE,
    get_script_generation_logic,
)
from .constants import (
    ARMADA_DEVICE_ENV,
    ARMADA_GAME_LAUNCH,
    MAKO_LAYER_ENABLE_ENV,
    FLATPAK_GAMESCOPE_IMPLICIT_LAYER_DIR,
    FLATPAK_IMPLICIT_LAYER_DIR,
    PRESENT_ACQUIRE_TIMEOUT_MS,
)
from .types import ConfigurationResponse, ProfilesResponse, ProfileResponse


class ConfigurationService(BaseService):
    """Service for managing MAKO Renderer TOML configuration."""

    _WRAPPER_FORMAT_MARKER = "# mako-wrapper-format: 27"
    _WRAPPER_PROFILE_SETTINGS_VERSION = 1
    _REQUIRED_WRAPPER_EXPORTS = (
        "export MAKO_PRESENT_ACQUIRE_TIMEOUT_MS=",
        f"export {MAKO_LAYER_ENABLE_ENV}=1",
        "export DISABLE_LSFGVK=1",
        "export DISABLE_LSFG=1",
        "mako_diagnostics_default=",
    )
    _OBSOLETE_WRAPPER_EXPORTS = (
        "PROTON_USE_WOW64",
        "MAKO_PRESENT_RECOVERY_RECREATE",
        "LSFGVK_PRESENT_RECOVERY_RECREATE",
        "MAKO_EXPERIMENTAL_HDR",
        "LSFGVK_EXPERIMENTAL_HDR",
        "VK_INSTANCE_LAYERS",
        "VK_LAYER_DECKY_LSFGVK_experimental_hdr_stack_x86_64",
    )

    @staticmethod
    def _wrapper_settings_defaults() -> Dict[str, Any]:
        return {
            field_name: CONFIG_SCHEMA[field_name].default
            for field_name in SCRIPT_ONLY_FIELDS
        }

    @staticmethod
    def _normalize_wrapper_settings(raw_settings: Dict[str, Any]) -> Dict[str, Any]:
        """Validate Decky-only wrapper settings without polluting engine TOML."""
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
            script_values = ConfigurationManager.parse_script_content(
                self.mako_script_path.read_text(encoding="utf-8")
            )
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
        """Choose between Decky's selected profile and automatic matching.

        ``MAKO_PROFILE`` deliberately overrides MAKO Renderer's ``active_in`` matching.
        Keep Decky's selected-profile behaviour for profiles without activation rules,
        but let MAKO Renderer perform its native automatic selection when rules are present.
        """
        if automatic_matching_enabled is None:
            automatic_matching_enabled = cls._has_active_in(config)

        if automatic_matching_enabled:
            return [
                "# An active_in profile is configured; MAKO Renderer will select a matching profile automatically.",
            ]
        return [f"export MAKO_PROFILE={shlex.quote(profile_name)}"]

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
            "# MAKO Renderer launch script generated by mako plugin",
            "# This script sets up the environment for MAKO Renderer to work with the plugin configuration",
        ]

        generate_script_lines = get_script_generation_logic()
        lines.extend(generate_script_lines(config))
        lines.extend(self._experimental_hdr_activation_lines(config))
        lines.extend(self._generate_layer_environment_lines())
        lines.extend(self._profile_selection_lines(DEFAULT_PROFILE_NAME, config))
        lines.extend(self._generate_game_launch_lines())

        return "\n".join(lines) + "\n"

    def _generate_script_content_for_profile(self, profile_data: ProfileData) -> str:
        """Generate the isolated per-game launch script with profile support

        Args:
            profile_data: Profile data containing current profile and configurations

        Returns:
            The complete script content as a string
        """
        current_profile = profile_data["current_profile"]
        merged_config = self._config_for_profile(profile_data, current_profile)
        automatic_matching_enabled = any(
            self._has_active_in(profile_config)
            for profile_config in profile_data["profiles"].values()
        )

        lines = [
            "#!/bin/bash",
            self._WRAPPER_FORMAT_MARKER,
            f"# Current profile: {current_profile}",
        ]

        generate_script_lines = get_script_generation_logic()
        lines.extend(generate_script_lines(merged_config))
        lines.extend(self._experimental_hdr_activation_lines(merged_config))
        lines.extend(self._generate_layer_environment_lines())
        # Never export MAKO_PROFILE once any profile uses Active In: the
        # environment override takes precedence over upstream's executable
        # detection and would otherwise make profiles depend on the UI's last
        # selected entry.
        lines.extend(self._profile_selection_lines(
            current_profile,
            merged_config,
            automatic_matching_enabled,
        ))
        lines.extend(self._generate_game_launch_lines())

        return "\n".join(lines) + "\n"

    @staticmethod
    def _experimental_hdr_activation_lines(config: Dict[str, Any]) -> list[str]:
        """Keep the packaged Decky launcher on its proven SDR contract.

        The engine contains HDR colour-pipeline groundwork, but cross-game HDR
        activation and presentation are not release-ready. Ignore stale profile
        opt-ins until a later Decky release deliberately unlocks this boundary.
        """
        del config
        return [
            "export MAKO_DISABLE_HDR_EXPOSURE=1",
            # Absence is DXVK's established SDR default. Do not replace the
            # launcher's normal Gamescope WSI contract while blocking MAKO's
            # unfinished HDR path.
            "unset DXVK_HDR",
        ]

    def _generate_layer_environment_lines(self) -> list[str]:
        """Activate only the registered MAKO Renderer layer for this game.

        The same wrapper is used in Steam launch options and as Heroic's
        per-game wrapper command. Host launches use the uniquely named, gated
        manifest installed in Vulkan's normal per-user directory so Pressure
        Vessel can register it before this wrapper starts. Heroic's UMU launch
        path rebuilds the child's Vulkan manifest search from an explicit
        override; use the mounted Flatpak extension as that override so the
        MAKO Renderer layer survives into the game process. The HDR exposure
        boundary is enforced separately.
        """
        diagnostics_log_path = self.config_dir / "present-diagnostics.log"
        return [
            f'export MAKO_PRESENT_ACQUIRE_TIMEOUT_MS="${{MAKO_PRESENT_ACQUIRE_TIMEOUT_MS:-{PRESENT_ACQUIRE_TIMEOUT_MS}}}"',
            f"export {MAKO_LAYER_ENABLE_ENV}=1",
            "export DISABLE_LSFGVK=1",
            "export DISABLE_LSFG=1",
            f"if [ -d {shlex.quote(FLATPAK_IMPLICIT_LAYER_DIR)} ]; then",
            f"    mako_implicit_layer_path={shlex.quote(FLATPAK_IMPLICIT_LAYER_DIR)}",
            f"    if [ -d {shlex.quote(FLATPAK_GAMESCOPE_IMPLICIT_LAYER_DIR)} ]; then",
            "        # Gamescope must stay above MAKO Renderer in Heroic's Vulkan chain.",
            f"        mako_implicit_layer_path={shlex.quote(FLATPAK_GAMESCOPE_IMPLICIT_LAYER_DIR)}:\"$mako_implicit_layer_path\"",
            "    fi",
            'elif [ "${MAKO_DISABLE_HDR_EXPOSURE:-0}" != "0" ]; then',
            f"    mako_implicit_layer_path={shlex.quote(str(self.local_share_dir))}",
            "else",
            '    mako_implicit_layer_path=""',
            "fi",
            'if [ "${MAKO_DISABLE_HDR_EXPOSURE:-0}" != "0" ]; then',
            '    export VK_IMPLICIT_LAYER_PATH="$mako_implicit_layer_path"',
            '    unset VK_ADD_IMPLICIT_LAYER_PATH',
            'elif [ -z "$mako_implicit_layer_path" ]; then',
            "    : # Host manifest is registered before Pressure Vessel starts.",
            'elif [ -n "${VK_IMPLICIT_LAYER_PATH:-}" ]; then',
            '    export VK_IMPLICIT_LAYER_PATH="$mako_implicit_layer_path:$VK_IMPLICIT_LAYER_PATH"',
            'elif [ -n "${VK_ADD_IMPLICIT_LAYER_PATH:-}" ]; then',
            '    export VK_ADD_IMPLICIT_LAYER_PATH="$mako_implicit_layer_path:$VK_ADD_IMPLICIT_LAYER_PATH"',
            "else",
            '    export VK_ADD_IMPLICIT_LAYER_PATH="$mako_implicit_layer_path"',
            "fi",
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

        Wrapper format 27 keeps Heroic's Gamescope WSI manifest and activation
        contract while using the explicit Flatpak search path required to carry
        LSFG through UMU. Format 26 restored LSFG attachment but still removed
        Gamescope from ordinary SDR launches. Format 25 used
        VK_ADD_IMPLICIT_LAYER_PATH, but Heroic's UMU child did not retain that
        addition and frame generation never loaded. HDR remains blocked by the
        separate restart-time SDR boundary.
        Formats 19 to 22
        attempted to order components through VK_INSTANCE_LAYERS or a Vulkan
        meta-layer and could leave Gamescope or LSFG unattached.
        Format 15 forced only LSFG and could break Wine swapchain dispatch;
        marker validation therefore still regenerates it. Formats 16 through
        18 returned to implicit discovery, which worked only when the loader
        happened to choose the required Gamescope -> LSFG order. Format 14
        activated a uniquely named, wrapper-scoped experimental
        manifest from Vulkan's normal per-user registry. It disables both
        public LSFG layer identities for this game and no longer relies on
        additive search ordering that Pressure Vessel resolves before the
        wrapper starts. Format 13 removed the obsolete PROTON_USE_WOW64 export
        now
        that the engine ships architecture-matched Vulkan layers. Format 12
        added an opt-in legacy-isolation recovery path for
        games that cannot start when Gamescope advertises HDR. Format 11 added
        the private experimental manifest ahead of the normal implicit-layer
        search path instead of replacing that path. Format 14 supersedes that
        ordering-based selection while preserving Gamescope WSI discovery for
        HDR-capable games. It
        Format 17 retains format 10's automatic Active In matching, selected-profile
        compatibility settings, plugin-private diagnostics log, in-place
        presentation recovery, explicit caller overrides, validated 50 ms
        acquisition timeout, and experimental Flatpak manifest selection.
        Format 17 removes the obsolete layer-initiated swapchain-recreation
        export. Validate the required exports as well as the marker so an
        intermediate locally generated wrapper cannot be mistaken for the
        completed format.
        """
        if not self.mako_script_path.exists():
            return False

        try:
            current_content = self.mako_script_path.read_text(encoding="utf-8")
            wrapper_is_current = (
                self._WRAPPER_FORMAT_MARKER in current_content
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

            self.log.info("Upgraded installed MAKO Renderer experimental launch wrapper to format 27")
            return True
        except OSError:
            raise
        except Exception as error:
            raise OSError(f"Could not upgrade MAKO Renderer experimental launch wrapper: {error}") from error

    @staticmethod
    def _generate_game_launch_lines() -> list[str]:
        """Preserve Armada's required host launcher when running under FEX.

        This is intentionally host-gated, so ordinary SteamOS installs retain
        the normal direct ``exec`` path.  The argument scan also avoids adding
        the wrapper twice when a user already has it in Steam launch options.
        """
        device_env = ARMADA_DEVICE_ENV.as_posix()
        game_launch = ARMADA_GAME_LAUNCH.as_posix()
        return [
            f'armada_game_launch="{game_launch}"',
            'for argument in "$@"; do',
            '    if [ "$argument" = "$armada_game_launch" ]; then',
            '        exec "$@"',
            "    fi",
            "done",
            f'if [ -f "{device_env}" ] && [ -x "$armada_game_launch" ]; then',
            '    exec "$armada_game_launch" "$@"',
            "fi",
            'exec "$@"',
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

            return self._success_response(ProfilesResponse,
                                        "Profiles retrieved successfully",
                                        profiles=list(profile_data["profiles"].keys()),
                                        current_profile=profile_data["current_profile"])

        except Exception as e:
            error_msg = f"Error getting profiles: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(ProfilesResponse, str(e),
                                       profiles=None, current_profile=None)

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

            if not source_profile:
                source_profile = profile_data["current_profile"]

            # Get the normalized name that will be used for storage
            normalized_name = ConfigurationManager.normalize_profile_name(profile_name)

            new_profile_data = ConfigurationManager.create_profile(profile_data, profile_name, source_profile)
            profile_settings = self._read_wrapper_profile_settings()
            profile_settings[normalized_name] = dict(
                self._wrapper_settings_for_profile(source_profile, profile_settings)
            )
            self._save_profile_data(new_profile_data)
            self._write_wrapper_profile_settings(profile_settings)

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
            profile_settings = self._read_wrapper_profile_settings()
            new_profile_data = ConfigurationManager.delete_profile(profile_data, profile_name)
            profile_settings.pop(profile_name, None)
            self._save_profile_data(new_profile_data)
            if self.wrapper_profile_settings_path.exists() or profile_settings:
                self._write_wrapper_profile_settings(profile_settings)

            script_result = self.update_mako_script_from_profile_data(new_profile_data)
            if not script_result["success"]:
                self.log.warning(f"Failed to update launch script: {script_result['error']}")

            self.log.info(f"Deleted profile '{profile_name}'")

            return self._success_response(ProfileResponse,
                                        f"Profile '{profile_name}' deleted successfully",
                                        profile_name=profile_name)

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

            # Get the normalized name that will be used for storage
            normalized_name = ConfigurationManager.normalize_profile_name(new_name)

            new_profile_data = ConfigurationManager.rename_profile(profile_data, old_name, new_name)
            profile_settings = self._read_wrapper_profile_settings()
            if old_name in profile_settings:
                profile_settings[normalized_name] = profile_settings.pop(old_name)
            self._save_profile_data(new_profile_data)
            if self.wrapper_profile_settings_path.exists() or profile_settings:
                self._write_wrapper_profile_settings(profile_settings)

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

            if profile_name == profile_data["current_profile"]:
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
