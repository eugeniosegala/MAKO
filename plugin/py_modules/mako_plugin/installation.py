"""
Installation service for MAKO Renderer.
"""

import shutil
import traceback
import tarfile
import tempfile
import json
import os
import hashlib
from pathlib import Path
from typing import Dict, Any, Optional

from .base_service import BaseService
from .constants import (
    LIB_FILENAME, JSON_FILENAME, JSON32_FILENAME, CLI_FILENAME, CLI_DIR, BIN_DIR,
    DIAGNOSTICS_HELPER_FILENAME, MAKO_LAYER_NAME,
    MAKO_LAYER_ENABLE_ENV, MAKO_LAYER_DISABLE_ENV,
    MAKO_LAYER_BUILD_MARKER,
)
from .config_schema import ConfigurationManager
from .types import InstallationResponse, UninstallationResponse, InstallationCheckResponse


class InstallationService(BaseService):
    """Service for handling MAKO Renderer installation and uninstallation"""

    def __init__(self, logger=None):
        super().__init__(logger)

        self.lib_file = self.local_lib_dir / LIB_FILENAME
        self.lib32_file = self.local_lib32_dir / LIB_FILENAME
        self.json_file = self.local_share_dir / JSON_FILENAME
        self.json32_file = self.local_share_dir / JSON32_FILENAME
        self.cli_file = self.user_home / CLI_DIR / CLI_FILENAME
        self.engine_state_file = self.local_lib_dir.parent / "installed-engine.json"

    def install(self) -> InstallationResponse:
        """Install the bundled MAKO Renderer archive into this plugin's private storage.

        Returns:
            InstallationResponse with success status and message/error
        """
        try:
            plugin_dir = Path(__file__).parent.parent.parent
            archive_metadata = self._bundled_archive_metadata(plugin_dir)
            archive_path = plugin_dir / BIN_DIR / archive_metadata["name"]

            if not archive_path.exists():
                error_msg = f"Bundled MAKO Renderer archive not found at {archive_path}"
                self.log.error(error_msg)
                return self._error_response(InstallationResponse, error_msg, message="")

            self._validate_archive_checksum(
                archive_path,
                archive_metadata["sha256hash"],
            )

            self._ensure_directories()

            self._extract_and_install_files(archive_path)

            # Register a uniquely named, wrapper-scoped manifest in Vulkan's normal
            # per-user discovery directory. Steam's Pressure Vessel snapshots
            # that directory before the per-game wrapper starts, so relying on
            # a wrapper-only additive search path can miss the private payload.
            self._register_layer_manifests()

            self._create_config_file()

            self._create_mako_launch_script()

            self._install_diagnostics_helper(plugin_dir)

            self._write_engine_state(archive_metadata)

            self.log.info("MAKO Renderer installed successfully")
            return self._success_response(InstallationResponse, "MAKO Renderer installed successfully")

        except (OSError, tarfile.TarError, shutil.Error) as e:
            error_msg = f"Error installing MAKO Renderer: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(InstallationResponse, str(e), message="")
        except Exception as e:
            error_msg = f"Unexpected error installing MAKO Renderer: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(InstallationResponse, str(e), message="")

    def _bundled_archive_metadata(self, plugin_dir: Path) -> Dict[str, Any]:
        """Return the versioned host payload metadata from package.json."""
        manifest_path = plugin_dir / "package.json"
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            binaries = manifest.get("remote_binary")
            if not isinstance(binaries, list) or len(binaries) != 1:
                raise ValueError("package.json must define exactly one remote_binary entry")
            binary = binaries[0]
            archive_name = binary.get("name")
            version = binary.get("version")
            checksum = binary.get("sha256hash")
            architectures = binary.get("architectures", ["64", "32"])
            if not isinstance(archive_name, str) or Path(archive_name).name != archive_name:
                raise ValueError("remote_binary name must be a filename")
            if not isinstance(version, str) or not version:
                raise ValueError("remote_binary version must be a non-empty string")
            if (
                not isinstance(checksum, str)
                or len(checksum) != 64
                or any(character not in "0123456789abcdefABCDEF" for character in checksum)
            ):
                raise ValueError("remote_binary sha256hash must be a SHA-256 checksum")
            if (
                not isinstance(architectures, list)
                or not architectures
                or any(architecture not in ("64", "32") for architecture in architectures)
                or "64" not in architectures
            ):
                raise ValueError("remote_binary architectures must contain 64 and optional 32")
            return {
                "name": archive_name,
                "version": version,
                "sha256hash": checksum,
                "architectures": architectures,
            }
        except (OSError, json.JSONDecodeError, TypeError, ValueError, KeyError) as exc:
            raise OSError(f"Could not read bundled MAKO Renderer metadata from {manifest_path}: {exc}") from exc

    def _write_engine_state(self, archive_metadata: Dict[str, Any]) -> None:
        """Record exactly which pinned payload was installed by this plugin."""
        state = {
            "archive": archive_metadata["name"],
            "version": archive_metadata["version"],
            "sha256hash": archive_metadata["sha256hash"],
            "architectures": archive_metadata.get("architectures", ["64", "32"]),
        }
        self._write_file(self.engine_state_file, json.dumps(state, indent=2) + "\n", 0o644)

    @staticmethod
    def _validate_archive_checksum(archive_path: Path, expected_checksum: str) -> None:
        """Reject a bundled payload that differs from the package manifest."""
        digest = hashlib.sha256()
        with archive_path.open("rb") as archive:
            for chunk in iter(lambda: archive.read(1024 * 1024), b""):
                digest.update(chunk)
        actual_checksum = digest.hexdigest()
        if actual_checksum.lower() != expected_checksum.lower():
            raise OSError(
                "Bundled MAKO Renderer archive checksum mismatch: "
                f"expected {expected_checksum.lower()}, got {actual_checksum}"
            )

    def _read_engine_state(self) -> Optional[Dict[str, Any]]:
        """Return the plugin-managed payload record, if one exists."""
        try:
            state = json.loads(self.engine_state_file.read_text(encoding="utf-8"))
            if not isinstance(state, dict):
                return None
            if not all(isinstance(state.get(key), str) and state[key] for key in ("archive", "version", "sha256hash")):
                return None
            return state
        except (OSError, json.JSONDecodeError):
            return None

    def _extract_and_install_files(self, archive_path: Path) -> None:
        """Install the layer, manifest, and optional CLI from an upstream tar.xz.

        Args:
            archive_path: Path to the tar.xz archive to extract

        Raises:
            tarfile.TarError: If the archive is corrupted
            OSError: If file operations fail
        """
        required_destinations = {
            f"lib/{LIB_FILENAME}": self.lib_file,
            f"share/vulkan/implicit_layer.d/{JSON_FILENAME}": self.json_file,
        }
        optional_32bit_destinations = {
            f"lib32/{LIB_FILENAME}": self.lib32_file,
            f"share/vulkan/implicit_layer.d/{JSON32_FILENAME}": self.json32_file,
        }
        destinations = {**required_destinations, **optional_32bit_destinations}
        with tarfile.open(archive_path, "r:xz") as archive:
            with tempfile.TemporaryDirectory() as temp_dir:
                temp_path = Path(temp_dir)
                staged_files = {}
                for member in archive.getmembers():
                    if not member.isfile():
                        continue
                    member_path = member.name.removeprefix("./")
                    filename = Path(member_path).name
                    destination = destinations.get(member_path)
                    if filename == CLI_FILENAME:
                        destination = self.cli_file
                    if destination is None:
                        continue
                    source = archive.extractfile(member)
                    if source is None:
                        continue
                    # Use a generated staging name rather than the archive path;
                    # this also keeps the two same-named architecture libraries
                    # separate without trusting member path traversal.
                    temp_file = temp_path / f"{len(staged_files)}-{filename}"
                    with source, temp_file.open("wb") as output:
                        shutil.copyfileobj(source, output)
                    staged_files[destination] = (temp_file, filename)

                missing = [
                    str(path) for path in required_destinations.values()
                    if path not in staged_files
                ]
                if missing:
                    raise OSError(
                        "Archive did not contain required MAKO Renderer files: "
                        + ", ".join(missing)
                    )

                has_32bit_library = self.lib32_file in staged_files
                has_32bit_manifest = self.json32_file in staged_files
                if has_32bit_library != has_32bit_manifest:
                    raise OSError(
                        "Archive contained an incomplete 32-bit Vulkan layer pair"
                    )

                layer_binaries = [staged_files[self.lib_file][0]]
                if has_32bit_library:
                    layer_binaries.append(staged_files[self.lib32_file][0])
                self._validate_layer_binary_identity(*layer_binaries)

                for destination, (temp_file, filename) in staged_files.items():
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    if filename == JSON_FILENAME:
                        self._copy_and_fix_json_file(
                            temp_file, destination, "../../lib/libmako-render.so", "64"
                        )
                    elif filename == JSON32_FILENAME:
                        self._copy_and_fix_json_file(
                            temp_file, destination, "../../lib32/libmako-render.so", "32"
                        )
                    else:
                        shutil.copy2(temp_file, destination)
                        if filename == CLI_FILENAME:
                            destination.chmod(0o755)
                    self.log.info("Installed %s to %s", filename, destination)

                if not has_32bit_library:
                    # A 64-bit-only local test package must not leave a stale
                    # 32-bit layer from an older install discoverable.
                    self._remove_if_exists(self.lib32_file)
                    self._remove_if_exists(self.json32_file)

    @staticmethod
    def _validate_layer_binary_identity(*layer_binaries: Path) -> None:
        """Reject payloads that cannot prove they are the isolated build."""
        for layer_binary in layer_binaries:
            if MAKO_LAYER_BUILD_MARKER not in layer_binary.read_bytes():
                raise OSError(
                    f"MAKO layer build marker is missing from {layer_binary}"
                )

    def _copy_and_fix_json_file(
            self, src_file: Path, dst_file: Path,
            library_path: str, library_arch: str) -> None:
        """Copy a JSON manifest and point it at the private architecture path.

        Args:
            src_file: Source JSON file path
            dst_file: Destination JSON file path
        """
        try:
            with src_file.open("r", encoding="utf-8") as source:
                json_data = json.load(source)
            layer = json_data.get("layer")
            if not isinstance(layer, dict) or not isinstance(layer.get("library_path"), str):
                raise ValueError("missing layer.library_path")

            layer["name"] = MAKO_LAYER_NAME
            layer["description"] = "MAKO Renderer graphics layer"
            layer["library_path"] = library_path
            layer["library_arch"] = library_arch
            layer["enable_environment"] = {
                MAKO_LAYER_ENABLE_ENV: "1",
            }
            layer["disable_environment"] = {
                MAKO_LAYER_DISABLE_ENV: "1",
            }
            dst_file.parent.mkdir(parents=True, exist_ok=True)
            file_descriptor, temporary_name = tempfile.mkstemp(
                prefix=f".{dst_file.name}.",
                dir=dst_file.parent,
            )
            temporary_path = Path(temporary_name)
            try:
                with os.fdopen(file_descriptor, "w", encoding="utf-8") as output:
                    json.dump(json_data, output, indent=2)
                    output.write("\n")
                    output.flush()
                    os.fsync(output.fileno())
                temporary_path.chmod(0o644)
                temporary_path.replace(dst_file)
            finally:
                temporary_path.unlink(missing_ok=True)
        except (json.JSONDecodeError, OSError, TypeError, ValueError) as error:
            raise OSError(f"Invalid Vulkan layer manifest {src_file}: {error}") from error

    def _register_layer_manifests(self) -> None:
        """Register gated manifests without activating the private layer globally."""
        self.user_vulkan_layer_dir.mkdir(parents=True, exist_ok=True)
        self._copy_and_fix_json_file(
            self.json_file,
            self.registered_json_file,
            str(self.lib_file),
            "64",
        )
        if self.lib32_file.exists() and self.json32_file.exists():
            self._copy_and_fix_json_file(
                self.json32_file,
                self.registered_json32_file,
                str(self.lib32_file),
                "32",
            )
        else:
            self._remove_if_exists(self.registered_json32_file)

    def _create_config_file(self) -> None:
        """Create or update this plugin's private TOML config with detected DLL path.

        If a config file already exists, preserve existing profiles and only update global settings like DLL path.
        """
        # Import here to avoid circular imports
        from .dll_detection import DllDetectionService

        # Try to detect DLL path
        dll_service = DllDetectionService(self.log)

        # Check if config file already exists
        if self.config_file_path.exists():
            try:
                # Read existing config to preserve user profiles
                content = self.config_file_path.read_text(encoding='utf-8')
                existing_profile_data = ConfigurationManager.parse_toml_content_multi_profile(content)
                self.log.info(f"Found existing config file, preserving user profiles")

                # Create merged profile data that preserves user settings but adds any new fields
                merged_profile_data = self._merge_config_with_defaults(existing_profile_data, dll_service)

                # Generate TOML content with merged profiles
                toml_content = ConfigurationManager.generate_toml_content_multi_profile(merged_profile_data)

            except Exception as e:
                self.log.warning(f"Failed to parse existing config file: {str(e)}, creating new one")
                # Fall back to creating a new config file
                config = ConfigurationManager.get_defaults_with_dll_detection(dll_service)
                toml_content = ConfigurationManager.generate_toml_content(config)
        else:
            # No existing config file, create a new one with defaults
            config = ConfigurationManager.get_defaults_with_dll_detection(dll_service)
            toml_content = ConfigurationManager.generate_toml_content(config)
            self.log.info(f"Creating new config file")

        # Write config file
        self._write_file(self.config_file_path, toml_content, 0o644)
        self.log.info(f"Created config file at {self.config_file_path}")

        # Log detected DLL path if found - USE GENERATED CONSTANTS
        from .config_schema_generated import DLL
        try:
            # Try to parse the written content to get the DLL path
            final_content = self.config_file_path.read_text(encoding='utf-8')
            final_config = ConfigurationManager.parse_toml_content(final_content)
            if final_config.get(DLL):
                self.log.info(f"Configured DLL path: {final_config[DLL]}")
        except (OSError, IOError, ValueError, KeyError) as e:
            # Don't fail installation if we can't log the DLL path
            self.log.debug(f"Could not log DLL path: {e}")

    def _create_mako_launch_script(self) -> None:
        """Create the isolated per-game launch script using the active profile."""
        # The configuration file is created or merged immediately before this
        # method runs. Rebuild from it rather than from defaults so an engine
        # reinstall does not silently reset the generated wrapper's settings.
        from .configuration import ConfigurationService
        config_service = ConfigurationService(logger=self.log)
        config_service.user_home = self.user_home
        config_service.local_share_dir = self.local_share_dir
        config_service.config_dir = self.config_dir
        config_service.config_file_path = self.config_file_path
        config_service.mako_script_path = self.mako_launch_script_path

        profile_data = config_service._get_profile_data()
        script_content = config_service._generate_script_content_for_profile(profile_data)

        # Write the script file
        self._write_file(self.mako_launch_script_path, script_content, 0o755)
        self.log.info(f"Created MAKO launch script at {self.mako_launch_script_path}")

    def _install_diagnostics_helper(self, plugin_dir: Path) -> None:
        """Install the packaged read-only diagnostic filter beside the wrapper."""
        source = self._diagnostics_helper_source(plugin_dir)
        self.diagnostics_script_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, self.diagnostics_script_path)
        self.diagnostics_script_path.chmod(0o755)
        self.log.info("Installed diagnostics helper to %s", self.diagnostics_script_path)

    @staticmethod
    def _diagnostics_helper_source(plugin_dir: Path) -> Path:
        """Resolve the release-ZIP helper, with a source-tree development fallback."""
        candidates = (
            plugin_dir / BIN_DIR / DIAGNOSTICS_HELPER_FILENAME,
            plugin_dir / "scripts" / DIAGNOSTICS_HELPER_FILENAME,
        )
        for candidate in candidates:
            if candidate.is_file():
                return candidate
        raise OSError(f"Bundled diagnostics helper not found at {candidates[0]}")

    def migrate_diagnostics_helper_if_needed(self) -> bool:
        """Install or refresh the helper without requiring an engine reinstall."""
        plugin_dir = Path(__file__).parent.parent.parent
        source = self._diagnostics_helper_source(plugin_dir)
        try:
            current = self.diagnostics_script_path.read_bytes()
            bundled = source.read_bytes()
            executable = bool(self.diagnostics_script_path.stat().st_mode & 0o111)
            if current == bundled and executable:
                return False
        except OSError:
            pass

        self._install_diagnostics_helper(plugin_dir)
        return True

    def get_launch_script_path(self) -> str:
        """Get the path to the MAKO launch script.

        Returns:
            String path to the launch script file
        """
        return str(self.mako_launch_script_path)

    def check_installation(self) -> InstallationCheckResponse:
        """Check if MAKO Renderer is already installed

        Returns:
            InstallationCheckResponse with installation status and file paths
        """
        try:
            lib_exists = self.lib_file.exists()
            lib32_exists = self.lib32_file.exists()
            json_exists = self.json_file.exists()
            json32_exists = self.json32_file.exists()
            registered_json_exists = self.registered_json_file.exists()
            registered_json32_exists = self.registered_json32_file.exists()
            script_exists = self.mako_launch_script_path.exists()
            installed = (
                lib_exists and json_exists and registered_json_exists
                and script_exists
            )
            expected = self._bundled_archive_metadata(Path(__file__).parent.parent.parent)
            expects_32bit = "32" in expected.get("architectures", ["64", "32"])
            installed = installed and (
                not expects_32bit
                or (lib32_exists and json32_exists and registered_json32_exists)
            )
            state = self._read_engine_state()
            version_known = state is not None
            installed_version = state["version"] if state else None
            update_required = installed and (
                state is None
                or state["version"] != expected["version"]
                or state["sha256hash"] != expected["sha256hash"]
            )

            self.log.info(
                "Installation check: lib64=%s, lib32=%s, private-json64=%s, "
                "private-json32=%s, registered-json64=%s, registered-json32=%s, "
                "script=%s",
                lib_exists, lib32_exists, json_exists, json32_exists,
                registered_json_exists, registered_json32_exists,
                script_exists,
            )

            return {
                "installed": installed,
                "lib_exists": lib_exists,
                "json_exists": json_exists,
                "script_exists": script_exists,
                "lib_path": str(self.lib_file),
                "json_path": str(self.registered_json_file),
                "script_path": str(self.mako_launch_script_path),
                "installed_engine_version": installed_version,
                "expected_engine_version": expected["version"],
                "engine_version_known": version_known,
                "engine_update_required": update_required,
                "error": None
            }

        except Exception as e:
            error_msg = f"Error checking MAKO Renderer installation: {str(e)}"
            self.log.error(error_msg)
            return {
                "installed": False,
                "lib_exists": False,
                "json_exists": False,
                "script_exists": False,
                "lib_path": str(self.lib_file),
                "json_path": str(self.json_file),
                "script_path": str(self.mako_launch_script_path),
                "installed_engine_version": None,
                "expected_engine_version": None,
                "engine_version_known": False,
                "engine_update_required": False,
                "error": str(e)
            }

    def uninstall(self) -> UninstallationResponse:
        """Uninstall MAKO Renderer by removing the installed files

        Note: The config file (conf.toml) is preserved to maintain user's custom profiles

        Returns:
            UninstallationResponse with success status and removed files list
        """
        try:
            removed_files = []
            # Remove core MAKO Renderer files, but preserve config file to maintain user's custom profiles
            files_to_remove = [
                self.lib_file, self.lib32_file, self.json_file, self.json32_file,
                self.registered_json_file, self.registered_json32_file,
                self.cli_file, self.engine_state_file, self.mako_launch_script_path,
                self.diagnostics_script_path,
            ]

            for file_path in files_to_remove:
                if self._remove_if_exists(file_path):
                    removed_files.append(str(file_path))

            # Remove the generated launch script if it exists.
            if self._remove_if_exists(self.mako_script_path):
                removed_files.append(str(self.mako_script_path))

            # Don't remove config directory since we're preserving the config file

            if not removed_files:
                return self._success_response(UninstallationResponse,
                                            "No MAKO Renderer files found to remove",
                                            removed_files=None)

            self.log.info("MAKO Renderer uninstalled successfully")
            return self._success_response(UninstallationResponse,
                                        f"MAKO Renderer uninstalled successfully. Removed {len(removed_files)} files.",
                                        removed_files=removed_files)

        except OSError as e:
            error_msg = f"Error uninstalling MAKO Renderer: {str(e)}"
            self.log.error(error_msg)
            return self._error_response(UninstallationResponse, str(e),
                                      message="", removed_files=None)

    def cleanup_on_uninstall(self) -> None:
        """Clean up MAKO Renderer files when the plugin is uninstalled

        Note: The config file (conf.toml) is preserved to maintain user's custom profiles
        """
        try:
            self.log.info("Checking for MAKO Renderer files to clean up:")
            self.log.info(f"  64-bit library file: {self.lib_file}")
            self.log.info(f"  32-bit library file: {self.lib32_file}")
            self.log.info(f"  64-bit JSON file: {self.json_file}")
            self.log.info(f"  32-bit JSON file: {self.json32_file}")
            self.log.info(f"  Config file: {self.config_file_path} (preserved)")
            self.log.info(f"  CLI file: {self.cli_file}")
            self.log.info(f"  Launch script: {self.mako_launch_script_path}")
            self.log.info(f"  Launch script: {self.mako_script_path}")
            self.log.info(f"  Diagnostics helper: {self.diagnostics_script_path}")

            removed_files = []
            # Remove core MAKO Renderer files, but preserve config file to maintain user's custom profiles
            files_to_remove = [
                self.lib_file, self.lib32_file, self.json_file, self.json32_file,
                self.registered_json_file, self.registered_json32_file,
                self.cli_file, self.engine_state_file, self.mako_launch_script_path,
                self.mako_script_path, self.diagnostics_script_path,
            ]

            for file_path in files_to_remove:
                try:
                    if self._remove_if_exists(file_path):
                        removed_files.append(str(file_path))
                except OSError as e:
                    self.log.error(f"Failed to remove {file_path}: {e}")

            # Don't remove config directory since we're preserving the config file

            if removed_files:
                self.log.info(f"Cleaned up {len(removed_files)} MAKO Renderer files during plugin uninstall: {removed_files}")
            else:
                self.log.info("No MAKO Renderer files found to clean up during plugin uninstall")

        except Exception as e:
            self.log.error(f"Error cleaning up MAKO Renderer files during uninstall: {str(e)}")
            self.log.error(f"Traceback: {traceback.format_exc()}")

    def _merge_config_with_defaults(self, existing_profile_data, dll_service):
        """Merge existing user config with current schema defaults

        This ensures that:
        1. User's custom profiles and values are preserved
        2. Any new fields added to the schema get their default values
        3. Global settings like DLL path are updated as needed

        Args:
            existing_profile_data: The user's existing ProfileData
            dll_service: DLL detection service for updating DLL path

        Returns:
            ProfileData with merged configuration
        """
        from .config_schema import ProfileData

        # Get current schema defaults
        default_config = ConfigurationManager.get_defaults_with_dll_detection(dll_service)
        default_global_config = {
            "dll": default_config.get("dll", ""),
            "allow_fp16": default_config.get("allow_fp16", True)
        }

        # Start with existing data
        merged_data: ProfileData = {
            "current_profile": existing_profile_data.get("current_profile", "mako"),
            "global_config": existing_profile_data.get("global_config", {}).copy(),
            "profiles": {}
        }

        # Merge global config: preserve user values, add missing fields, update DLL
        for key, default_value in default_global_config.items():
            if key not in merged_data["global_config"]:
                merged_data["global_config"][key] = default_value
                self.log.info(f"Added missing global field '{key}' with default value: {default_value}")

        # Update DLL path if detected
        dll_result = dll_service.check_lossless_scaling_dll()
        if dll_result.get("detected") and dll_result.get("path"):
            old_dll = merged_data["global_config"].get("dll")
            merged_data["global_config"]["dll"] = dll_result["path"]
            if old_dll != dll_result["path"]:
                self.log.info(f"Updated DLL path from '{old_dll}' to: {dll_result['path']}")
        elif merged_data["global_config"].get("dll") == "/games/Lossless Scaling/Lossless.dll":
            # Releases before 0.13.0-experimental.2 wrote this placeholder when
            # detection failed. Removing a nonexistent placeholder lets MAKO Renderer
            # use its own automatic discovery instead.
            legacy_path = Path("/games/Lossless Scaling/Lossless.dll")
            if not legacy_path.exists():
                merged_data["global_config"]["dll"] = ""
                self.log.info("Removed obsolete Lossless.dll placeholder to enable upstream automatic discovery")

        # Merge each profile: preserve user values, add missing fields
        existing_profiles = existing_profile_data.get("profiles", {})

        for profile_name, existing_profile_config in existing_profiles.items():
            merged_profile_config = existing_profile_config.copy()

            # Add any missing fields from current schema with default values
            added_fields = []
            for key, default_value in default_config.items():
                if key not in merged_profile_config and key not in ["dll", "allow_fp16"]:  # Skip global fields
                    merged_profile_config[key] = default_value
                    added_fields.append(key)

            if added_fields:
                self.log.info(f"Profile '{profile_name}': Added missing fields {added_fields}")

            merged_data["profiles"][profile_name] = merged_profile_config

        # If no profiles exist, create the default one
        if not merged_data["profiles"]:
            merged_data["profiles"]["mako"] = {
                k: v for k, v in default_config.items()
                if k not in ["dll", "allow_fp16"]  # Exclude global fields
            }
            merged_data["current_profile"] = "mako"
            self.log.info("No existing profiles found, created default profile")

        return merged_data
