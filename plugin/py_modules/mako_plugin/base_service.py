"""
Base service class with common functionality.
"""

import os
from pathlib import Path
from typing import Any, Optional, TypeVar, Dict

import decky

from .constants import (
    LOCAL_LIB,
    LOCAL_LIB32,
    VULKAN_LAYER_DIR,
    SPATIAL_SCALING_LAYER_DIR,
    GAMESCOPE_WSI_COMPATIBILITY_LAYER_DIR,
    MANGOHUD_LAYER_DIR,
    VKBASALT_LAYER_DIR,
    USER_VULKAN_LAYER_DIR,
    JSON_FILENAME,
    JSON32_FILENAME,
    SPATIAL_SCALING_JSON_FILENAME,
    SPATIAL_SCALING_JSON32_FILENAME,
    SCRIPT_NAME,
    DIAGNOSTICS_SCRIPT_NAME,
    CONFIG_DIR,
    CONFIG_FILENAME,
    WRAPPER_PROFILE_SETTINGS_FILENAME,
    PROFILE_METADATA_FILENAME,
)

ResponseType = TypeVar('ResponseType', bound=Dict[str, Any])


def resolve_user_home() -> Path:
    """Return the real Decky user's home directory.

    Decky can run plugins with a service environment whose ``HOME`` does not
    describe the logged-in user.  ``DECKY_USER_HOME`` is the supported source
    for that path and also covers systems such as Bazzite where the user is not
    named ``deck``.  The fallback keeps tests and older Decky versions working.
    """
    decky_user_home = getattr(decky, "DECKY_USER_HOME", None)
    if decky_user_home:
        candidate = Path(str(decky_user_home)).expanduser()
        if candidate.is_absolute():
            return candidate

    return Path.home()


class BaseService:
    """Base service class with common functionality"""

    def __init__(self, logger: Optional[Any] = None):
        """Initialize base service

        Args:
            logger: Logger instance, defaults to decky.logger if None
        """
        if logger is None:
            self.log = decky.logger
        else:
            self.log = logger

        self.user_home = resolve_user_home()
        self.local_lib_dir = self.user_home / LOCAL_LIB
        self.local_lib32_dir = self.user_home / LOCAL_LIB32
        self.local_share_dir = self.user_home / VULKAN_LAYER_DIR
        self.spatial_scaling_layer_dir = (
            self.user_home / SPATIAL_SCALING_LAYER_DIR
        )
        self.gamescope_wsi_compatibility_dir = (
            self.user_home / GAMESCOPE_WSI_COMPATIBILITY_LAYER_DIR
        )
        self.mangohud_layer_dir = self.user_home / MANGOHUD_LAYER_DIR
        self.vkbasalt_layer_dir = self.user_home / VKBASALT_LAYER_DIR
        self.user_vulkan_layer_dir = self.user_home / USER_VULKAN_LAYER_DIR
        self.registered_json_file = self.user_vulkan_layer_dir / JSON_FILENAME
        self.registered_json32_file = self.user_vulkan_layer_dir / JSON32_FILENAME
        self.spatial_scaling_json_file = (
            self.spatial_scaling_layer_dir /
            SPATIAL_SCALING_JSON_FILENAME
        )
        self.spatial_scaling_json32_file = (
            self.spatial_scaling_layer_dir /
            SPATIAL_SCALING_JSON32_FILENAME
        )
        self.mako_script_path = self.user_home / SCRIPT_NAME
        self.diagnostics_script_path = self.user_home / DIAGNOSTICS_SCRIPT_NAME
        self.config_dir = self.user_home / CONFIG_DIR
        self.config_file_path = self.config_dir / CONFIG_FILENAME
        self.wrapper_profile_settings_path = self.config_dir / WRAPPER_PROFILE_SETTINGS_FILENAME
        self.profile_metadata_path = self.config_dir / PROFILE_METADATA_FILENAME

    @property
    def mako_launch_script_path(self) -> Path:
        """Compatibility alias for the canonical generated-wrapper path."""
        return self.mako_script_path

    @mako_launch_script_path.setter
    def mako_launch_script_path(self, path: Path) -> None:
        self.mako_script_path = path

    def _ensure_directories(self) -> None:
        """Create necessary directories if they don't exist"""
        self.local_lib_dir.mkdir(parents=True, exist_ok=True)
        self.local_lib32_dir.mkdir(parents=True, exist_ok=True)
        self.local_share_dir.mkdir(parents=True, exist_ok=True)
        self.spatial_scaling_layer_dir.mkdir(parents=True, exist_ok=True)
        self.gamescope_wsi_compatibility_dir.mkdir(parents=True, exist_ok=True)
        self.mangohud_layer_dir.mkdir(parents=True, exist_ok=True)
        self.vkbasalt_layer_dir.mkdir(parents=True, exist_ok=True)
        self.user_vulkan_layer_dir.mkdir(parents=True, exist_ok=True)
        self.config_dir.mkdir(parents=True, exist_ok=True)
        self.mako_script_path.parent.mkdir(parents=True, exist_ok=True)
        self.log.info(
            "Ensured isolated directories exist: %s, %s, %s, %s, %s, %s, %s, %s, %s, %s",
            self.local_lib_dir,
            self.local_lib32_dir,
            self.local_share_dir,
            self.spatial_scaling_layer_dir,
            self.gamescope_wsi_compatibility_dir,
            self.mangohud_layer_dir,
            self.vkbasalt_layer_dir,
            self.user_vulkan_layer_dir,
            self.config_dir,
            self.mako_script_path.parent,
        )

    def _remove_if_exists(self, path: Path) -> bool:
        """Remove a file if it exists

        Args:
            path: Path to the file to remove

        Returns:
            True if file was removed, False if it didn't exist

        Raises:
            OSError: If removal fails
        """
        if path.exists():
            try:
                path.unlink()
                self.log.info(f"Removed {path}")
                return True
            except OSError as e:
                self.log.error(f"Failed to remove {path}: {e}")
                raise
        else:
            self.log.info(f"File not found: {path}")
            return False

    def _write_file(self, path: Path, content: str, mode: int = 0o644) -> bool:
        """Write content to a user-owned file.

        Args:
            path: Target file path
            content: Content to write
            mode: File permissions (default: 0o644)

        Raises:
            OSError: If write fails

        Returns:
            True when the file was updated, False when it was already current
        """
        try:
            if path.is_file():
                try:
                    current_content = path.read_text(encoding="utf-8")
                except (OSError, UnicodeError):
                    current_content = None
                if current_content == content:
                    if path.stat().st_mode & 0o777 != mode:
                        path.chmod(mode)
                        self.log.info(f"Corrected permissions for {path}")
                        return True
                    self.log.debug(f"MAKO file already current: {path}")
                    return False

            with open(path, "w", encoding="utf-8") as output:
                output.write(content)
                output.flush()
                os.fsync(output.fileno())

            path.chmod(mode)
            self.log.info(f"Wrote to {path}")
            return True
        except (OSError, IOError, PermissionError) as error:
            self.log.error(f"Failed to write to {path}: {error}")
            raise

    def _success_response(self, response_type: type, message: str = "", **kwargs) -> Any:
        """Create a standardized success response

        Args:
            response_type: The TypedDict response type to create
            message: Success message
            **kwargs: Additional response fields

        Returns:
            Success response dict
        """
        response = {
            "success": True,
            "message": message,
            "error": None
        }
        response.update(kwargs)
        return response

    def _error_response(self, response_type: type, error: str, message: str = "", **kwargs) -> Any:
        """Create a standardized error response

        Args:
            response_type: The TypedDict response type to create
            error: Error description
            message: Optional message
            **kwargs: Additional response fields

        Returns:
            Error response dict
        """
        response = {
            "success": False,
            "message": message,
            "error": error
        }
        response.update(kwargs)
        return response
