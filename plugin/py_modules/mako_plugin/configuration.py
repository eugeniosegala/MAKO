"""Configuration service for MAKO Renderer TOML and Decky profiles."""

import json
import re
from pathlib import Path
from threading import RLock
from typing import Dict, Any, Optional

from .build_flavor import LOCAL_DEVELOPMENT_BUILD
from .base_service import BaseService
from .config_schema import (
    ConfigurationManager,
    ProfileData,
    DEFAULT_PROFILE_NAME,
    PROFILE_KIND_GAME,
    PROFILE_KIND_PROCESS,
)
from .config_schema_generated import (
    ConfigurationData,
    ConfigurationPatch,
)
from .constants import (
    ARMADA_DEVICE_ENV,
    ARMADA_GAME_LAUNCH,
    FLATPAK_IMPLICIT_LAYER_DIR,
    GAMESCOPE_WSI_MANIFEST_FILENAME_64,
    SPATIAL_SCALING_JSON_FILENAME,
    MANGOHUD_MANIFEST_FILENAME_64,
    MANGOHUD_MANIFEST_FILENAME_32,
    VKBASALT_MANIFEST_FILENAME_64,
    VKBASALT_MANIFEST_FILENAME_32,
)
from .managed_files import write_managed_text_atomically
from .process_detection import (
    detect_processes_for_steam_app,
    is_matchable_process_name,
)
from . import profile_storage
from . import wrapper_generation
from .types import ConfigurationResponse, ProfilesResponse, ProfileResponse


class ConfigurationService(BaseService):
    """Service for managing MAKO Renderer TOML configuration."""

    _WRAPPER_FORMAT_VERSION = wrapper_generation.WRAPPER_FORMAT_VERSION
    _WRAPPER_FORMAT_MARKER = wrapper_generation.WRAPPER_FORMAT_MARKER
    _HOST_COMPATIBILITY_MARKER = (
        wrapper_generation.HOST_COMPATIBILITY_MARKER
    )
    _WRAPPER_PROFILE_SETTINGS_VERSION = 1
    _PROFILE_METADATA_VERSION = 1
    _REQUIRED_WRAPPER_EXPORTS = wrapper_generation.REQUIRED_WRAPPER_EXPORTS
    _VKBASALT_SHADER_ASSET_DIR = Path(__file__).with_name("vkbasalt_shaders")
    _VKBASALT_SHADER_ASSETS = (
        "BleachBypass.fx",
        "Cartoon.fx",
        "ChromaticAberration.fx",
        "Colourfulness.fx",
        "Curves.fx",
        "DPX.fx",
        "FakeHDR.fx",
        "FilmGrain.fx",
        "LICENSE-Colourfulness",
        "LICENSE-SweetFX",
        "Monochrome.fx",
        "Noir.fx",
        "Nostalgia.fx",
        "ReShade.fxh",
        "Sepia.fx",
        "SOURCE.md",
        "Technicolor.fx",
        "Technicolor2.fx",
        "Vibrance.fx",
        "Vignette.fx",
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
        self._configuration_write_lock = RLock()

    def _diagnostics_default_marker(self) -> str:
        return wrapper_generation.DIAGNOSTICS_DEFAULT_MARKER

    def _wrapper_generation_context(
            self) -> wrapper_generation.WrapperGenerationContext:
        """Capture current managed paths for one pure wrapper-generation call."""
        return wrapper_generation.WrapperGenerationContext(
            wrapper_format_marker=self._WRAPPER_FORMAT_MARKER,
            host_compatibility_marker=self._HOST_COMPATIBILITY_MARKER,
            diagnostics_default_marker=self._diagnostics_default_marker(),
            config_dir=self.config_dir,
            config_file_path=self.config_file_path,
            local_share_dir=self.local_share_dir,
            user_vulkan_layer_dir=self.user_vulkan_layer_dir,
            spatial_scaling_layer_dir=self.spatial_scaling_layer_dir,
            gamescope_wsi_compatibility_dir=(
                self.gamescope_wsi_compatibility_dir
            ),
            mangohud_layer_dir=self.mangohud_layer_dir,
            vkbasalt_layer_dir=self.vkbasalt_layer_dir,
            vkbasalt_global_config_path=self.vkbasalt_global_config_path,
            vkbasalt_profile_config_dir=self.vkbasalt_profile_config_dir,
            flatpak_implicit_layer_dir=FLATPAK_IMPLICIT_LAYER_DIR,
            gamescope_wsi_manifest_filename_64=(
                GAMESCOPE_WSI_MANIFEST_FILENAME_64
            ),
            spatial_scaling_manifest_filename_64=(
                SPATIAL_SCALING_JSON_FILENAME
            ),
            mangohud_manifest_filename_64=MANGOHUD_MANIFEST_FILENAME_64,
            mangohud_manifest_filename_32=MANGOHUD_MANIFEST_FILENAME_32,
            vkbasalt_manifest_filename_64=VKBASALT_MANIFEST_FILENAME_64,
            vkbasalt_manifest_filename_32=VKBASALT_MANIFEST_FILENAME_32,
            armada_device_env=ARMADA_DEVICE_ENV,
            armada_game_launch=ARMADA_GAME_LAUNCH,
        )

    @staticmethod
    def _wrapper_settings_defaults() -> profile_storage.WrapperSettingsData:
        return profile_storage.wrapper_settings_defaults()

    @staticmethod
    def _normalize_wrapper_settings(
            raw_settings: Dict[str, Any],
    ) -> profile_storage.WrapperSettingsData:
        """Allowlist current wrapper settings without polluting engine TOML.

        Removed and unknown fields are intentionally discarded so profile data
        can never create an environment export unless the current schema and
        wrapper generator both support it.
        """
        return profile_storage.normalize_wrapper_settings(raw_settings)

    def _read_wrapper_profile_settings(
            self,
    ) -> profile_storage.WrapperProfileSettings:
        """Read persisted per-profile launcher settings, falling back safely."""
        return profile_storage.read_wrapper_profile_settings(
            self.wrapper_profile_settings_path,
            self._WRAPPER_PROFILE_SETTINGS_VERSION,
            self.log,
            self._normalize_wrapper_settings,
        )

    def _write_wrapper_profile_settings(
            self,
            profile_settings: profile_storage.WrapperProfileSettings,
            metadata: Optional[profile_storage.ProfileMetadata] = None,
    ) -> None:
        resolved_metadata = metadata
        if resolved_metadata is None:
            profile_data = self._get_profile_data()
            resolved_metadata = self._read_profile_metadata(profile_data)
        normalized_profiles = {
            profile_name: self._normalize_wrapper_settings(settings)
            for profile_name, settings in profile_settings.items()
        }
        expected_profile_configs = self._write_vkbasalt_profile_configs(
            normalized_profiles,
            resolved_metadata,
        )
        profile_storage.write_wrapper_profile_settings(
            self.config_dir,
            self.wrapper_profile_settings_path,
            self._WRAPPER_PROFILE_SETTINGS_VERSION,
            normalized_profiles,
            self._write_file,
            self._normalize_wrapper_settings,
        )
        self._remove_stale_vkbasalt_profile_configs(expected_profile_configs)

    def _write_vkbasalt_profile_configs(
            self,
            profile_settings: profile_storage.WrapperProfileSettings,
            metadata: Optional[profile_storage.ProfileMetadata] = None,
    ) -> set[str]:
        """Merge enabled configs while retaining files for existing profiles."""
        resolved_metadata = metadata or {}
        if any(profile_storage.uses_vkbasalt(value) for value in profile_settings.values()):
            self._write_vkbasalt_shader_assets()
        expected_names = {
            profile_storage.vkbasalt_profile_config_filename(
                profile_name,
                profile_storage.metadata_steam_app_id(
                    resolved_metadata,
                    profile_name,
                ),
            )
            for profile_name in profile_settings
            if profile_name != DEFAULT_PROFILE_NAME
        }
        for profile_name, settings in profile_settings.items():
            steam_app_id = profile_storage.metadata_steam_app_id(
                resolved_metadata,
                profile_name,
            )
            if not profile_storage.uses_vkbasalt(settings):
                self._migrate_vkbasalt_profile_config(
                    profile_name,
                    steam_app_id,
                )
                continue
            config_path = profile_storage.vkbasalt_config_path(
                profile_name,
                self.vkbasalt_global_config_path,
                self.vkbasalt_profile_config_dir,
                steam_app_id,
            )
            self._migrate_vkbasalt_profile_config(
                profile_name,
                steam_app_id,
            )
            config_path.parent.mkdir(
                parents=True,
                exist_ok=True,
            )
            legacy_default_path = (
                self.vkbasalt_profile_config_dir /
                profile_storage.legacy_vkbasalt_profile_config_filename(
                    DEFAULT_PROFILE_NAME
                )
            )
            source_path = config_path
            if (
                    profile_name == DEFAULT_PROFILE_NAME
                    and not config_path.is_file()
                    and legacy_default_path.is_file()
            ):
                source_path = legacy_default_path
            existing_content = (
                source_path.read_text(encoding="utf-8")
                if source_path.is_file()
                else ""
            )
            write_managed_text_atomically(
                config_path,
                profile_storage.merge_vkbasalt_config_content(
                    existing_content,
                    settings,
                    self.vkbasalt_shader_dir,
                ),
                0o644,
                self.log,
            )
        return expected_names

    def _write_vkbasalt_shader_assets(self) -> None:
        """Install the immutable shader sources shared by every profile."""
        for filename in self._VKBASALT_SHADER_ASSETS:
            source = self._VKBASALT_SHADER_ASSET_DIR / filename
            write_managed_text_atomically(
                self.vkbasalt_shader_dir / filename,
                source.read_text(encoding="utf-8"),
                0o644,
                self.log,
            )

    def _migrate_vkbasalt_profile_config(
            self,
            profile_name: str,
            steam_app_id: Optional[str],
    ) -> None:
        """Move one retired opaque config to its compact canonical identity."""
        if profile_name == DEFAULT_PROFILE_NAME:
            return
        legacy_path = (
            self.vkbasalt_profile_config_dir /
            profile_storage.legacy_vkbasalt_profile_config_filename(
                profile_name
            )
        )
        config_path = profile_storage.vkbasalt_config_path(
            profile_name,
            self.vkbasalt_global_config_path,
            self.vkbasalt_profile_config_dir,
            steam_app_id,
        )
        with self._configuration_write_lock:
            if not legacy_path.is_file() or config_path.exists():
                return
            config_path.parent.mkdir(parents=True, exist_ok=True)
            legacy_path.replace(config_path)
        self.log.info(
            "Migrated MAKO vkBasalt config from %s to %s",
            legacy_path,
            config_path,
        )

    def _move_vkbasalt_profile_config_identity(
            self,
            profile_name: str,
            old_steam_app_id: Optional[str],
            new_steam_app_id: Optional[str],
    ) -> None:
        """Keep advanced edits when a profile gains a Steam identity."""
        self._migrate_vkbasalt_profile_config(
            profile_name,
            old_steam_app_id,
        )
        old_path = profile_storage.vkbasalt_config_path(
            profile_name,
            self.vkbasalt_global_config_path,
            self.vkbasalt_profile_config_dir,
            old_steam_app_id,
        )
        new_path = profile_storage.vkbasalt_config_path(
            profile_name,
            self.vkbasalt_global_config_path,
            self.vkbasalt_profile_config_dir,
            new_steam_app_id,
        )
        if old_path == new_path:
            return
        with self._configuration_write_lock:
            if not old_path.is_file() or new_path.exists():
                return
            new_path.parent.mkdir(parents=True, exist_ok=True)
            old_path.replace(new_path)
        self.log.info(
            "Moved MAKO vkBasalt config identity from %s to %s",
            old_path,
            new_path,
        )

    def _vkbasalt_config_path(
            self,
            profile_name: str,
            metadata: Optional[profile_storage.ProfileMetadata] = None,
    ) -> Path:
        """Return the advanced-edit file associated with one profile."""
        resolved_metadata = metadata
        if resolved_metadata is None and profile_name != DEFAULT_PROFILE_NAME:
            profile_data = self._get_profile_data()
            resolved_metadata = self._read_profile_metadata(profile_data)
        steam_app_id = profile_storage.metadata_steam_app_id(
            resolved_metadata or {},
            profile_name,
        )
        self._migrate_vkbasalt_profile_config(profile_name, steam_app_id)
        return profile_storage.vkbasalt_config_path(
            profile_name,
            self.vkbasalt_global_config_path,
            self.vkbasalt_profile_config_dir,
            steam_app_id,
        )

    def _rename_vkbasalt_profile_config(
            self,
            old_name: str,
            new_name: str,
            metadata: profile_storage.ProfileMetadata,
    ) -> None:
        """Keep advanced edits with a saved profile when it is renamed."""
        steam_app_id = profile_storage.metadata_steam_app_id(
            metadata,
            old_name,
        )
        old_path = self._vkbasalt_config_path(old_name, metadata)
        new_path = profile_storage.vkbasalt_config_path(
            new_name,
            self.vkbasalt_global_config_path,
            self.vkbasalt_profile_config_dir,
            steam_app_id,
        )
        if old_path == new_path:
            return
        if not old_path.is_file():
            return
        new_path.parent.mkdir(parents=True, exist_ok=True)
        old_path.replace(new_path)
        self.log.info(
            "Moved MAKO vkBasalt config from %s to %s",
            old_path,
            new_path,
        )

    def _remove_stale_vkbasalt_profile_configs(
            self,
            expected_names: set[str],
    ) -> None:
        """Remove only generated configs no longer referenced by the sidecar."""
        if not self.vkbasalt_profile_config_dir.is_dir():
            return
        for path in self.vkbasalt_profile_config_dir.iterdir():
            if (
                path.is_file()
                and re.fullmatch(
                    r"(?:[0-9a-f]{24}|profile-[0-9a-f]{12}|steam-[0-9]+)\.conf",
                    path.name,
                )
                and path.name not in expected_names
            ):
                path.unlink()
                self.log.info("Removed stale MAKO vkBasalt config %s", path)

    def _wrapper_settings_for_profile(
            self,
            profile_name: str,
            profile_settings: profile_storage.WrapperProfileSettings = None,
    ) -> profile_storage.WrapperSettingsData:
        return profile_storage.wrapper_settings_for_profile(
            profile_name,
            profile_settings or self._read_wrapper_profile_settings(),
            self._wrapper_settings_defaults,
            self._normalize_wrapper_settings,
        )

    @staticmethod
    def _processes_for_config(config: Dict[str, Any]) -> list[str]:
        return profile_storage.processes_for_config(config)

    @classmethod
    def _default_profile_metadata(
            cls, profile_data: ProfileData) -> profile_storage.ProfileMetadata:
        return profile_storage.default_profile_metadata(
            profile_data,
            cls._processes_for_config,
        )

    def _write_profile_metadata(
            self, metadata: profile_storage.ProfileMetadata) -> None:
        profile_storage.write_profile_metadata(
            self.config_dir,
            self.profile_metadata_path,
            self._PROFILE_METADATA_VERSION,
            metadata,
            self._write_file,
        )

    def _read_profile_metadata(
            self,
            profile_data: ProfileData = None,
    ) -> profile_storage.ProfileMetadata:
        profile_data = profile_data or self._get_profile_data()
        return profile_storage.read_profile_metadata(
            self.profile_metadata_path,
            self._PROFILE_METADATA_VERSION,
            profile_data,
            self._default_profile_metadata,
        )

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

    def _profile_details(
            self,
            profile_data: ProfileData,
            metadata: profile_storage.ProfileMetadata,
    ) -> list[profile_storage.ProfileDetails]:
        return profile_storage.profile_details(
            profile_data,
            metadata,
            self._processes_for_config,
        )

    def _config_for_profile(
            self,
            profile_data: ProfileData,
            profile_name: str,
            profile_settings: profile_storage.WrapperProfileSettings = None,
    ) -> ConfigurationData:
        """Merge MAKO Renderer TOML, global, and Decky wrapper fields for one profile."""
        return profile_storage.config_for_profile(
            profile_data,
            profile_name,
            profile_settings or self._read_wrapper_profile_settings(),
            self._wrapper_settings_for_profile,
        )

    @staticmethod
    def _has_active_in(config: ConfigurationData) -> bool:
        """Return whether an engine profile can select itself by process name."""
        return wrapper_generation.has_active_in(config)

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
        return wrapper_generation.profile_selection_lines(
            profile_name,
            config,
            automatic_matching_enabled,
            cls._has_active_in,
        )

    def get_config(self) -> ConfigurationResponse:
        """Read current TOML configuration merged with launch script environment variables

        Returns:
            ConfigurationResponse with current configuration or error
        """
        try:
            profile_data = self._get_profile_data()
            profile_name = profile_data["current_profile"]
            metadata = self._read_profile_metadata(profile_data)
            config = self._config_for_profile(
                profile_data, profile_name
            )

            return self._success_response(
                ConfigurationResponse,
                config=config,
                vkbasalt_config_path=str(
                    self._vkbasalt_config_path(profile_name, metadata)
                ),
            )

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
            profile_data = self._get_profile_data()
            if profile_name not in profile_data["profiles"]:
                return self._error_response(
                    ConfigurationResponse,
                    f"Profile '{profile_name}' does not exist",
                    config=None,
                )

            metadata = self._read_profile_metadata(profile_data)
            config = self._config_for_profile(profile_data, profile_name)
            return self._success_response(
                ConfigurationResponse,
                f"Profile '{profile_name}' retrieved successfully",
                config=config,
                vkbasalt_config_path=str(
                    self._vkbasalt_config_path(profile_name, metadata)
                ),
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
            normalized_wrapper_settings = self._normalize_wrapper_settings(config)
            expected_profile_configs = self._write_vkbasalt_profile_configs({
                DEFAULT_PROFILE_NAME: normalized_wrapper_settings,
            })
            self._remove_stale_vkbasalt_profile_configs(
                expected_profile_configs
            )
            script_content = self._generate_script_content(config)

            script_changed = write_managed_text_atomically(
                self.mako_script_path,
                script_content,
                0o755,
                self.log,
            )

            if script_changed:
                self.log.info(f"Updated MAKO launch script at {self.mako_script_path}")

            return self._success_response(ConfigurationResponse,
                                        "Launch script updated successfully",
                                        config=config)

        except Exception as e:
            error_msg = f"Error updating launch script: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(ConfigurationResponse, str(e), config=None)

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
        write_managed_text_atomically(
            self.mako_script_path,
            content,
            0o755,
            self.log,
        )
        return True

    def _generate_script_content(self, config: ConfigurationData) -> str:
        """Generate the content for the isolated per-game launch script

        Args:
            config: Configuration data to apply to the script

        Returns:
            The complete script content as a string
        """
        return wrapper_generation.assemble_script_content(
            self._wrapper_generation_context(),
            self._generate_host_compatibility_guard_lines(),
            [
                *self._script_configuration_lines(config),
                *wrapper_generation.vkbasalt_profile_environment_lines(
                    DEFAULT_PROFILE_NAME,
                    config,
                    self.vkbasalt_global_config_path,
                    self.vkbasalt_profile_config_dir,
                ),
            ],
            self._generate_layer_environment_lines(),
            self._profile_selection_lines(DEFAULT_PROFILE_NAME, config),
        )

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
            profile_data,
            fallback_profile,
        )
        automatic_matching_enabled = any(
            self._has_active_in(profile_config)
            for profile_config in profile_data["profiles"].values()
        )
        return wrapper_generation.assemble_profile_script_content(
            current_profile,
            self._wrapper_generation_context(),
            self._generate_host_compatibility_guard_lines(),
            self._wrapper_profile_configuration_lines(profile_data),
            self._generate_layer_environment_lines(),
            self._profile_selection_lines(
                fallback_profile,
                fallback_config,
                automatic_matching_enabled,
            ),
        )

    def _wrapper_profile_configuration_lines(
            self, profile_data: ProfileData) -> list[str]:
        """Select launcher-only settings by explicit profile or Steam app ID."""
        return wrapper_generation.wrapper_profile_configuration_lines(
            profile_data,
            self._read_wrapper_profile_settings(),
            self._read_profile_metadata(profile_data),
            vkbasalt_global_config_path=self.vkbasalt_global_config_path,
            vkbasalt_profile_config_dir=self.vkbasalt_profile_config_dir,
            profile_config=self._config_for_profile,
            config_lines=self._script_configuration_lines,
        )

    @classmethod
    def _script_configuration_lines(cls, config: ConfigurationData) -> list[str]:
        """Generate wrapper settings without repeating forced compatibility exports."""
        return wrapper_generation.script_configuration_lines(
            config,
            cls._hdr_activation_lines,
        )

    @staticmethod
    def _hdr_activation_lines(config: Dict[str, Any]) -> list[str]:
        """Keep the packaged Decky launcher on its proven SDR contract.

        The engine contains HDR colour-pipeline groundwork, but cross-game HDR
        activation and presentation are unavailable in the current Decky
        release. Remove inherited DXVK HDR exposure while MAKO enforces its
        supported SDR processing and presentation boundary.
        """
        return wrapper_generation.hdr_activation_lines(config)

    def _generate_layer_environment_lines(self) -> list[str]:
        """Activate MAKO through its deterministic Vulkan discovery boundary.

        The same wrapper is used in Steam launch options and as Heroic's
        per-game wrapper command. Give the Vulkan loader one deterministic
        implicit-layer directory before it constructs the chain: the mounted
        MAKO extension in Flatpak, or Decky's private MAKO manifest directory
        on the host. This restores the v2 SDR boundary that is proven to
        intercept Wine's swapchain without Gamescope WSI, Steam's Vulkan
        Fossilize/overlay layers, or system-wide ordering changing the dispatch
        chain. A profile may admit the guarded host system directory for exactly
        one selected external tool, or the bounded Gamescope WSI
        compatibility lane. The Gamescope compositor and Steam/Game Mode UI
        remain outside the default application layer chain. The explicit LSFG,
        Gamescope, Mesa, and HDR guards provide defence in depth.
        """
        return wrapper_generation.layer_environment_lines(
            self._wrapper_generation_context()
        )

    def migrate_launch_script_if_needed(self) -> bool:
        """Replace stale generated cache from canonical profile/config state.

        Only the current wrapper is supported in place. Any non-current,
        incomplete, or contaminated wrapper is regenerated atomically; do not
        add format-specific transforms here. Migrate unique user state before
        calling this method.
        """
        if not self.mako_script_path.exists():
            return False

        try:
            current_content = self.mako_script_path.read_text(encoding="utf-8")
            wrapper_is_current = wrapper_generation.is_current_wrapper(
                current_content,
                self._WRAPPER_FORMAT_MARKER,
                self._HOST_COMPATIBILITY_MARKER,
                self._diagnostics_default_marker(),
                self._REQUIRED_WRAPPER_EXPORTS,
            )
            if wrapper_is_current:
                return False

            profile_data = self._get_profile_data()
            result = self.update_mako_script_from_profile_data(profile_data)
            if not result["success"]:
                raise OSError(result.get("error") or "could not refresh launch wrapper")

            self.log.info(
                "Upgraded installed MAKO launch wrapper to format %s",
                self._WRAPPER_FORMAT_VERSION,
            )
            return True
        except OSError:
            raise
        except Exception as error:
            raise OSError(f"Could not upgrade MAKO Renderer launch wrapper: {error}") from error

    @staticmethod
    def _generate_unsupported_host_passthrough_lines(
            indent: str = "") -> list[str]:
        """Disable MAKO and preserve Armada's launcher exactly once."""
        return wrapper_generation.unsupported_host_passthrough_lines(
            ARMADA_DEVICE_ENV,
            ARMADA_GAME_LAUNCH,
            indent,
        )

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
        return wrapper_generation.host_compatibility_guard_lines(
            ARMADA_DEVICE_ENV,
            ARMADA_GAME_LAUNCH,
            ConfigurationService._HOST_COMPATIBILITY_MARKER,
            ConfigurationService._generate_unsupported_host_passthrough_lines(
                "    "
            ),
        )

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

        write_managed_text_atomically(
            self.config_file_path,
            toml_content,
            0o644,
            self.log,
        )

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
            metadata[normalized_name] = profile_storage.profile_metadata_entry(
                profile_name.strip(), PROFILE_KIND_PROCESS
            )
            self._save_profile_data(new_profile_data)
            self._write_wrapper_profile_settings(profile_settings, metadata)
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
            profile_data = self._get_profile_data()
            self.migrate_profile_metadata_if_needed()
            metadata = self._read_profile_metadata(profile_data)
            profile_settings = self._read_wrapper_profile_settings()
            new_profile_data = ConfigurationManager.delete_profile(profile_data, profile_name)
            profile_settings.pop(profile_name, None)
            metadata.pop(profile_name, None)
            self._save_profile_data(new_profile_data)
            if self.wrapper_profile_settings_path.exists() or profile_settings:
                self._write_wrapper_profile_settings(profile_settings, metadata)
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
            profile_data = self._get_profile_data()
            self.migrate_profile_metadata_if_needed()
            metadata = self._read_profile_metadata(profile_data)

            # Get the normalized name that will be used for storage
            normalized_name = ConfigurationManager.normalize_profile_name(new_name)

            new_profile_data = ConfigurationManager.rename_profile(profile_data, old_name, new_name)
            profile_settings = self._read_wrapper_profile_settings()
            if old_name in profile_settings:
                profile_settings[normalized_name] = profile_settings.pop(old_name)
            self._rename_vkbasalt_profile_config(
                old_name,
                normalized_name,
                metadata,
            )
            profile_storage.rename_profile_metadata(
                metadata, old_name, normalized_name, new_name.strip()
            )
            self._save_profile_data(new_profile_data)
            if self.wrapper_profile_settings_path.exists() or profile_settings:
                self._write_wrapper_profile_settings(profile_settings, metadata)
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
            profile_data = self._get_profile_data()
            self.migrate_profile_metadata_if_needed()
            metadata = self._read_profile_metadata(profile_data)
            profile_settings = self._read_wrapper_profile_settings()

            target_profile = next((
                profile_name
                for profile_name in metadata
                if profile_storage.metadata_steam_app_id(
                    metadata, profile_name
                ) == normalized_app_id
            ), None)

            detected_names = {name.casefold() for name in processes}
            if target_profile is None:
                for profile_name, config in profile_data["profiles"].items():
                    if profile_name == DEFAULT_PROFILE_NAME:
                        continue
                    # Process-name matching may adopt a legacy or manually
                    # created profile, but it must never repurpose a profile
                    # already bound to a different Steam game. Many unrelated
                    # games use the same generic executable name.
                    if profile_storage.metadata_steam_app_id(
                            metadata, profile_name):
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
                for name in profile_storage.metadata_captured_processes(
                    metadata, target_profile
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
            previous_app_id = profile_storage.metadata_steam_app_id(
                metadata,
                target_profile,
            )
            self._move_vkbasalt_profile_config_identity(
                target_profile,
                previous_app_id,
                normalized_app_id,
            )
            metadata[target_profile] = profile_storage.profile_metadata_entry(
                friendly_name,
                PROFILE_KIND_GAME,
                normalized_app_id,
                processes,
            )

            self._save_profile_data(profile_data)
            self._write_wrapper_profile_settings(profile_settings, metadata)
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
                    for profile_name in metadata
                    if profile_name != DEFAULT_PROFILE_NAME
                    and profile_storage.metadata_steam_app_id(
                        metadata, profile_name
                    ) == normalized_app_id
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
                        and not profile_storage.metadata_steam_app_id(
                            metadata, profile_name
                        )
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

    def update_profile_config(
            self, profile_name: str, config: ConfigurationData
    ) -> ConfigurationResponse:
        """Serialize a complete profile replacement with field patches."""
        with self._configuration_write_lock:
            return self._persist_profile_config(profile_name, config)

    def _persist_profile_config(
            self, profile_name: str, config: ConfigurationData
    ) -> ConfigurationResponse:
        """Update configuration for a specific profile

        Args:
            profile_name: Name of the profile to update
            config: Configuration data to apply

        Returns:
            ConfigurationResponse with success status
        """
        try:
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
            metadata = self._read_profile_metadata(profile_data)
            self._save_profile_data(profile_data)
            self._write_wrapper_profile_settings(profile_settings, metadata)

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

    def update_profile_config_fields(
            self, profile_name: str, changes: ConfigurationPatch
    ) -> ConfigurationResponse:
        """Merge validated field changes into the latest saved profile.

        Decky's controls update independently and some writes are deliberately
        deferred. Reading the canonical profile here prevents an older frontend
        snapshot from reverting unrelated fields or writing into another
        profile after the editor selection changes.
        """
        with self._configuration_write_lock:
            return self._update_profile_config_fields(profile_name, changes)

    def _update_profile_config_fields(
            self, profile_name: str, changes: ConfigurationPatch
    ) -> ConfigurationResponse:
        """Execute one profile patch while holding the write lock."""
        try:
            profile_data = self._get_profile_data()
            if profile_name not in profile_data["profiles"]:
                return self._error_response(
                    ConfigurationResponse,
                    f"Profile '{profile_name}' does not exist",
                    config=None,
                )

            unknown_fields = sorted(
                set(changes) - set(ConfigurationManager.get_field_names())
            )
            if unknown_fields:
                return self._error_response(
                    ConfigurationResponse,
                    "Unknown configuration fields: " + ", ".join(unknown_fields),
                    config=None,
                )

            current_config = self._config_for_profile(profile_data, profile_name)
            merged_config = ConfigurationManager.validate_config({
                **current_config,
                **changes,
            })
            return self._persist_profile_config(profile_name, merged_config)
        except (OSError, IOError, ValueError, TypeError) as error:
            self.log.error(
                "Error updating profile '%s' fields: %s",
                profile_name,
                error,
            )
            return self._error_response(
                ConfigurationResponse,
                str(error),
                config=None,
            )

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
            script_changed = write_managed_text_atomically(
                self.mako_script_path,
                script_content,
                0o755,
                self.log,
            )

            if script_changed:
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
