"""
Flatpak service for managing MAKO Renderer Flatpak runtime extensions.
"""

import subprocess
import os
import re
from pathlib import Path
from typing import Dict, List, Optional, TypedDict

from .base_service import BaseService
from .config_schema import ConfigurationManager
from .constants import (
    BIN_DIR,
    COMPETING_LSFG_DISABLE_ENVS,
    CONFIG_FILENAME,
    DXVK_HDR_ENV,
    FLATPAK_EXTENSION_NAME,
    FLATPAK_EXTENSION_PREFIX,
    FLATPAK_HOST_ARCHITECTURE,
    FLATPAK_IMPLICIT_LAYER_DIR,
    FLATPAK_RUNTIME_BUNDLES,
    GAMESCOPE_WSI_DISABLE_ENV,
    GAMESCOPE_WSI_ENABLE_ENV,
    HDR_EXPOSURE_DISABLE_ENV,
    MAKO_CONFIG_ENV,
    MAKO_LAYER_ENABLE_ENV,
    PER_GAME_WRAPPER_FLATPAK_APPS,
    PLUGIN_ROOT,
    VK_ADD_IMPLICIT_LAYER_PATH_ENV,
    VK_IMPLICIT_LAYER_PATH_ENV,
)
from .host_environment import detect_host_environment
from .types import ServiceResponse


_SUPPORTED_FREEDESKTOP_VULKAN_LAYER_VERSIONS = tuple(FLATPAK_RUNTIME_BUNDLES)
_SUPPORTED_VERSION_ERROR = (
    "Invalid version. Must be "
    + ", ".join(
        f"'{version}'"
        for version in _SUPPORTED_FREEDESKTOP_VULKAN_LAYER_VERSIONS[:-1]
    )
    + f", or '{_SUPPORTED_FREEDESKTOP_VULKAN_LAYER_VERSIONS[-1]}'"
)
_FREEDESKTOP_VULKAN_LAYER_EXTENSION = (
    "org.freedesktop.Platform.VulkanLayer"
)
# Heroic starts each game in a child compatibility environment. Its per-game
# wrapper must set MAKO values there, rather than enabling the layer for the
# entire Heroic UI. Direct Flatpak launches (such as EmuDeck's Dolphin
# shortcuts) do not have that child boundary: Flatpak's persisted
# ``unset-environment`` rules otherwise clear the wrapper's config and Vulkan
# path before the app starts.
_LAYER_ENVIRONMENT_VARIABLES = (
    MAKO_CONFIG_ENV,
    MAKO_LAYER_ENABLE_ENV,
    *COMPETING_LSFG_DISABLE_ENVS,
    GAMESCOPE_WSI_DISABLE_ENV,
    GAMESCOPE_WSI_ENABLE_ENV,
    HDR_EXPOSURE_DISABLE_ENV,
    DXVK_HDR_ENV,
    VK_IMPLICIT_LAYER_PATH_ENV,
    VK_ADD_IMPLICIT_LAYER_PATH_ENV,
)


FlatpakExtensionStatus = TypedDict(
    "FlatpakExtensionStatus",
    {
        "success": bool,
        "message": str,
        "error": Optional[str],
        **{
            f"installed_{version.replace('.', '_')}": bool
            for version in _SUPPORTED_FREEDESKTOP_VULKAN_LAYER_VERSIONS
        },
    },
)


def _empty_extension_status_fields() -> Dict[str, bool]:
    return {
        f"installed_{version.replace('.', '_')}": False
        for version in _SUPPORTED_FREEDESKTOP_VULKAN_LAYER_VERSIONS
    }


class FlatpakOverrideStatus(TypedDict):
    """Internal normalized state read from one Flatpak app override."""

    filesystem: bool
    wrapper: bool
    legacy_env: bool
    mako_env: bool
    required_env: bool


def _empty_app_override_status() -> FlatpakOverrideStatus:
    return {
        "filesystem": False,
        "wrapper": False,
        "legacy_env": False,
        "mako_env": False,
        "required_env": False,
    }


class FlatpakRefreshRequiredResult(TypedDict):
    """Fields emitted by every installed-runtime refresh path."""

    success: bool
    updated_versions: List[str]


class FlatpakRefreshResult(FlatpakRefreshRequiredResult, total=False):
    message: str
    error: str


class FlatpakCleanupRequiredResult(TypedDict):
    """Fields emitted by every incompatible-override cleanup path."""

    success: bool
    disabled_apps: List[str]


class FlatpakCleanupResult(FlatpakCleanupRequiredResult, total=False):
    message: str
    error: str


class FlatpakApp(TypedDict):
    """One installed Flatpak app and its MAKO override state."""

    app_id: str
    app_name: str
    wrapper_path: str
    has_filesystem_override: bool
    has_wrapper_override: bool
    has_env_override: bool
    has_required_env_override: bool


class FlatpakAppInfo(ServiceResponse):
    """Response for Flatpak app information"""

    apps: List[FlatpakApp]
    total_apps: int


class FlatpakOverrideResponse(ServiceResponse, total=False):
    """Response for Flatpak override operations"""

    app_id: str
    operation: str


class FlatpakService(BaseService):
    """Service for handling Flatpak runtime extensions and app overrides"""

    def __init__(self, logger=None):
        super().__init__(logger)
        for version, bundle in FLATPAK_RUNTIME_BUNDLES.items():
            setattr(
                self,
                f"extension_id_{version.replace('.', '_')}",
                bundle.extension_id,
            )
        self.flatpak_command = None

    def _host_architecture_supported(self) -> bool:
        """Return whether this release's Flatpak payload matches the host ISA."""
        return (
            detect_host_environment(self.log).native_architecture
            == FLATPAK_HOST_ARCHITECTURE
        )

    @staticmethod
    def _unsupported_host_error() -> str:
        return (
            "MAKO Flatpak activation is disabled on native AArch64/Armada in "
            "this release because the bundled runtime extensions are x86_64."
        )

    def _get_mako_paths(self) -> tuple[str, str]:
        """Return the config directory and read-only directory containing Lossless.dll.

        Upstream's Flatpak guide grants the Steam common directory so the sandbox can
        load Lossless Scaling. If the user selected a custom DLL path, grant that
        DLL's directory instead.
        """
        config_path = str(self.config_dir)
        dll_directory = str(
            self.user_home / ".local" / "share" / "Steam" / "steamapps" / "common"
        )

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
        """Return the isolated MAKO extension reference for a runtime."""
        if version not in FLATPAK_RUNTIME_BUNDLES:
            return None
        return getattr(self, f"extension_id_{version.replace('.', '_')}")

    def _get_app_runtime_version(self, app_id: str) -> Optional[str]:
        """Return the Freedesktop Vulkan-layer version usable by an app.

        Applications based directly on Freedesktop use the runtime branch as
        their layer-extension version. Derived runtimes, including KDE 6.10
        used by current Dolphin Flatpaks, expose the inherited VulkanLayer
        extension point in their runtime metadata. That point declares the
        matching Freedesktop base version (for example, KDE 6.10 -> 25.08).
        """
        result = self._run_flatpak_command(
            ["info", "--show-runtime", app_id],
            capture_output=True, text=True
        )
        if result.returncode != 0:
            return None

        runtime = result.stdout.strip()
        if runtime.startswith("org.freedesktop.Platform/"):
            version = runtime.rsplit("/", 1)[-1]
            if version in _SUPPORTED_FREEDESKTOP_VULKAN_LAYER_VERSIONS:
                return version

        metadata_result = self._run_flatpak_command(
            ["info", "--show-metadata", runtime],
            capture_output=True,
            text=True,
        )
        if metadata_result.returncode != 0:
            self.log.debug(
                "Could not inspect Flatpak runtime metadata for %s: %s",
                runtime,
                metadata_result.stderr,
            )
            return None

        version = self._get_inherited_vulkan_layer_version(metadata_result.stdout)
        if version is not None:
            self.log.debug(
                "Flatpak app %s uses runtime %s with Freedesktop VulkanLayer %s",
                app_id,
                runtime,
                version,
            )
        return version

    @staticmethod
    def _get_inherited_vulkan_layer_version(metadata: str) -> Optional[str]:
        """Read the compatible Freedesktop Vulkan-layer branch from metadata."""
        section = re.search(
            rf"(?ms)^\[Extension {re.escape(_FREEDESKTOP_VULKAN_LAYER_EXTENSION)}\]$"
            r"(.*?)(?=^\[|\Z)",
            metadata,
        )
        if section is None:
            return None

        values = {}
        for line in section.group(1).splitlines():
            key, separator, value = line.partition("=")
            if separator:
                values[key.strip()] = value.strip()

        candidates = []
        if "version" in values:
            candidates.append(values["version"])
        if "versions" in values:
            candidates.extend(values["versions"].split(";"))

        for version in candidates:
            if version in _SUPPORTED_FREEDESKTOP_VULKAN_LAYER_VERSIONS:
                return version
        return None

    def _is_extension_installed(self, version: str) -> bool:
        """Check whether the isolated MAKO extension is installed."""
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

        flatpak_paths = dict.fromkeys([
            "flatpak",
            "/usr/bin/flatpak",
            "/var/lib/flatpak/exports/bin/flatpak",
            str(self.user_home / ".local" / "bin" / "flatpak"),
        ])

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
                                          **_empty_extension_status_fields())

            result = self._run_flatpak_command(
                ["list", "--runtime"],
                capture_output=True, text=True, check=True
            )

            installed_runtimes = result.stdout

            base_extension_name = FLATPAK_EXTENSION_NAME
            installed_by_version = {
                version: False
                for version in _SUPPORTED_FREEDESKTOP_VULKAN_LAYER_VERSIONS
            }

            for line in installed_runtimes.split('\n'):
                if base_extension_name in line:
                    for version in _SUPPORTED_FREEDESKTOP_VULKAN_LAYER_VERSIONS:
                        if version in line:
                            installed_by_version[version] = True
                            break

            status_msg = [
                f"{version} runtime extension installed"
                for version in _SUPPORTED_FREEDESKTOP_VULKAN_LAYER_VERSIONS
                if installed_by_version[version]
            ]

            if not status_msg:
                status_msg.append("No MAKO Renderer runtime extensions installed")

            status_fields = {
                f"installed_{version.replace('.', '_')}": installed
                for version, installed in installed_by_version.items()
            }
            return self._success_response(FlatpakExtensionStatus,
                                        "; ".join(status_msg),
                                        **status_fields)

        except subprocess.CalledProcessError as e:
            error_msg = f"Error checking Flatpak extensions: {e.stderr if e.stderr else str(e)}"
            self.log.error(error_msg)
            return self._error_response(FlatpakExtensionStatus, error_msg,
                                      **_empty_extension_status_fields())

    def install_extension(self, version: str) -> ServiceResponse:
        """Install or refresh a specific MAKO Renderer Flatpak runtime extension."""
        try:
            if not self._host_architecture_supported():
                return self._error_response(
                    ServiceResponse, self._unsupported_host_error()
                )
            if version not in FLATPAK_RUNTIME_BUNDLES:
                return self._error_response(ServiceResponse, _SUPPORTED_VERSION_ERROR)

            if not self.check_flatpak_available():
                return self._error_response(ServiceResponse, "Flatpak is not available on this system")

            flatpak_path = (
                PLUGIN_ROOT / BIN_DIR / FLATPAK_RUNTIME_BUNDLES[version].filename
            )

            if not flatpak_path.is_file():
                return self._error_response(
                    ServiceResponse,
                    "Flatpak bundle is missing from this plugin package. "
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
                return self._error_response(ServiceResponse, error_msg)

            action = "updated" if was_installed else "installed"
            self.log.info(f"Successfully {action} MAKO Renderer Flatpak extension {version}")
            return self._success_response(
                ServiceResponse,
                f"MAKO Renderer {version} runtime extension {action} successfully"
            )

        except Exception as e:
            error_msg = f"Error installing Flatpak extension {version}: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(ServiceResponse, error_msg)

    def refresh_installed_extensions(self) -> FlatpakRefreshResult:
        """Replace installed MAKO runtime branches with this plugin's payloads.

        Flatpak runtime branches keep a stable extension ID across renderer
        releases. Installing a newer Decky ZIP therefore does not update them
        automatically unless they are explicitly reinstalled. Refresh only
        branches already present on the machine; first-time setup remains an
        explicit choice in Flatpak Setup.
        """
        if not self._host_architecture_supported():
            return {
                "success": False,
                "updated_versions": [],
                "error": self._unsupported_host_error(),
            }
        if not self.check_flatpak_available():
            return {
                "success": False,
                "updated_versions": [],
                "error": "Flatpak is not available on this system",
            }

        installed_versions = [
            version
            for version in _SUPPORTED_FREEDESKTOP_VULKAN_LAYER_VERSIONS
            if self._is_extension_installed(version)
        ]
        if not installed_versions:
            return {
                "success": True,
                "updated_versions": [],
                "message": "No installed MAKO Flatpak runtimes needed refreshing",
            }

        updated_versions = []
        failures = []
        for version in installed_versions:
            result = self.install_extension(version)
            if result.get("success"):
                updated_versions.append(version)
            else:
                failures.append(
                    f"{version}: {result.get('error') or 'unknown error'}"
                )

        if failures:
            return {
                "success": False,
                "updated_versions": updated_versions,
                "error": "; ".join(failures),
            }

        return {
            "success": True,
            "updated_versions": updated_versions,
            "message": (
                "Refreshed MAKO Flatpak runtime extensions: "
                + ", ".join(updated_versions)
            ),
        }

    def uninstall_extension(self, version: str) -> ServiceResponse:
        """Uninstall a specific version of the MAKO Renderer Flatpak extension"""
        try:
            if version not in FLATPAK_RUNTIME_BUNDLES:
                return self._error_response(ServiceResponse, _SUPPORTED_VERSION_ERROR)

            if not self.check_flatpak_available():
                return self._error_response(ServiceResponse, "Flatpak is not available on this system")

            extension_id = self._get_extension_id(version)
            if extension_id is None:
                return self._error_response(ServiceResponse, f"Unsupported Flatpak runtime: {version}")

            result = self._run_flatpak_command(
                ["uninstall", "--user", "--noninteractive", extension_id],
                capture_output=True, text=True
            )

            if result.returncode != 0:
                error_msg = f"Failed to uninstall Flatpak extension: {result.stderr}"
                self.log.error(error_msg)
                return self._error_response(ServiceResponse, error_msg)

            self.log.info(f"Successfully uninstalled MAKO Renderer Flatpak extension {version}")
            return self._success_response(ServiceResponse, f"MAKO Renderer {version} runtime extension uninstalled successfully")

        except Exception as e:
            error_msg = f"Error uninstalling Flatpak extension {version}: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(ServiceResponse, error_msg)

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
                        "wrapper_path": str(self.mako_script_path),
                        "has_filesystem_override": override_status["filesystem"],
                        "has_wrapper_override": override_status["wrapper"],
                        "has_env_override": override_status["legacy_env"],
                        "has_required_env_override": override_status["required_env"],
                    })

            return self._success_response(FlatpakAppInfo,
                                        f"Found {len(apps)} Flatpak applications",
                                        apps=apps, total_apps=len(apps))

        except subprocess.CalledProcessError as e:
            error_msg = f"Error getting Flatpak apps: {e.stderr if e.stderr else str(e)}"
            self.log.error(error_msg)
            return self._error_response(FlatpakAppInfo, error_msg, apps=[], total_apps=0)

    def _check_app_override_status(
            self, app_id: str) -> FlatpakOverrideStatus:
        """Check whether an app has the required MAKO layer access."""
        try:
            result = self._run_flatpak_command(
                ["override", "--user", "--show", app_id],
                capture_output=True, text=True
            )

            if result.returncode != 0:
                return _empty_app_override_status()

            output = result.stdout
            config_path, dll_directory = self._get_mako_paths()
            wrapper_path = str(self.mako_script_path)

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
            has_gamescope_wsi_fs = self._filesystem_override_present(
                filesystem_section,
                str(self.gamescope_wsi_compatibility_dir),
            )

            filesystem_override = (
                has_config_fs and has_dll_fs and has_gamescope_wsi_fs
            )

            environment_values = {}
            in_environment = False

            for line in output.split('\n'):
                line = line.strip()
                if line == "[Environment]":
                    in_environment = True
                elif line.startswith("[") and line != "[Environment]":
                    in_environment = False
                elif in_environment:
                    key, separator, value = line.partition("=")
                    if separator:
                        environment_values[key] = value

            legacy_env_override = any(
                variable in environment_values
                for variable in _LAYER_ENVIRONMENT_VARIABLES
            )
            mako_env_override = (
                MAKO_CONFIG_ENV in environment_values
                or MAKO_LAYER_ENABLE_ENV in environment_values
                or HDR_EXPOSURE_DISABLE_ENV in environment_values
                or environment_values.get(VK_IMPLICIT_LAYER_PATH_ENV)
                == FLATPAK_IMPLICIT_LAYER_DIR
                or FLATPAK_EXTENSION_PREFIX
                in environment_values.get(VK_ADD_IMPLICIT_LAYER_PATH_ENV, "")
            )
            compatible_layer_path = (
                environment_values.get(VK_IMPLICIT_LAYER_PATH_ENV) ==
                FLATPAK_IMPLICIT_LAYER_DIR
                and not environment_values.get(VK_ADD_IMPLICIT_LAYER_PATH_ENV)
            )
            required_env_override = (
                app_id in PER_GAME_WRAPPER_FLATPAK_APPS
                or (
                    environment_values.get(MAKO_CONFIG_ENV) ==
                    f"{config_path}/{CONFIG_FILENAME}"
                    and environment_values.get(MAKO_LAYER_ENABLE_ENV) == "1"
                    and all(
                        environment_values.get(variable) == "1"
                        for variable in COMPETING_LSFG_DISABLE_ENVS
                    )
                    and environment_values.get(GAMESCOPE_WSI_DISABLE_ENV) == "1"
                    and not environment_values.get(GAMESCOPE_WSI_ENABLE_ENV)
                    and environment_values.get(HDR_EXPOSURE_DISABLE_ENV) == "1"
                    and not environment_values.get(DXVK_HDR_ENV)
                    and compatible_layer_path
                )
            )

            self.log.debug(
                "Override status for %s: resources=%s (%s/%s/%s), wrapper=%s, "
                "environment=%s, required_environment=%s",
                app_id,
                filesystem_override,
                has_config_fs,
                has_dll_fs,
                has_gamescope_wsi_fs,
                has_wrapper_fs,
                legacy_env_override,
                required_env_override,
            )

            return {
                "filesystem": filesystem_override,
                "wrapper": has_wrapper_fs,
                "legacy_env": legacy_env_override,
                "mako_env": mako_env_override,
                "required_env": required_env_override,
            }

        except Exception as e:
            self.log.error(f"Error checking override status for {app_id}: {e}")
            return _empty_app_override_status()

    def disable_incompatible_host_overrides(self) -> FlatpakCleanupResult:
        """Fail closed for MAKO-owned Flatpak overrides on unsupported hosts.

        Older plugin builds could prepare a direct Flatpak application before
        the native-host architecture boundary existed. Merely rejecting new
        activation would leave those persisted variables in place. Inspect
        every application, but remove settings only when its override contains
        a MAKO-specific path or variable; competitor-only LSFG settings are not
        sufficient evidence of MAKO ownership.
        """
        if self._host_architecture_supported():
            return {
                "success": True,
                "disabled_apps": [],
                "message": "Native host supports the packaged Flatpak payload",
            }
        if not self.check_flatpak_available():
            return {
                "success": False,
                "disabled_apps": [],
                "error": "Flatpak is unavailable; persisted MAKO overrides could not be inspected",
            }

        try:
            result = self._run_flatpak_command(
                ["list", "--app"],
                capture_output=True,
                text=True,
                check=True,
            )
        except subprocess.CalledProcessError as error:
            return {
                "success": False,
                "disabled_apps": [],
                "error": (
                    "Could not inspect Flatpak applications for incompatible "
                    f"MAKO overrides: {error.stderr or error}"
                ),
            }

        disabled_apps = []
        failures = []
        for line in result.stdout.splitlines():
            parts = line.split("\t")
            if len(parts) < 2:
                continue
            app_id = parts[1].strip()
            if not app_id:
                continue
            status = self._check_app_override_status(app_id)
            if not (
                status["filesystem"]
                or status["wrapper"]
                or status["mako_env"]
            ):
                continue
            removal = self.remove_app_override(app_id)
            if removal.get("success"):
                disabled_apps.append(app_id)
            else:
                failures.append(
                    f"{app_id}: {removal.get('error') or 'unknown error'}"
                )

        if failures:
            return {
                "success": False,
                "disabled_apps": disabled_apps,
                "error": "; ".join(failures),
            }
        return {
            "success": True,
            "disabled_apps": disabled_apps,
            "message": (
                "Disabled incompatible MAKO Flatpak overrides"
                if disabled_apps
                else "No incompatible MAKO Flatpak overrides were present"
            ),
        }

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
            if not self._host_architecture_supported():
                return self._error_response(
                    FlatpakOverrideResponse,
                    self._unsupported_host_error(),
                    app_id=app_id,
                    operation="set",
                )
            if not self.check_flatpak_available():
                return self._error_response(FlatpakOverrideResponse,
                                          "Flatpak is not available on this system",
                                          app_id=app_id, operation="set")

            runtime_version = self._get_app_runtime_version(app_id)
            if runtime_version is None:
                return self._error_response(
                    FlatpakOverrideResponse,
                    "Could not determine a supported Flatpak runtime for this application. "
                    "Install the matching MAKO runtime extension first.",
                    app_id=app_id,
                    operation="set",
                )
            if not self._is_extension_installed(runtime_version):
                return self._error_response(
                    FlatpakOverrideResponse,
                    f"Install the MAKO {runtime_version} runtime extension before enabling this application.",
                    app_id=app_id,
                    operation="set",
                )

            if not self.mako_script_path.is_file():
                return self._error_response(
                    FlatpakOverrideResponse,
                    "Install MAKO Renderer before preparing a Flatpak application.",
                    app_id=app_id,
                    operation="set",
                )

            config_path, dll_directory = self._get_mako_paths()
            wrapper_path = str(self.mako_script_path)

            filesystem_overrides = [
                f"--filesystem={config_path}:rw",
                f"--filesystem={dll_directory}:ro",
                f"--filesystem={wrapper_path}:ro",
                f"--filesystem={self.gamescope_wsi_compatibility_dir}:ro",
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

            if app_id in PER_GAME_WRAPPER_FLATPAK_APPS:
                # Heroic starts each game in a child compatibility environment.
                # Keep its layer activation in the selected game's wrapper so
                # preparing Heroic cannot enable frame generation in every game.
                environment_overrides = [
                    f"--unset-env={variable}"
                    for variable in _LAYER_ENVIRONMENT_VARIABLES
                ]
            else:
                # Flatpak applies persisted unset-environment entries after a
                # host-side Steam wrapper exports variables. Direct Flatpak
                # applications therefore need the config and manifest path in
                # their app override; otherwise the wrapper launches successfully
                # but the Vulkan layer never attaches. Use one deterministic
                # MAKO-only extension boundary on every supported runtime so a
                # system layer cannot reorder the swapchain dispatch chain.
                # MAKO must remain enabled for direct Flatpak applications,
                # including helper Vulkan processes launched after the host-side
                # wrapper environment is filtered.
                layer_environment = [
                    f"--env={VK_IMPLICIT_LAYER_PATH_ENV}={FLATPAK_IMPLICIT_LAYER_DIR}",
                    f"--unset-env={VK_ADD_IMPLICIT_LAYER_PATH_ENV}",
                ]
                environment_overrides = [
                    f"--env={MAKO_CONFIG_ENV}={config_path}/{CONFIG_FILENAME}",
                    f"--env={MAKO_LAYER_ENABLE_ENV}=1",
                    *(
                        f"--env={variable}=1"
                        for variable in COMPETING_LSFG_DISABLE_ENVS
                    ),
                    f"--env={GAMESCOPE_WSI_DISABLE_ENV}=1",
                    f"--unset-env={GAMESCOPE_WSI_ENABLE_ENV}",
                    f"--env={HDR_EXPOSURE_DISABLE_ENV}=1",
                    f"--unset-env={DXVK_HDR_ENV}",
                    *layer_environment,
                ]

            for override in environment_overrides:
                result = self._run_flatpak_command(
                    ["override", "--user", override, app_id],
                    capture_output=True, text=True
                )
                if result.returncode != 0:
                    error_msg = f"Failed to set environment override {override}: {result.stderr}"
                    return self._error_response(FlatpakOverrideResponse, error_msg,
                                              app_id=app_id, operation="set")

            self.log.info(f"Prepared MAKO Flatpak access for {app_id}")
            return self._success_response(FlatpakOverrideResponse,
                                        f"MAKO access prepared for {app_id}",
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
            wrapper_path = str(self.mako_script_path)

            filesystem_overrides = [
                f"--nofilesystem={dll_directory}",
                f"--nofilesystem={config_path}",
                f"--nofilesystem={wrapper_path}",
                f"--nofilesystem={self.gamescope_wsi_compatibility_dir}",
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

            for variable in _LAYER_ENVIRONMENT_VARIABLES:
                result = self._run_flatpak_command(
                    ["override", "--user", f"--unset-env={variable}", app_id],
                    capture_output=True, text=True
                )

                if result.returncode != 0:
                    removal_errors.append(f"unset-env {variable}: {result.stderr}")

            if removal_errors:
                error_msg = (
                    f"Could not remove every mako override for {app_id}: "
                    f"{'; '.join(removal_errors)}"
                )
                self.log.warning(error_msg)
                return self._error_response(
                    FlatpakOverrideResponse,
                    error_msg,
                    app_id=app_id,
                    operation="remove",
                )

            self.log.info(f"Completed override removal for {app_id}")
            return self._success_response(FlatpakOverrideResponse,
                                        f"MAKO Renderer overrides removed for {app_id}",
                                        app_id=app_id, operation="remove")

        except Exception as e:
            error_msg = f"Error removing overrides for {app_id}: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(FlatpakOverrideResponse, error_msg,
                                      app_id=app_id, operation="remove")
