"""
Base service class with common functionality.
"""

import logging
import os
import secrets
import shutil
from pathlib import Path
from typing import Any, Optional, TypeVar, Dict

import decky

from .constants import (
    LOCAL_LIB,
    LOCAL_LIB32,
    VULKAN_LAYER_DIR,
    USER_VULKAN_LAYER_DIR,
    JSON_FILENAME,
    JSON32_FILENAME,
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
        self.user_vulkan_layer_dir = self.user_home / USER_VULKAN_LAYER_DIR
        self.registered_json_file = self.user_vulkan_layer_dir / JSON_FILENAME
        self.registered_json32_file = self.user_vulkan_layer_dir / JSON32_FILENAME
        self.mako_script_path = self.user_home / SCRIPT_NAME
        self.mako_launch_script_path = self.user_home / SCRIPT_NAME
        self.diagnostics_script_path = self.user_home / DIAGNOSTICS_SCRIPT_NAME
        self.config_dir = self.user_home / CONFIG_DIR
        self.config_file_path = self.config_dir / CONFIG_FILENAME
        self.wrapper_profile_settings_path = self.config_dir / WRAPPER_PROFILE_SETTINGS_FILENAME
        self.profile_metadata_path = self.config_dir / PROFILE_METADATA_FILENAME

    def _ensure_directories(self) -> None:
        """Create necessary directories if they don't exist"""
        self.local_lib_dir.mkdir(parents=True, exist_ok=True)
        self.local_lib32_dir.mkdir(parents=True, exist_ok=True)
        self.local_share_dir.mkdir(parents=True, exist_ok=True)
        self.user_vulkan_layer_dir.mkdir(parents=True, exist_ok=True)
        self.config_dir.mkdir(parents=True, exist_ok=True)
        self.mako_script_path.parent.mkdir(parents=True, exist_ok=True)
        self.log.info(
            "Ensured isolated directories exist: %s, %s, %s, %s, %s, %s",
            self.local_lib_dir,
            self.local_lib32_dir,
            self.local_share_dir,
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

    def _write_file(self, path: Path, content: str, mode: int = 0o644) -> None:
        """Atomically replace a MAKO-managed text file.

        Args:
            path: Target file path
            content: Content to write
            mode: File permissions (default: 0o644)

        Raises:
            OSError: If write fails
        """
        file_descriptor, temporary_path = self._create_staged_file(path, mode)
        try:
            output = os.fdopen(file_descriptor, "w", encoding="utf-8")
            file_descriptor = -1
            with output:
                output.write(content)
                output.flush()
                os.fsync(output.fileno())

            temporary_path.replace(path)
            self.log.info(f"Wrote to {path}")
        except OSError as error:
            self.log.error(f"Failed to atomically replace {path}: {error}")
            raise OSError(
                f"Could not atomically replace MAKO-managed file {path}: {error}"
            ) from error
        finally:
            if file_descriptor >= 0:
                os.close(file_descriptor)
            temporary_path.unlink(missing_ok=True)

    def _copy_file_atomically(
            self, source: Path, destination: Path, mode: int = 0o644) -> None:
        """Copy bytes into a new file before replacing a MAKO-managed path."""
        file_descriptor, temporary_path = self._create_staged_file(
            destination,
            mode,
        )
        try:
            with source.open("rb") as input_file:
                output = os.fdopen(file_descriptor, "wb")
                file_descriptor = -1
                with output:
                    shutil.copyfileobj(input_file, output)
                    output.flush()
                    os.fsync(output.fileno())

            temporary_path.replace(destination)
            self.log.info("Copied %s to %s", source, destination)
        except OSError as error:
            self.log.error(
                "Failed to atomically copy %s to %s: %s",
                source,
                destination,
                error,
            )
            raise OSError(
                f"Could not atomically replace MAKO-managed file {destination}: {error}"
            ) from error
        finally:
            if file_descriptor >= 0:
                os.close(file_descriptor)
            temporary_path.unlink(missing_ok=True)

    def _create_staged_file(self, destination: Path, mode: int) -> tuple[int, Path]:
        """Create a same-directory staging file without requiring chmod normally."""
        destination.parent.mkdir(parents=True, exist_ok=True)
        open_flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
        if hasattr(os, "O_CLOEXEC"):
            open_flags |= os.O_CLOEXEC

        for _attempt in range(32):
            temporary_path = destination.parent / (
                f".{destination.name}.{secrets.token_hex(8)}"
            )
            try:
                file_descriptor = os.open(temporary_path, open_flags, mode)
            except FileExistsError:
                continue

            actual_mode = os.fstat(file_descriptor).st_mode & 0o777
            required_owner_mode = mode & 0o700
            if actual_mode & required_owner_mode == required_owner_mode:
                if actual_mode != mode:
                    self.log.debug(
                        "Host umask adjusted staging mode for %s from %03o to %03o",
                        destination,
                        mode,
                        actual_mode,
                    )
                return file_descriptor, temporary_path

            try:
                os.fchmod(file_descriptor, mode)
                return file_descriptor, temporary_path
            except OSError as error:
                os.close(file_descriptor)
                temporary_path.unlink(missing_ok=True)
                raise OSError(
                    "The host removed required owner permissions while creating "
                    f"{destination}, and could not restore them: {error}"
                ) from error

        raise OSError(f"Could not allocate a staging file beside {destination}")

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
