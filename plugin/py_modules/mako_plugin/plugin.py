"""
Main plugin class for the MAKO Decky Loader plugin.

This plugin provides services for installing and managing the MAKO Renderer
Vulkan layer for frame generation and scaling on SteamOS.
"""

import os
import subprocess
import hashlib
import shlex
from typing import Dict, Any
from pathlib import Path

import decky

from .installation import InstallationService
from .dll_detection import DllDetectionService
from .configuration import ConfigurationService
from .runtime_state import RuntimeStateService
from .config_schema import ConfigurationManager, DEFAULT_PROFILE_NAME
from .config_schema_generated import ConfigurationPatch
from .flatpak_service import (
    FlatpakAppInfo,
    FlatpakExtensionStatus,
    FlatpakOverrideResponse,
    FlatpakService,
)
from .types import (
    ConfigSchemaResponse,
    ConfigurationResponse,
    DllDetectionResponse,
    DllStatsResponse,
    FgmodCheckResponse,
    FileContentResponse,
    InstallationCheckResponse,
    InstallationResult,
    LaunchOptionResponse,
    ProfileResponse,
    ProfilesResponse,
    RuntimeStatusResponse,
)


class Plugin:
    """
    Main plugin class for MAKO Decky and MAKO Renderer management.

    This class provides a unified interface for installation, configuration,
    and DLL detection services. It implements the Decky Loader plugin lifecycle
    methods (_main, _unload, and _uninstall).
    """

    def __init__(self):
        """Initialize the plugin with all necessary services"""
        self.installation_service = InstallationService()
        self.dll_detection_service = DllDetectionService()
        self.configuration_service = ConfigurationService()
        self.runtime_state_service = RuntimeStateService()
        self.flatpak_service = FlatpakService()

    async def install_mako(self) -> InstallationResult:
        """Install MAKO Renderer and refresh existing Flatpak runtime copies.

        Returns:
            InstallationResponse dict with success status and message/error
        """
        result = self.installation_service.install()
        if not result.get("success"):
            return result

        refresh = self.flatpak_service.refresh_installed_extensions()
        updated_versions = refresh.get("updated_versions", [])
        result["flatpak_extensions_updated"] = updated_versions
        if refresh.get("success"):
            if updated_versions:
                result["message"] = (
                    "MAKO Renderer installed; refreshed Flatpak runtimes "
                    + ", ".join(updated_versions)
                )
        else:
            refresh_error = refresh.get("error") or "unknown Flatpak error"
            self.installation_service.log.warning(
                "MAKO Renderer installed, but Flatpak runtimes were not fully refreshed: %s",
                refresh_error,
            )
            result["flatpak_refresh_error"] = refresh_error
            result["message"] = (
                "MAKO Renderer installed. Flatpak runtime refresh needs attention "
                "in Flatpak Setup."
            )
        return result

    async def check_mako_installed(self) -> InstallationCheckResponse:
        """Check if MAKO Renderer is already installed

        Returns:
            InstallationCheckResponse dict with installation status and paths
        """
        return self.installation_service.check_installation()

    async def uninstall_mako(self) -> InstallationResult:
        """Uninstall MAKO Renderer by removing the installed files

        Returns:
            UninstallationResponse dict with success status and removed files
        """
        return self.installation_service.uninstall()

    async def check_lossless_scaling_dll(self) -> DllDetectionResponse:
        """Check if Lossless Scaling DLL is available at the expected paths

        Returns:
            DllDetectionResponse dict with detection status and path info
        """
        return self.dll_detection_service.check_lossless_scaling_dll()

    async def get_dll_stats(self) -> DllStatsResponse:
        """Get detailed statistics about the detected DLL

        Returns:
            Dict containing DLL path, SHA256 hash, and other stats
        """
        try:
            dll_result = self.dll_detection_service.check_lossless_scaling_dll()

            if not dll_result.get("detected") or not dll_result.get("path"):
                return {
                    "success": False,
                    "error": "DLL not detected",
                    "dll_path": None,
                    "dll_sha256": None
                }

            dll_path = dll_result["path"]
            if dll_path is None:
                return {
                    "success": False,
                    "error": "DLL path is None",
                    "dll_path": None,
                    "dll_sha256": None
                }

            dll_path_obj = Path(dll_path)

            sha256_hash = hashlib.sha256()
            try:
                with open(dll_path_obj, "rb") as f:
                    for chunk in iter(lambda: f.read(4096), b""):
                        sha256_hash.update(chunk)
                dll_sha256 = sha256_hash.hexdigest()
            except Exception as e:
                return {
                    "success": False,
                    "error": f"Failed to calculate SHA256: {str(e)}",
                    "dll_path": dll_path,
                    "dll_sha256": None
                }

            return {
                "success": True,
                "dll_path": dll_path,
                "dll_sha256": dll_sha256,
                "dll_source": dll_result.get("source"),
                "error": None
            }

        except Exception as e:
            return {
                "success": False,
                "error": f"Failed to get DLL stats: {str(e)}",
                "dll_path": None,
                "dll_sha256": None
            }

    async def get_mako_config(self) -> ConfigurationResponse:
        """Read the current MAKO Renderer configuration.

        Returns:
            ConfigurationResponse dict with current configuration or error
        """
        return self.configuration_service.get_config()

    async def get_profile_config(
            self, profile_name: str
    ) -> ConfigurationResponse:
        """Read a saved profile without making it the runtime profile."""
        return self.configuration_service.get_profile_config(profile_name)

    async def get_runtime_status(
            self, profile_name: str = ""
    ) -> RuntimeStatusResponse:
        """Return validated requested-versus-applied Renderer state."""
        return self.runtime_state_service.get_status(profile_name)

    async def get_config_schema(self) -> ConfigSchemaResponse:
        """Get configuration schema information for frontend

        Returns:
            Dict with field names, types, defaults, and profile information
        """
        try:
            profiles_response = self.configuration_service.get_profiles()

            schema_data = {
                "field_names": ConfigurationManager.get_field_names(),
                "field_types": {name: field_type.value for name, field_type in ConfigurationManager.get_field_types().items()},
                "defaults": ConfigurationManager.get_defaults()
            }

            if profiles_response.get("success"):
                schema_data["profiles"] = profiles_response.get("profiles", [])
                schema_data["current_profile"] = profiles_response.get("current_profile")
            else:
                schema_data["profiles"] = [DEFAULT_PROFILE_NAME]
                schema_data["current_profile"] = DEFAULT_PROFILE_NAME

            return schema_data

        except (ValueError, KeyError, AttributeError) as e:
            self.configuration_service.log.warning(f"Failed to get full schema, using fallback: {e}")
            return {
                "field_names": ConfigurationManager.get_field_names(),
                "field_types": {name: field_type.value for name, field_type in ConfigurationManager.get_field_types().items()},
                "defaults": ConfigurationManager.get_defaults(),
                "profiles": [DEFAULT_PROFILE_NAME],
                "current_profile": DEFAULT_PROFILE_NAME
            }

    async def update_mako_config(
            self, config: Dict[str, Any]
    ) -> ConfigurationResponse:
        """Update MAKO Renderer TOML configuration using the object API.

        Args:
            config: Configuration data dictionary containing all settings

        Returns:
            ConfigurationResponse dict with success status
        """
        validated_config = ConfigurationManager.validate_config(config)

        return self.configuration_service.update_config_from_dict(validated_config)

    async def get_profiles(self) -> ProfilesResponse:
        """Get list of all profiles and current profile

        Returns:
            ProfilesResponse dict with profile list and current profile
        """
        return self.configuration_service.get_profiles()

    async def create_profile(
            self, profile_name: str, source_profile: str = None
    ) -> ProfileResponse:
        """Create a new profile

        Args:
            profile_name: Name for the new profile
            source_profile: Optional source profile to copy from (default: current profile)

        Returns:
            ProfileResponse dict with success status
        """
        return self.configuration_service.create_profile(profile_name, source_profile)

    async def delete_profile(self, profile_name: str) -> ProfileResponse:
        """Delete a profile

        Args:
            profile_name: Name of the profile to delete

        Returns:
            ProfileResponse dict with success status
        """
        return self.configuration_service.delete_profile(profile_name)

    async def rename_profile(
            self, old_name: str, new_name: str
    ) -> ProfileResponse:
        """Rename a profile

        Args:
            old_name: Current profile name
            new_name: New profile name

        Returns:
            ProfileResponse dict with success status
        """
        return self.configuration_service.rename_profile(old_name, new_name)

    async def capture_game_profile(
            self,
            app_id: str,
            display_name: str,
            source_profile: str = None,
    ) -> ProfileResponse:
        """Create or refresh a profile from the currently running game."""
        return self.configuration_service.capture_game_profile(
            app_id, display_name, source_profile
        )

    async def set_current_profile(self, profile_name: str) -> ProfileResponse:
        """Set the current active profile

        Args:
            profile_name: Name of the profile to set as current

        Returns:
            ProfileResponse dict with success status
        """
        return self.configuration_service.set_current_profile(profile_name)

    async def sync_current_profile(self, app_id: str = "") -> ProfileResponse:
        """Select a live app's saved profile, or restore the default profile."""
        return self.configuration_service.sync_current_profile(app_id)

    async def update_profile_config(
            self, profile_name: str, config: Dict[str, Any]
    ) -> ConfigurationResponse:
        """Update configuration for a specific profile

        Args:
            profile_name: Name of the profile to update
            config: Configuration data dictionary containing settings

        Returns:
            ConfigurationResponse dict with success status
        """
        validated_config = ConfigurationManager.validate_config(config)

        return self.configuration_service.update_profile_config(profile_name, validated_config)

    async def update_profile_config_fields(
            self, profile_name: str, changes: ConfigurationPatch
    ) -> ConfigurationResponse:
        """Merge independent UI field changes into one canonical profile."""
        return self.configuration_service.update_profile_config_fields(
            profile_name, changes
        )

    async def get_launch_option(self) -> LaunchOptionResponse:
        """Get the launch option that users need to set for their games

        Returns:
            Dict containing the launch option string and instructions
        """
        wrapper_path = self.installation_service.get_launch_script_path()
        return {
            "launch_option": f"{shlex.quote(wrapper_path)} %command%",
            "wrapper_path": wrapper_path,
            "instructions": "Add this to your game's launch options in Steam Properties",
            "explanation": "The launcher is created during installation, enables MAKO Renderer's Vulkan layer for this game, and selects its private configuration"
        }

    async def get_config_file_content(self) -> FileContentResponse:
        """Get the current config file content

        Returns:
            Dict containing the config file content or error message
        """
        try:
            config_path = self.configuration_service.config_file_path
            if not config_path.exists():
                return {
                    "success": False,
                    "content": None,
                    "path": str(config_path),
                    "error": "Config file does not exist"
                }

            content = config_path.read_text(encoding='utf-8')
            return {
                "success": True,
                "content": content,
                "path": str(config_path),
                "error": None
            }
        except Exception as e:
            return {
                "success": False,
                "content": None,
                "path": str(config_path) if 'config_path' in locals() else "unknown",
                "error": f"Error reading config file: {str(e)}"
            }

    async def get_launch_script_content(self) -> FileContentResponse:
        """Get the content of the launch script file

        Returns:
            FileContentResponse dict with file content or error information
        """
        try:
            script_path = self.installation_service.get_launch_script_path()

            if not os.path.exists(script_path):
                return {
                    "success": False,
                    "error": f"Launch script not found at {script_path}",
                    "path": str(script_path)
                }

            with open(script_path, 'r') as file:
                content = file.read()

            return {
                "success": True,
                "content": content,
                "path": str(script_path)
            }

        except Exception as e:
            decky.logger.error(f"Error reading launch script: {e}")
            return {
                "success": False,
                "error": str(e)
            }

    async def check_fgmod_directory(self) -> FgmodCheckResponse:
        """Check if the fgmod directory exists in the home directory

        Returns:
            Dict with exists status and directory path
        """
        try:
            home_path = Path(decky.DECKY_USER_HOME)
            fgmod_path = home_path / "fgmod"

            exists = fgmod_path.exists() and fgmod_path.is_dir()

            return {
                "success": True,
                "exists": exists,
                "path": str(fgmod_path)
            }

        except Exception as e:
            decky.logger.error(f"Error checking fgmod directory: {e}")
            return {
                "success": False,
                "exists": False,
                "error": str(e)
            }

    async def check_flatpak_extension_status(self) -> FlatpakExtensionStatus:
        """Check status of MAKO Renderer Flatpak runtime extensions

        Returns:
            FlatpakExtensionStatus dict with installation status for supported runtime versions
        """
        return self.flatpak_service.get_extension_status()

    async def install_flatpak_extension(
            self, version: str
    ) -> FlatpakOverrideResponse:
        """Install MAKO Renderer Flatpak runtime extension

        Args:
            version: A supported runtime version to install

        Returns:
            BaseResponse dict with success status and message/error
        """
        return self.flatpak_service.install_extension(version)

    async def uninstall_flatpak_extension(
            self, version: str
    ) -> FlatpakOverrideResponse:
        """Uninstall MAKO Renderer Flatpak runtime extension

        Args:
            version: A supported runtime version to uninstall

        Returns:
            BaseResponse dict with success status and message/error
        """
        return self.flatpak_service.uninstall_extension(version)

    async def get_flatpak_apps(self) -> FlatpakAppInfo:
        """Get list of installed Flatpak apps and their MAKO Renderer override status

        Returns:
            FlatpakAppInfo dict with apps list and override status
        """
        return self.flatpak_service.get_flatpak_apps()

    async def set_flatpak_app_override(
            self, app_id: str
    ) -> FlatpakOverrideResponse:
        """Set MAKO Renderer overrides for a Flatpak app

        Args:
            app_id: Flatpak application ID

        Returns:
            FlatpakOverrideResponse dict with operation result
        """
        return self.flatpak_service.set_app_override(app_id)

    async def remove_flatpak_app_override(
            self, app_id: str
    ) -> FlatpakOverrideResponse:
        """Remove MAKO Renderer overrides for a Flatpak app

        Args:
            app_id: Flatpak application ID

        Returns:
            FlatpakOverrideResponse dict with operation result
        """
        return self.flatpak_service.remove_app_override(app_id)

    async def _main(self):
        """
        Main entry point for the plugin.

        This method is called by Decky Loader when the plugin is loaded.
        Any initialization code should go here.
        """
        decky.logger.info("MAKO Decky loaded")
        try:
            _host, host_supported, host_error = (
                self.installation_service.current_package_host_compatibility()
            )
        except OSError as error:
            # Invalid release metadata is itself unsafe: do not regenerate an
            # activation wrapper until installation compatibility is known.
            host_supported = False
            host_error = str(error)

        if not host_supported:
            try:
                if self.configuration_service.enforce_unsupported_host_passthrough_if_needed():
                    decky.logger.info(
                        "Replaced incompatible MAKO wrapper with native-host passthrough"
                    )
            except OSError as error:
                decky.logger.warning(
                    "Could not enforce the native-host passthrough wrapper: %s",
                    error,
                )
            try:
                flatpak_cleanup = (
                    self.flatpak_service.disable_incompatible_host_overrides()
                )
                if flatpak_cleanup.get("success"):
                    disabled_apps = flatpak_cleanup.get("disabled_apps", [])
                    if disabled_apps:
                        decky.logger.info(
                            "Disabled incompatible MAKO Flatpak overrides for: %s",
                            ", ".join(disabled_apps),
                        )
                else:
                    decky.logger.warning(
                        "Could not verify every persisted Flatpak override: %s",
                        flatpak_cleanup.get("error") or "unknown error",
                    )
            except Exception as error:
                # The host wrapper boundary is independent of Flatpak. Keep the
                # plugin available so users can inspect or remove old state.
                decky.logger.warning(
                    "Could not verify every persisted Flatpak override: %s",
                    error,
                )
            decky.logger.warning("MAKO Renderer remains disabled: %s", host_error)
            return

        try:
            if self.configuration_service.migrate_profile_metadata_if_needed():
                decky.logger.info("Initialized game/process profile metadata")
        except (OSError, ValueError) as error:
            decky.logger.warning("Could not initialize profile metadata: %s", error)
        try:
            if self.configuration_service.migrate_launch_script_if_needed():
                decky.logger.info("Upgraded installed MAKO launch wrapper to the current format")
        except OSError as error:
            decky.logger.warning("Could not upgrade MAKO launch wrapper: %s", error)

        try:
            if self.installation_service.prepare_active_standalone_for_decky():
                decky.logger.info(
                    "Adopted the active standalone MAKO Renderer for Decky launch workflows"
                )
        except OSError as error:
            decky.logger.warning(
                "Could not prepare the active standalone MAKO Renderer: %s",
                error,
            )

        try:
            if self.installation_service.migrate_gamescope_wsi_compatibility_manifest_if_needed():
                decky.logger.info("Staged the guarded Gamescope WSI compatibility manifest")
        except OSError as error:
            decky.logger.warning(
                "Could not stage the Gamescope WSI compatibility manifest: %s",
                error,
            )

        try:
            if self.installation_service.refresh_guarded_postprocess_manifests_if_needed():
                decky.logger.info(
                    "Refreshed guarded optional post-process manifests"
                )
        except OSError as error:
            decky.logger.warning(
                "Could not stage optional post-process manifests: %s",
                error,
            )

        try:
            if self.installation_service.migrate_diagnostics_helper_if_needed():
                decky.logger.info("Installed the diagnostics helper")
        except OSError as error:
            decky.logger.warning("Could not install the diagnostics helper: %s", error)

    async def _unload(self):
        """
        Cleanup tasks when the plugin is unloaded.

        This method is called by Decky Loader when the plugin is being unloaded.
        Any cleanup code should go here.
        """
        decky.logger.info("MAKO Decky unloaded")

    async def _uninstall(self):
        """
        Called when the plugin is uninstalled.

        This method is called by Decky Loader when the plugin is being uninstalled.
        Performs cleanup of this plugin's private files.
        """
        decky.logger.info("MAKO Decky is being uninstalled")

        # Clean up MAKO Renderer files when the plugin is uninstalled
        self.installation_service.cleanup_on_uninstall()

        # Flatpak runtime extensions are shared by every MAKO Renderer installation.
        # Never remove them automatically: another plugin may still depend on one.
        decky.logger.info("Leaving shared Flatpak runtime extensions installed")

        decky.logger.info("MAKO Decky uninstall cleanup completed")
