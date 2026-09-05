"""
DLL detection service for Lossless Scaling.
"""

import os
import re
import json
import math
import subprocess
import threading
import time
from pathlib import Path
from typing import Dict, Any, List

from .base_service import BaseService
from .constants import (
    ENV_MAKO_DLL_PATH, ENV_XDG_DATA_HOME,
    STEAM_COMMON_PATH, LOSSLESS_DLL_NAME, CLI_DIR, CLI_FILENAME,
)
from .types import DllDetectionResponse, ScalingModelStatusResponse


class DllDetectionService(BaseService):
    """Service for detecting Lossless Scaling DLL"""

    def __init__(self, logger=None):
        super().__init__(logger)
        self._model_lock = threading.Lock()
        self._model_cache_key = None
        self._model_cache_time = 0.0
        self._model_cache: ScalingModelStatusResponse | None = None

    def check_scaling_model(
        self, dll: str, method: str, sharpness: float,
    ) -> ScalingModelStatusResponse:
        """Inspect the selected graph through the installed Renderer, without saving profiles.

        The one-entry, process-local cache avoids repeated translations. File
        replacement invalidates it even when size and mtime are preserved; the
        bounded lifetime also retries after translator/runtime updates.
        """
        unknown: ScalingModelStatusResponse = {
            "compatible": None, "reason": "inspection-unavailable",
        }
        if (not isinstance(dll, str) or method not in ("ls1", "ls1-performance")
                or isinstance(sharpness, bool)
                or not isinstance(sharpness, (int, float))
                or not math.isfinite(sharpness) or not 0 <= sharpness <= 1):
            return {"compatible": None, "reason": "invalid-selection"}
        if not self._model_lock.acquire(blocking=False):
            return unknown
        try:
            # An explicit path is authoritative, just as it is in the Renderer.
            # Do not conceal a broken configured DLL by inspecting a different one.
            if dll:
                path = Path(dll)
            else:
                detected = self.check_lossless_scaling_dll()
                if detected["error"]:
                    return unknown
                if not detected["path"]:
                    return {"compatible": False, "reason": "dll-unavailable"}
                path = Path(detected["path"])
            cli = self.user_home / CLI_DIR / CLI_FILENAME
            try:
                if not path.is_file():
                    return {"compatible": False, "reason": "dll-unavailable"}
                def identity(candidate: Path):
                    status = candidate.stat()
                    return (str(candidate.resolve()), status.st_dev, status.st_ino,
                            status.st_size, status.st_mtime_ns, status.st_ctime_ns)
                key = (identity(path), identity(cli), method, sharpness)
                if (key == self._model_cache_key and self._model_cache is not None
                        and time.monotonic() - self._model_cache_time < 300):
                    return self._model_cache.copy()
                completed = subprocess.run(
                    [str(cli), "inspect-dll", "--dll", str(path),
                     "--ls1", method, "--sharpness", str(sharpness)],
                    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                    text=True, timeout=15, check=False,
                )
                # Old inspectors, crashes, timeouts, and malformed responses are
                # unknown, not proof that a user's model has disappeared.
                result = json.loads(completed.stdout)
                if (not isinstance(result, dict)
                        or type(result.get("schema_version")) is not int
                        or result["schema_version"] != 1
                        or type(result.get("compatible")) is not bool
                        or completed.returncode != (0 if result["compatible"] else 1)):
                    return unknown
                if key != (identity(path), identity(cli), method, sharpness):
                    return unknown
                response: ScalingModelStatusResponse = {
                    "compatible": result["compatible"],
                    "reason": None if result["compatible"] else "ls1-unavailable",
                }
                self._model_cache_key = key
                self._model_cache_time = time.monotonic()
                self._model_cache = response.copy()
                return response
            except (OSError, ValueError, subprocess.SubprocessError):
                return unknown
        finally:
            self._model_lock.release()

    def check_lossless_scaling_dll(self) -> DllDetectionResponse:
        """Check if Lossless Scaling DLL is available at the expected paths

        Search order:
        1. MAKO_DLL_PATH environment variable
        2. XDG_DATA_HOME Steam directory
        3. HOME/.local/share Steam directory
        4. All Steam library folders (including SD cards)

        Returns:
            DllDetectionResponse with detection status and path information
        """
        try:
            dll_path = self._check_env_dll_path()
            if dll_path:
                return dll_path

            xdg_path = self._check_xdg_data_home()
            if xdg_path:
                return xdg_path

            home_path = self._check_home_local_share()
            if home_path:
                return home_path

            steam_libraries_path = self._check_steam_library_folders()
            if steam_libraries_path:
                return steam_libraries_path

            return {
                "detected": False,
                "path": None,
                "source": None,
                "message": "Lossless Scaling DLL not found in expected locations",
                "error": None
            }

        except Exception as e:
            error_msg = f"Error checking Lossless Scaling DLL: {str(e)}"
            self.log.error(error_msg)
            return {
                "detected": False,
                "path": None,
                "source": None,
                "message": None,
                "error": str(e)
            }

    def _check_env_dll_path(self) -> DllDetectionResponse | None:
        """Check MAKO_DLL_PATH environment variable

        Returns:
            DllDetectionResponse if found, None otherwise
        """
        dll_path = os.getenv(ENV_MAKO_DLL_PATH)
        if dll_path and dll_path.strip():
            dll_path_obj = Path(dll_path.strip())
            if dll_path_obj.is_file():
                self.log.info(f"Found DLL via {ENV_MAKO_DLL_PATH}: {dll_path_obj}")
                return {
                    "detected": True,
                    "path": str(dll_path_obj),
                    "source": f"{ENV_MAKO_DLL_PATH} environment variable",
                    "message": None,
                    "error": None
                }
        return None

    def _check_xdg_data_home(self) -> DllDetectionResponse | None:
        """Check XDG_DATA_HOME Steam directory

        Returns:
            DllDetectionResponse if found, None otherwise
        """
        data_dir = os.getenv(ENV_XDG_DATA_HOME)
        if data_dir and data_dir.strip():
            dll_path = Path(data_dir.strip()) / "Steam" / STEAM_COMMON_PATH / LOSSLESS_DLL_NAME
            if dll_path.is_file():
                self.log.info(f"Found DLL via {ENV_XDG_DATA_HOME}: {dll_path}")
                return {
                    "detected": True,
                    "path": str(dll_path),
                    "source": f"{ENV_XDG_DATA_HOME} Steam directory",
                    "message": None,
                    "error": None
                }
        return None

    def _check_home_local_share(self) -> DllDetectionResponse | None:
        """Check HOME/.local/share Steam directory

        Returns:
            DllDetectionResponse if found, None otherwise
        """
        dll_path = self.user_home / ".local" / "share" / "Steam" / STEAM_COMMON_PATH / LOSSLESS_DLL_NAME
        if dll_path.is_file():
            self.log.info(f"Found DLL in the Decky user's Steam directory: {dll_path}")
            return {
                "detected": True,
                "path": str(dll_path),
                "source": "Decky user Steam directory",
                "message": None,
                "error": None
            }
        return None

    def _check_steam_library_folders(self) -> DllDetectionResponse | None:
        """Check all Steam library folders for Lossless Scaling DLL

        This method parses Steam's libraryfolders.vdf file to find all
        Steam library locations and checks each one for the DLL.

        Returns:
            DllDetectionResponse if found, None otherwise
        """
        steam_libraries = self._get_steam_library_paths()

        for library_path in steam_libraries:
            dll_path = Path(library_path) / STEAM_COMMON_PATH / LOSSLESS_DLL_NAME
            if dll_path.is_file():
                self.log.info(f"Found DLL in Steam library: {dll_path}")
                return {
                    "detected": True,
                    "path": str(dll_path),
                    "source": f"Steam library folder: {library_path}",
                    "message": None,
                    "error": None
                }

        return None

    def _get_steam_library_paths(self) -> List[str]:
        """Get all Steam library folder paths from libraryfolders.vdf

        Returns:
            List of Steam library folder paths
        """
        library_paths = []

        steam_paths = []

        data_dir = os.getenv(ENV_XDG_DATA_HOME)
        if data_dir and data_dir.strip():
            steam_paths.append(Path(data_dir.strip()) / "Steam")

        steam_paths.extend([
            self.user_home / ".local" / "share" / "Steam",
            self.user_home / ".steam" / "root",
            self.user_home / ".steam" / "steam",
            self.user_home / ".var" / "app" / "com.valvesoftware.Steam"
                / ".local" / "share" / "Steam",
        ])

        for steam_path in steam_paths:
            if steam_path.exists():
                library_paths.append(str(steam_path))

                vdf_path = steam_path / "steamapps" / "libraryfolders.vdf"
                if vdf_path.exists():
                    try:
                        additional_paths = self._parse_library_folders_vdf(vdf_path)
                        library_paths.extend(additional_paths)
                    except Exception as e:
                        self.log.warning(f"Failed to parse {vdf_path}: {str(e)}")

        seen = set()
        unique_paths = []
        for path in library_paths:
            if path not in seen:
                seen.add(path)
                unique_paths.append(path)

        self.log.info(f"Found {len(unique_paths)} Steam library paths: {unique_paths}")
        return unique_paths

    def _parse_library_folders_vdf(self, vdf_path: Path) -> List[str]:
        """Parse Steam's libraryfolders.vdf file to extract library paths

        Args:
            vdf_path: Path to the libraryfolders.vdf file

        Returns:
            List of additional Steam library folder paths
        """
        library_paths = []

        try:
            with open(vdf_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()

            path_pattern = r'"path"\s*"([^"]+)"'
            matches = re.findall(path_pattern, content, re.IGNORECASE)

            for path_match in matches:
                path = path_match.replace('\\\\', '/').replace('\\', '/')
                library_path = Path(path)

                if library_path.exists() and (library_path / "steamapps").exists():
                    library_paths.append(str(library_path))
                    self.log.info(f"Found additional Steam library: {library_path}")

        except Exception as e:
            self.log.error(f"Error parsing libraryfolders.vdf: {str(e)}")

        return library_paths
