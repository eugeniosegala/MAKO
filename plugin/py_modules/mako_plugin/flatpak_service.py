"""
Flatpak service for managing MAKO Renderer Flatpak runtime extensions.
"""

import subprocess
import os
from pathlib import Path
from typing import Dict, Any, List, Optional

from .base_service import BaseService
from .config_schema import ConfigurationManager
from .constants import (
    BIN_DIR,
    FLATPAK_23_08_FILENAME,
    FLATPAK_24_08_FILENAME,
    FLATPAK_25_08_FILENAME,
    FLATPAK_EXTENSION_NAME,
    FLATPAK_IMPLICIT_LAYER_DIR,
)
from .types import BaseResponse


class FlatpakExtensionStatus(BaseResponse):
    """Response for Flatpak extension status"""
    def __init__(self, success: bool = False, message: str = "", error: str = "",
                 installed_23_08: bool = False, installed_24_08: bool = False, installed_25_08: bool = False):
        super().__init__(success, message, error)
        self.installed_23_08 = installed_23_08
        self.installed_24_08 = installed_24_08
        self.installed_25_08 = installed_25_08


class FlatpakAppInfo(BaseResponse):
    """Response for Flatpak app information"""
    def __init__(self, success: bool = False, message: str = "", error: str = "",
                 apps: List[Dict[str, Any]] = None, total_apps: int = 0):
        super().__init__(success, message, error)
        self.apps = apps or []
        self.total_apps = total_apps


class FlatpakOverrideResponse(BaseResponse):
    """Response for Flatpak override operations"""
    def __init__(self, success: bool = False, message: str = "", error: str = "",
                 app_id: str = "", operation: str = ""):
        super().__init__(success, message, error)
        self.app_id = app_id
        self.operation = operation


class FlatpakService(BaseService):
    """Service for handling Flatpak runtime extensions and app overrides"""

    def __init__(self, logger=None):
        super().__init__(logger)
        self.extension_id_23_08 = f"{FLATPAK_EXTENSION_NAME}/x86_64/23.08"
        self.extension_id_24_08 = f"{FLATPAK_EXTENSION_NAME}/x86_64/24.08"
        self.extension_id_25_08 = f"{FLATPAK_EXTENSION_NAME}/x86_64/25.08"
        self.flatpak_command = None

    def _get_mako_paths(self) -> tuple[str, str]:
        """Return the config directory and read-only directory containing Lossless.dll.

        Upstream's Flatpak guide grants the Steam common directory so the sandbox can
        load Lossless Scaling. If the user selected a custom DLL path, grant that
        DLL's directory instead.
        """
        home_path = os.path.expanduser("~")
        config_path = str(self.config_dir)
        dll_directory = f"{home_path}/.local/share/Steam/steamapps/common"

        if not self.config_file_path.exists():
            return config_path, dll_directory

        try:
            profile_data = ConfigurationManager.parse_toml_content_multi_profile(
                self.config_file_path.read_text(encoding="utf-8")
            )
            configured_dll = profile_data["global_config"].get("dll", "")
            if configured_dll:
                dll_directory = str(Path(str(configured_dll)).parent)
        except Exception as error:
            self.log.debug("Could not read configured DLL path for Flatpak override: %s", error)

        return config_path, dll_directory

    def _get_clean_env(self):
        """Get a clean environment without PyInstaller's bundled libraries"""
        env = os.environ.copy()

        if 'LD_LIBRARY_PATH' in env:
            del env['LD_LIBRARY_PATH']

        standard_paths = ['/usr/bin', '/usr/local/bin', '/bin']
        current_path = env.get('PATH', '')

        path_parts = current_path.split(':') if current_path else []
        for std_path in standard_paths:
            if std_path not in path_parts:
                path_parts.insert(0, std_path)

        env['PATH'] = ':'.join(path_parts)

        return env

    def _get_extension_id(self, version: str) -> Optional[str]:
        """Return the isolated experimental extension reference for a runtime."""
        extension_ids = {
            "23.08": self.extension_id_23_08,
            "24.08": self.extension_id_24_08,
            "25.08": self.extension_id_25_08,
        }
        return extension_ids.get(version)

    def _get_app_runtime_version(self, app_id: str) -> Optional[str]:
        """Return a supported Flatpak runtime branch for an application."""
        result = self._run_flatpak_command(
            ["info", "--show-runtime", app_id],
            capture_output=True, text=True
        )
        if result.returncode != 0:
            return None

        runtime = result.stdout.strip()
        for version in ("23.08", "24.08", "25.08"):
            if runtime.endswith(f"/{version}") or runtime.endswith(f"//{version}"):
                return version
        return None

    def _is_extension_installed(self, version: str) -> bool:
        """Check whether the isolated experimental extension is installed."""
        extension_id = self._get_extension_id(version)
        if extension_id is None:
            return False
        result = self._run_flatpak_command(
            ["info", "--user", extension_id],
            capture_output=True, text=True
        )
        return result.returncode == 0

    def _run_flatpak_command(self, args: List[str], **kwargs):
        """Run flatpak command with clean environment to avoid library conflicts"""
        if self.flatpak_command is None:
            raise FileNotFoundError("Flatpak command not available")

        env = self._get_clean_env()

        self.log.info(f"Running flatpak with PATH: {env.get('PATH')}")
        self.log.info(f"LD_LIBRARY_PATH removed: {'LD_LIBRARY_PATH' not in env}")

        return subprocess.run([self.flatpak_command] + args, env=env, **kwargs)

    def check_flatpak_available(self) -> bool:
        """Check if flatpak command is available and store the working command"""
        self.log.info(f"PATH: {os.environ.get('PATH', 'Not set')}")
        self.log.info(f"HOME: {os.environ.get('HOME', 'Not set')}")
        self.log.info(f"USER: {os.environ.get('USER', 'Not set')}")

        flatpak_paths = [
            "flatpak",
            "/usr/bin/flatpak",
            "/var/lib/flatpak/exports/bin/flatpak",
            "/home/deck/.local/bin/flatpak"
        ]

        for flatpak_path in flatpak_paths:
            try:
                result = subprocess.run([flatpak_path, "--version"],
                                      capture_output=True, check=True, text=True,
                                      env=self._get_clean_env())
                self.log.info(f"Flatpak found at {flatpak_path}: {result.stdout.strip()}")
                self.flatpak_command = flatpak_path
                return True
            except (subprocess.CalledProcessError, FileNotFoundError):
                self.log.debug(f"Flatpak not found at {flatpak_path}")
                continue

        self.log.error("Flatpak command not found in any known locations")
        self.flatpak_command = None
        return False

    def get_extension_status(self) -> FlatpakExtensionStatus:
        """Check if MAKO Renderer Flatpak extensions are installed"""
        try:
            if not self.check_flatpak_available():
                error_msg = "Flatpak is not available on this system"
                if self.flatpak_command is None:
                    error_msg += ". Command not found in PATH or common install locations."
                self.log.error(error_msg)
                return self._error_response(FlatpakExtensionStatus,
                                          error_msg,
                                          installed_23_08=False, installed_24_08=False, installed_25_08=False)

            result = self._run_flatpak_command(
                ["list", "--runtime"],
                capture_output=True, text=True, check=True
            )

            installed_runtimes = result.stdout

            base_extension_name = FLATPAK_EXTENSION_NAME
            installed_23_08 = False
            installed_24_08 = False
            installed_25_08 = False

            for line in installed_runtimes.split('\n'):
                if base_extension_name in line:
                    if "23.08" in line:
                        installed_23_08 = True
                    elif "24.08" in line:
                        installed_24_08 = True
                    elif "25.08" in line:
                        installed_25_08 = True

            status_msg = []
            if installed_23_08:
                status_msg.append("23.08 runtime extension installed")
            if installed_24_08:
                status_msg.append("24.08 runtime extension installed")
            if installed_25_08:
                status_msg.append("25.08 runtime extension installed")

            if not status_msg:
                status_msg.append("No MAKO Renderer runtime extensions installed")

            return self._success_response(FlatpakExtensionStatus,
                                        "; ".join(status_msg),
                                        installed_23_08=installed_23_08,
                                        installed_24_08=installed_24_08,
                                        installed_25_08=installed_25_08)

        except subprocess.CalledProcessError as e:
            error_msg = f"Error checking Flatpak extensions: {e.stderr if e.stderr else str(e)}"
            self.log.error(error_msg)
            return self._error_response(FlatpakExtensionStatus, error_msg,
                                      installed_23_08=False, installed_24_08=False, installed_25_08=False)

    def install_extension(self, version: str) -> BaseResponse:
        """Install or refresh a specific MAKO Renderer Flatpak runtime extension."""
        try:
            if version not in ["23.08", "24.08", "25.08"]:
                return self._error_response(BaseResponse, "Invalid version. Must be '23.08', '24.08', or '25.08'")

            if not self.check_flatpak_available():
                return self._error_response(BaseResponse, "Flatpak is not available on this system")

            plugin_dir = Path(__file__).parent.parent.parent
            filenames = {
                "23.08": FLATPAK_23_08_FILENAME,
                "24.08": FLATPAK_24_08_FILENAME,
                "25.08": FLATPAK_25_08_FILENAME,
            }
            flatpak_path = plugin_dir / BIN_DIR / filenames[version]

            if not flatpak_path.is_file():
                return self._error_response(
                    BaseResponse,
                    "Experimental Flatpak bundle is missing from this plugin package. "
                    "Install a release that includes Flatpak support.",
                )

            was_installed = self._is_extension_installed(version)
            install_args = ["install", "--user", "--noninteractive"]
            if was_installed:
                # The plugin ZIP can carry a newer engine with the same Flatpak
                # extension ID/runtime branch. Reinstall in place so Heroic's
                # preparation and its per-game wrapper commands remain intact.
                install_args.append("--reinstall")
            install_args.append(str(flatpak_path))
            result = self._run_flatpak_command(
                install_args,
                capture_output=True, text=True
            )

            if result.returncode != 0:
                error_msg = f"Failed to install Flatpak extension: {result.stderr}"
                self.log.error(error_msg)
                return self._error_response(BaseResponse, error_msg)

            action = "updated" if was_installed else "installed"
            self.log.info(f"Successfully {action} MAKO Renderer Flatpak extension {version}")
            return self._success_response(
                BaseResponse,
                f"MAKO Renderer {version} runtime extension {action} successfully"
            )

        except Exception as e:
            error_msg = f"Error installing Flatpak extension {version}: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(BaseResponse, error_msg)

    def uninstall_extension(self, version: str) -> BaseResponse:
        """Uninstall a specific version of the MAKO Renderer Flatpak extension"""
        try:
            if version not in ["23.08", "24.08", "25.08"]:
                return self._error_response(BaseResponse, "Invalid version. Must be '23.08', '24.08', or '25.08'")

            if not self.check_flatpak_available():
                return self._error_response(BaseResponse, "Flatpak is not available on this system")

            extension_id = self._get_extension_id(version)
            if extension_id is None:
                return self._error_response(BaseResponse, f"Unsupported Flatpak runtime: {version}")

            result = self._run_flatpak_command(
                ["uninstall", "--user", "--noninteractive", extension_id],
                capture_output=True, text=True
            )

            if result.returncode != 0:
                error_msg = f"Failed to uninstall Flatpak extension: {result.stderr}"
                self.log.error(error_msg)
                return self._error_response(BaseResponse, error_msg)

            self.log.info(f"Successfully uninstalled MAKO Renderer Flatpak extension {version}")
            return self._success_response(BaseResponse, f"MAKO Renderer {version} runtime extension uninstalled successfully")

        except Exception as e:
            error_msg = f"Error uninstalling Flatpak extension {version}: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(BaseResponse, error_msg)

    def get_flatpak_apps(self) -> FlatpakAppInfo:
        """Get list of installed Flatpak apps and their MAKO Renderer override status"""
        try:
            if not self.check_flatpak_available():
                error_msg = "Flatpak is not available on this system"
                if self.flatpak_command is None:
                    error_msg += ". Command not found in PATH or common install locations."
                return self._error_response(FlatpakAppInfo,
                                          error_msg,
                                          apps=[], total_apps=0)

            result = self._run_flatpak_command(
                ["list", "--app"],
                capture_output=True, text=True, check=True
            )

            apps = []
            for line in result.stdout.strip().split('\n'):
                if not line.strip():
                    continue

                parts = line.split('\t')
                if len(parts) >= 2:
                    app_name = parts[0].strip()
                    app_id = parts[1].strip()

                    # Check override status
                    override_status = self._check_app_override_status(app_id)

                    apps.append({
                        "app_id": app_id,
                        "app_name": app_name,
                        "wrapper_path": str(self.mako_launch_script_path),
                        "has_filesystem_override": override_status["filesystem"],
                        "has_wrapper_override": override_status["wrapper"],
                        "has_env_override": override_status["legacy_env"],
                    })

            return self._success_response(FlatpakAppInfo,
                                        f"Found {len(apps)} Flatpak applications",
                                        apps=apps, total_apps=len(apps))

        except subprocess.CalledProcessError as e:
            error_msg = f"Error getting Flatpak apps: {e.stderr if e.stderr else str(e)}"
            self.log.error(error_msg)
            return self._error_response(FlatpakAppInfo, error_msg, apps=[], total_apps=0)

    def _check_app_override_status(self, app_id: str) -> Dict[str, bool]:
        """Check whether an app can execute the per-game experimental wrapper."""
        try:
            result = self._run_flatpak_command(
                ["override", "--user", "--show", app_id],
                capture_output=True, text=True
            )

            if result.returncode != 0:
                return {"filesystem": False, "wrapper": False, "legacy_env": False}

            output = result.stdout
            config_path, dll_directory = self._get_mako_paths()
            wrapper_path = str(self.mako_launch_script_path)

            filesystem_section = ""
            in_context = False

            for line in output.split('\n'):
                line = line.strip()
                if line == "[Context]":
                    in_context = True
                elif line.startswith("[") and line != "[Context]":
                    in_context = False
                elif in_context and line.startswith("filesystems="):
                    filesystem_section = line
                    break

            has_config_fs = self._filesystem_override_present(filesystem_section, config_path)
            has_dll_fs = self._filesystem_override_present(filesystem_section, dll_directory)
            has_wrapper_fs = self._filesystem_override_present(filesystem_section, wrapper_path)

            filesystem_override = has_config_fs and has_dll_fs

            has_mako_config_env = False
            has_isolated_layer_env = False
            in_environment = False

            for line in output.split('\n'):
                line = line.strip()
                if line == "[Environment]":
                    in_environment = True
                elif line.startswith("[") and line != "[Environment]":
                    in_environment = False
                elif in_environment and line.startswith(f"MAKO_CONFIG={config_path}/conf.toml"):
                    has_mako_config_env = True
                elif in_environment and (
                    line.startswith(f"VK_IMPLICIT_LAYER_PATH={FLATPAK_IMPLICIT_LAYER_DIR}")
                    or line.startswith(f"VK_ADD_IMPLICIT_LAYER_PATH={FLATPAK_IMPLICIT_LAYER_DIR}")
                ):
                    has_isolated_layer_env = True

            legacy_env_override = has_mako_config_env or has_isolated_layer_env

            self.log.debug(
                "Override status for %s: resources=%s (%s/%s), wrapper=%s, legacy_env=%s (%s/%s)",
                app_id,
                filesystem_override,
                has_config_fs,
                has_dll_fs,
                has_wrapper_fs,
                legacy_env_override,
                has_mako_config_env,
                has_isolated_layer_env,
            )

            return {
                "filesystem": filesystem_override,
                "wrapper": has_wrapper_fs,
                "legacy_env": legacy_env_override,
            }

        except Exception as e:
            self.log.error(f"Error checking override status for {app_id}: {e}")
            return {"filesystem": False, "wrapper": False, "legacy_env": False}

    def _filesystem_override_present(self, filesystem_section: str, host_path: str) -> bool:
        """Match Flatpak's absolute or home-relative permission representation.

        ``flatpak override --show`` may render a user-home path as ``~/.…``
        even though the plugin originally set it as an absolute path. Accept
        both forms so a successfully prepared Heroic app is not shown as off.
        A leading ``!`` is Flatpak's explicit denial form and must not count as
        an enabled permission after the user turns the toggle off.
        """
        try:
            relative_path = Path(host_path).relative_to(self.user_home)
        except ValueError:
            relative_path = None

        accepted_paths = {host_path}
        if relative_path is not None:
            accepted_paths.add(f"~/{relative_path.as_posix()}")

        _, _, raw_entries = filesystem_section.partition("=")
        enabled = False
        for entry in raw_entries.split(";"):
            entry = entry.strip()
            if not entry:
                continue
            denied = entry.startswith("!")
            permission_path = entry[1:] if denied else entry
            permission_path = permission_path.split(":", 1)[0]
            if permission_path in accepted_paths:
                if denied:
                    return False
                enabled = True

        return enabled

    def set_app_override(self, app_id: str) -> FlatpakOverrideResponse:
        """Set MAKO Renderer overrides for a Flatpak app"""
        try:
            if not self.check_flatpak_available():
                return self._error_response(FlatpakOverrideResponse,
                                          "Flatpak is not available on this system",
                                          app_id=app_id, operation="set")

            runtime_version = self._get_app_runtime_version(app_id)
            if runtime_version is None:
                return self._error_response(
                    FlatpakOverrideResponse,
                    "Could not determine a supported Flatpak runtime for this application. "
                    "Install the matching experimental runtime extension first.",
                    app_id=app_id,
                    operation="set",
                )
            if not self._is_extension_installed(runtime_version):
                return self._error_response(
                    FlatpakOverrideResponse,
                    f"Install the experimental {runtime_version} runtime extension before enabling this application.",
                    app_id=app_id,
                    operation="set",
                )

            if not self.mako_launch_script_path.is_file():
                return self._error_response(
                    FlatpakOverrideResponse,
                    "Install MAKO Renderer before preparing a Flatpak application.",
                    app_id=app_id,
                    operation="set",
                )

            config_path, dll_directory = self._get_mako_paths()
            wrapper_path = str(self.mako_launch_script_path)

            filesystem_overrides = [
                f"--filesystem={config_path}:rw",
                f"--filesystem={dll_directory}:ro",
                f"--filesystem={wrapper_path}:ro",
            ]

            for override in filesystem_overrides:
                result = self._run_flatpak_command(
                    ["override", "--user", override, app_id],
                    capture_output=True, text=True
                )
                if result.returncode != 0:
                    error_msg = f"Failed to set filesystem override {override}: {result.stderr}"
                    return self._error_response(FlatpakOverrideResponse, error_msg,
                                              app_id=app_id, operation="set")

            # Older experimental versions activated the layer globally for the
            # Flatpak app. Remove those values during upgrade: the mounted
            # wrapper now applies them only to an individual Heroic game.
            for variable in (
                "MAKO_CONFIG",
                "VK_IMPLICIT_LAYER_PATH",
                "VK_ADD_IMPLICIT_LAYER_PATH",
            ):
                result = self._run_flatpak_command(
                    ["override", "--user", f"--unset-env={variable}", app_id],
                    capture_output=True, text=True
                )
                if result.returncode != 0:
                    error_msg = f"Failed to clear legacy environment override {variable}: {result.stderr}"
                    return self._error_response(FlatpakOverrideResponse, error_msg,
                                              app_id=app_id, operation="set")

            self.log.info(f"Prepared per-game MAKO Renderer wrapper access for {app_id}")
            return self._success_response(FlatpakOverrideResponse,
                                        f"MAKO Renderer per-game wrapper access prepared for {app_id}",
                                        app_id=app_id, operation="set")

        except Exception as e:
            error_msg = f"Error setting overrides for {app_id}: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(FlatpakOverrideResponse, error_msg,
                                      app_id=app_id, operation="set")

    def remove_app_override(self, app_id: str) -> FlatpakOverrideResponse:
        """Remove MAKO Renderer overrides for a Flatpak app"""
        try:
            if not self.check_flatpak_available():
                return self._error_response(FlatpakOverrideResponse,
                                          "Flatpak is not available on this system",
                                          app_id=app_id, operation="remove")

            config_path, dll_directory = self._get_mako_paths()
            wrapper_path = str(self.mako_launch_script_path)

            filesystem_overrides = [
                f"--nofilesystem={dll_directory}",
                f"--nofilesystem={config_path}",
                f"--nofilesystem={wrapper_path}",
            ]

            removal_errors = []

            # Remove filesystem overrides
            for override in filesystem_overrides:
                result = self._run_flatpak_command(
                    ["override", "--user", override, app_id],
                    capture_output=True, text=True
                )
                if result.returncode != 0:
                    removal_errors.append(f"{override}: {result.stderr}")

            for variable in (
                "MAKO_CONFIG",
                "VK_IMPLICIT_LAYER_PATH",
                "VK_ADD_IMPLICIT_LAYER_PATH",
            ):
                result = self._run_flatpak_command(
                    ["override", "--user", f"--unset-env={variable}", app_id],
                    capture_output=True, text=True
                )

                if result.returncode != 0:
                    removal_errors.append(f"unset-env {variable}: {result.stderr}")

            if removal_errors:
                self.log.warning(f"Some override removals had issues for {app_id}: {'; '.join(removal_errors)}")

            self.log.info(f"Completed override removal for {app_id}")
            return self._success_response(FlatpakOverrideResponse,
                                        f"MAKO Renderer overrides removed for {app_id}",
                                        app_id=app_id, operation="remove")

        except Exception as e:
            error_msg = f"Error removing overrides for {app_id}: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(FlatpakOverrideResponse, error_msg,
                                      app_id=app_id, operation="remove")
