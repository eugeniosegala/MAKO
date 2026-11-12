"""
Installation service for MAKO Renderer.
"""

import shutil
import traceback
import tarfile
import tempfile
import json
import hashlib
import fcntl
import os
import stat
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Dict, Iterator, Optional, TypedDict, cast

from .base_service import BaseService
from .constants import (
    LIB_FILENAME, JSON_FILENAME, JSON32_FILENAME,
    SPATIAL_SCALING_LIB_FILENAME,
    SPATIAL_SCALING_JSON_FILENAME, SPATIAL_SCALING_JSON32_FILENAME,
    SHADER_TOOL_FILENAME, CLI_FILENAME, CLI_DIR, VRR_LEASE_FILENAME, BIN_DIR,
    DIAGNOSTICS_HELPER_FILENAME, MAKO_LAYER_NAME,
    MAKO_LAYER_ENABLE_ENV, MAKO_LAYER_DISABLE_ENV,
    SPATIAL_SCALING_LAYER_NAME,
    SPATIAL_SCALING_LAYER_ENABLE_ENV,
    SPATIAL_SCALING_LAYER_DISABLE_ENV,
    MAKO_LAYER_BUILD_MARKER, SPATIAL_SCALING_LAYER_BUILD_MARKER,
    MAKO_PROFILE_FALLBACK_MARKER,
    GAMESCOPE_WSI_DISABLE_ENV, GAMESCOPE_WSI_ENABLE_ENV,
    GAMESCOPE_WSI_LAYER_NAME_64, GAMESCOPE_WSI_MANIFEST_FILENAME_64,
    GAMESCOPE_WSI_LIBRARY_FILENAME_64,
    MANGOHUD_LAYER_NAME_64, MANGOHUD_MANIFEST_FILENAME_64,
    MANGOHUD_LAYER_NAME_32, MANGOHUD_MANIFEST_FILENAME_32,
    VKBASALT_LAYER_NAME_64, VKBASALT_MANIFEST_FILENAME_64,
    VKBASALT_LAYER_NAME_32, VKBASALT_MANIFEST_FILENAME_32,
    VKBASALT_LIB_FILENAME,
    MAKO_FG_DIRECTORY,
    MAKO_FG_FILES,
    MAKO_FG_HOST_FILENAME,
    VKBASALT_SHADER_DIR, VKBASALT_SHADER_ASSET_FILENAMES,
    VKBASALT_LAYER_ENABLE_ENV, VKBASALT_LAYER_DISABLE_ENV,
    HOST_SYSTEM_IMPLICIT_LAYER_DIR,
    ARMADA_DEVICE_ENV,
    ACTIVE_RENDERER_STATE_FILENAME,
    ACTIVE_RENDERER_STATE_SCHEMA_VERSION,
    ACTIVE_RENDERER_OWNER_DECKY,
    ACTIVE_RENDERER_OWNER_STANDALONE,
    STANDALONE_INSTALLER_STATE_RELATIVE_PATH,
    PLUGIN_ROOT,
)
from .config_schema import ConfigurationManager, DEFAULT_PROFILE_NAME
from .remote_play_launch import SDR_HELPER_RELATIVE_PATH
from .host_environment import detect_host_environment
from .layer_manifests import manifest_owner
from .build_flavor import LOCAL_DEVELOPMENT_BUILD
from .development_renderer import read_development_receipt
from .managed_files import (
    copy_managed_file_atomically,
    managed_install_transaction,
    write_managed_text_atomically,
)
from .types import InstallationResponse, UninstallationResponse, InstallationCheckResponse

SYSTEM_RENDERER_VERSION_FILE = Path("/usr/share/doc/mako-renderer-bin/MAKO-Renderer-version.txt")


class RendererArchiveMetadataRequired(TypedDict):
    """Validated Renderer payload metadata consumed by installation."""

    name: str
    version: str
    sha256hash: str
    architectures: list[str]
    host_architectures: list[str]


class RendererArchiveMetadata(RendererArchiveMetadataRequired, total=False):
    """Package identity, including the portable native payload fingerprint."""

    payload_sha256hash: str


class InstalledEngineStateRequired(TypedDict):
    """Fields required to identify an installed Renderer payload."""

    archive: str
    version: str
    sha256hash: str


class InstalledEngineState(InstalledEngineStateRequired, total=False):
    """Installed payload record with optional legacy-compatible descriptors."""

    architectures: list[str]
    host_architectures: list[str]


class ActiveRendererStateRequired(TypedDict):
    """Identity of the native Renderer selected by the latest installer."""

    schema_version: int
    owner: str
    version: str


class ActiveRendererState(ActiveRendererStateRequired, total=False):
    """Optional fingerprints added without invalidating older native records."""

    sha256hash: str
    payload_sha256hash: str


@dataclass(frozen=True)
class RendererFileInventory:
    """One inventory for archive selection, rollback, and native cleanup."""

    required_archive: dict[str, Path]
    optional_32bit_archive: dict[str, Path]
    optional_archive: dict[str, Path]
    generated: tuple[Path, ...]

    def managed_paths(self) -> list[Path]:
        return list(dict.fromkeys((
            *self.required_archive.values(),
            *self.optional_32bit_archive.values(),
            *self.optional_archive.values(),
            *self.generated,
        )))


class InstallationService(BaseService):
    """Service for handling MAKO Renderer installation and uninstallation"""

    @contextmanager
    def native_installation_lock(self) -> Iterator[None]:
        """Serialize the two user-local native installers without a stale lock."""
        self.standalone_install_prefix.mkdir(parents=True, exist_ok=True)
        path = self.standalone_install_prefix / ".mako-render-install.lock"
        descriptor = os.open(path, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600)
        with os.fdopen(descriptor, "r+") as lock:
            identity = os.fstat(lock.fileno())
            if not stat.S_ISREG(identity.st_mode) or identity.st_nlink != 1:
                raise OSError("MAKO Renderer installation lock must be a private regular file")
            # Decky can run as root; the native user must be able to lock it too.
            owner = self.user_home.stat()
            os.fchown(lock.fileno(), owner.st_uid, owner.st_gid)
            os.fchmod(lock.fileno(), 0o600)
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as error:
                raise OSError("Another MAKO Renderer installation or removal is in progress") from error
            yield

    def _system_renderer_notice(self) -> Optional[str]:
        """Explain the independent system owner before an explicit local install."""
        if (SYSTEM_RENDERER_VERSION_FILE.is_file()
                and not self.lib_file.is_file()
                and not self.standalone_lib_file.is_file()):
            return (
                "MAKO Renderer is installed through pacman. Selecting Install creates a "
                "separate user-local Renderer for MAKO Decky; pacman continues to manage "
                "the system package. Both use your saved profiles, so keep their Renderer "
                "versions aligned."
            )
        return None

    def __init__(self, logger=None):
        super().__init__(logger)

        self.lib_file = self.local_lib_dir / LIB_FILENAME
        self.lib32_file = self.local_lib32_dir / LIB_FILENAME
        self.spatial_scaling_lib_file = (
            self.local_lib_dir / SPATIAL_SCALING_LIB_FILENAME
        )
        self.spatial_scaling_lib32_file = (
            self.local_lib32_dir / SPATIAL_SCALING_LIB_FILENAME
        )
        self.json_file = self.local_share_dir / JSON_FILENAME
        self.json32_file = self.local_share_dir / JSON32_FILENAME
        # Only the standalone archive registers its scaling layer publicly.
        self.registered_spatial_scaling_json_file = (
            self.user_vulkan_layer_dir / SPATIAL_SCALING_JSON_FILENAME
        )
        self.registered_spatial_scaling_json32_file = (
            self.user_vulkan_layer_dir / SPATIAL_SCALING_JSON32_FILENAME
        )
        self.gamescope_wsi_compatibility_manifest = (
            self.gamescope_wsi_compatibility_dir /
            GAMESCOPE_WSI_MANIFEST_FILENAME_64
        )
        self.gamescope_wsi_compatibility_library = (
            self.gamescope_wsi_compatibility_dir /
            GAMESCOPE_WSI_LIBRARY_FILENAME_64
        )
        self.mangohud_manifest = (
            self.mangohud_layer_dir / MANGOHUD_MANIFEST_FILENAME_64
        )
        self.mangohud_manifest32 = (
            self.mangohud_layer_dir / MANGOHUD_MANIFEST_FILENAME_32
        )
        self.vkbasalt_manifest = (
            self.vkbasalt_layer_dir / VKBASALT_MANIFEST_FILENAME_64
        )
        self.vkbasalt_manifest32 = (
            self.vkbasalt_layer_dir / VKBASALT_MANIFEST_FILENAME_32
        )
        self.vkbasalt_lib_file = (
            self.vkbasalt_lib_dir / VKBASALT_LIB_FILENAME
        )
        self.vkbasalt_lib32_file = (
            self.vkbasalt_lib32_dir / VKBASALT_LIB_FILENAME
        )
        self.renderer_vkbasalt_shader_dir = (
            self.user_home / VKBASALT_SHADER_DIR
        )
        self.cli_file = self.user_home / CLI_DIR / CLI_FILENAME
        self.vrr_lease_file = self.user_home / CLI_DIR / VRR_LEASE_FILENAME
        self.remote_play_sdr_file = self.user_home / '.local' / SDR_HELPER_RELATIVE_PATH
        self.engine_state_file = self.local_lib_dir.parent / "installed-engine.json"
        self.active_renderer_state_file = (
            self.local_lib_dir.parent / ACTIVE_RENDERER_STATE_FILENAME
        )
        # The installation check runs on every panel open; reread the bundled
        # manifest and rehash installed files only when they change.
        self._payload_manifest_cache: Optional[
            tuple[tuple[str, int, int, str], dict[str, str]]
        ] = None
        self._file_checksum_cache: dict[Path, tuple[tuple[int, int, int], str]] = {}
        self.standalone_lib_file = (
            self.standalone_install_prefix / "lib" / LIB_FILENAME
        )
        self.standalone_lib32_file = (
            self.standalone_install_prefix / "lib32" / LIB_FILENAME
        )
        self.standalone_spatial_scaling_lib_file = (
            self.standalone_install_prefix
            / "lib"
            / SPATIAL_SCALING_LIB_FILENAME
        )
        self.standalone_spatial_scaling_lib32_file = (
            self.standalone_install_prefix
            / "lib32"
            / SPATIAL_SCALING_LIB_FILENAME
        )
        self.standalone_vkbasalt_lib_file = (
            self.standalone_install_prefix / "lib" / "vkbasalt" /
            VKBASALT_LIB_FILENAME
        )
        self.standalone_vkbasalt_lib32_file = (
            self.standalone_install_prefix / "lib32" / "vkbasalt" /
            VKBASALT_LIB_FILENAME
        )
        self.standalone_vrr_lease_file = (
            self.standalone_install_prefix / "bin" / VRR_LEASE_FILENAME
        )
        self.standalone_installer_state_file = (
            self.user_home / STANDALONE_INSTALLER_STATE_RELATIVE_PATH
        )

    def install(self) -> InstallationResponse:
        try:
            with self.native_installation_lock():
                return self._install_locked()
        except OSError as error:
            return self._error_response(InstallationResponse, str(error), message="")

    def _install_locked(self) -> InstallationResponse:
        """Install the bundled MAKO Renderer archive into this plugin's private storage.

        Returns:
            InstallationResponse with success status and message/error
        """
        try:
            archive_metadata = self._bundled_archive_metadata(PLUGIN_ROOT)
            self._validate_host_architecture(archive_metadata)
            archive_path = PLUGIN_ROOT / BIN_DIR / archive_metadata["name"]

            if not archive_path.exists():
                error_msg = f"Bundled MAKO Renderer archive not found at {archive_path}"
                self.log.error(error_msg)
                return self._error_response(InstallationResponse, error_msg, message="")

            self._validate_archive_checksum(
                archive_path,
                archive_metadata["sha256hash"],
            )
            if "payload_sha256hash" in archive_metadata:
                checksum = hashlib.sha256(
                    self._read_payload_manifest(archive_path)
                ).hexdigest()
                if checksum != archive_metadata["payload_sha256hash"]:
                    raise OSError("Bundled MAKO Renderer payload manifest checksum mismatch")

            self._ensure_directories()

            with managed_install_transaction(
                self._decky_renderer_files() + [self.config_file_path], self.log,
            ):
                self._extract_and_install_files(archive_path)
                self._sync_packaged_management_tools()

                # Register a uniquely named, wrapper-scoped manifest in Vulkan's normal
                # per-user discovery directory. Steam's Pressure Vessel snapshots
                # that directory before the per-game wrapper starts, so relying on
                # a wrapper-only additive search path can miss the private payload.
                self._register_layer_manifests()

                self.migrate_gamescope_wsi_compatibility_manifest_if_needed()
                self.refresh_guarded_postprocess_manifests_if_needed()

                self._create_config_file()

                self._create_mako_launch_script()

                self._install_diagnostics_helper(PLUGIN_ROOT)

                self._write_engine_state(archive_metadata)

                self._write_active_renderer_state(archive_metadata)

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

    @staticmethod
    def _read_payload_manifest(archive_path: Path) -> bytes:
        """Return the archive's per-file SHA-256 install manifest."""
        with tarfile.open(archive_path, "r:xz") as archive:
            member = next((entry for entry in archive.getmembers()
                           if entry.name.removeprefix("./") == "MAKO-Renderer-install-manifest.txt"
                           and entry.isfile()), None)
            source = archive.extractfile(member) if member is not None else None
            if source is None:
                raise OSError("Bundled MAKO Renderer payload manifest is missing")
            with source:
                return source.read()

    def _bundled_payload_checksums(
            self, archive_path: Path, payload_checksum: str) -> Optional[dict[str, str]]:
        """Return the bundled per-file checksums the payload fingerprint vouches for."""
        try:
            archive_stat = archive_path.stat()
        except OSError:
            return None
        key = (str(archive_path), archive_stat.st_size,
               archive_stat.st_mtime_ns, payload_checksum)
        if self._payload_manifest_cache and self._payload_manifest_cache[0] == key:
            return self._payload_manifest_cache[1]
        try:
            manifest = self._read_payload_manifest(archive_path)
        except (OSError, tarfile.TarError):
            return None
        if hashlib.sha256(manifest).hexdigest() != payload_checksum:
            return None
        checksums: dict[str, str] = {}
        for line in manifest.decode("utf-8", "replace").splitlines():
            digest, separator, member = line.partition("  ")
            if separator and len(digest) == 64:
                checksums[member.removeprefix("./")] = digest.lower()
        self._payload_manifest_cache = (key, checksums)
        return checksums

    def _cached_file_sha256(self, path: Path) -> Optional[str]:
        """Return a file's SHA-256, reusing it while size, time and inode hold."""
        try:
            file_stat = path.stat()
        except OSError:
            return None
        key = (file_stat.st_size, file_stat.st_mtime_ns, file_stat.st_ino)
        cached = self._file_checksum_cache.get(path)
        if cached and cached[0] == key:
            return cached[1]
        try:
            checksum = self._file_sha256(path)
        except OSError:
            return None
        self._file_checksum_cache[path] = (key, checksum)
        return checksum

    def _installed_payload_differences(
            self, archive_metadata: RendererArchiveMetadata) -> list[Path]:
        """Return installed Renderer binaries that differ from the bundled payload.

        Decky's own record cannot see files replaced outside Decky or an
        interrupted install. Layer libraries and MAKO FG files are copied
        verbatim, so the bundled manifest checksums them directly; generated
        and rewritten files are not compared. Without a payload fingerprint or
        a readable archive there is nothing trustworthy to compare against.
        Local development builds also accept exact copies verified by direct
        deployment against this package and native identity; other files keep
        the bundled checksums and ordinary installs ignore the receipt.
        """
        payload_checksum = archive_metadata.get("payload_sha256hash")
        if payload_checksum is None:
            return []
        checksums = self._bundled_payload_checksums(
            PLUGIN_ROOT / BIN_DIR / archive_metadata["name"], payload_checksum
        )
        if not checksums:
            return []
        inventory = self._renderer_file_inventory()
        destinations = {
            **inventory.required_archive,
            **inventory.optional_32bit_archive,
            **inventory.optional_archive,
        }
        receipt = (read_development_receipt(PLUGIN_ROOT, self.active_renderer_state_file)
                   if LOCAL_DEVELOPMENT_BUILD else None)
        development_files = receipt["files"] if receipt else {}
        return [
            destination
            for member, destination in destinations.items()
            if member.startswith(("lib/", "lib32/"))
            and member in checksums
            and self._cached_file_sha256(destination) != checksums[member]
            and (str(destination) not in development_files
                 or self._cached_file_sha256(destination) != development_files[str(destination)])
        ]

    def _bundled_archive_metadata(
            self, plugin_dir: Path) -> RendererArchiveMetadata:
        """Return the versioned host payload metadata from package.json."""
        manifest_path = plugin_dir / "package.json"
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            if "bundled_renderer" in manifest:
                if "remote_binary" in manifest:
                    raise ValueError(
                        "package.json must not define both bundled_renderer and remote_binary"
                    )
                binary = manifest["bundled_renderer"]
                metadata_name = "bundled_renderer"
                if not isinstance(binary, dict):
                    raise ValueError("bundled_renderer must be an object")
            else:
                binaries = manifest.get("remote_binary")
                if not isinstance(binaries, list) or len(binaries) != 1:
                    raise ValueError(
                        "package.json must define bundled_renderer or exactly one "
                        "remote_binary entry"
                    )
                binary = binaries[0]
                metadata_name = "remote_binary"
            archive_name = binary.get("name")
            version = binary.get("version")
            checksum = binary.get("sha256hash")
            payload_checksum = binary.get("payload_sha256hash")
            architectures = binary.get("architectures", ["64", "32"])
            host_architectures = binary.get("host_architectures", ["x86_64"])
            if not isinstance(archive_name, str) or Path(archive_name).name != archive_name:
                raise ValueError(f"{metadata_name} name must be a filename")
            if not isinstance(version, str) or not version:
                raise ValueError(f"{metadata_name} version must be a non-empty string")
            if (
                not isinstance(checksum, str)
                or len(checksum) != 64
                or any(character not in "0123456789abcdefABCDEF" for character in checksum)
            ):
                raise ValueError(
                    f"{metadata_name} sha256hash must be a SHA-256 checksum"
                )
            if (
                not isinstance(architectures, list)
                or not architectures
                or any(architecture not in ("64", "32") for architecture in architectures)
                or "64" not in architectures
            ):
                raise ValueError(
                    f"{metadata_name} architectures must contain 64 and optional 32"
                )
            if (
                not isinstance(host_architectures, list)
                or not host_architectures
                or any(
                    architecture not in ("x86_64", "aarch64")
                    for architecture in host_architectures
                )
            ):
                raise ValueError(
                    f"{metadata_name} host_architectures must contain supported native "
                    "host names"
                )
            metadata: RendererArchiveMetadata = {
                "name": archive_name,
                "version": version,
                "sha256hash": checksum.lower(),
                "architectures": architectures,
                "host_architectures": host_architectures,
            }
            if "payload_sha256hash" in binary:
                if (not isinstance(payload_checksum, str)
                        or len(payload_checksum) != 64
                        or any(c not in "0123456789abcdefABCDEF" for c in payload_checksum)):
                    raise ValueError(f"{metadata_name} payload_sha256hash must be a SHA-256 checksum")
                metadata["payload_sha256hash"] = payload_checksum.lower()
            return metadata
        except (OSError, json.JSONDecodeError, TypeError, ValueError, KeyError) as exc:
            raise OSError(f"Could not read bundled MAKO Renderer metadata from {manifest_path}: {exc}") from exc

    def _write_engine_state(
            self, archive_metadata: RendererArchiveMetadata) -> None:
        """Record exactly which pinned payload was installed by this plugin."""
        state = {
            "archive": archive_metadata["name"],
            "version": archive_metadata["version"],
            "sha256hash": archive_metadata["sha256hash"],
            "architectures": archive_metadata.get("architectures", ["64", "32"]),
            "host_architectures": archive_metadata.get(
                "host_architectures", ["x86_64"]
            ),
        }
        write_managed_text_atomically(
            self.engine_state_file,
            json.dumps(state, indent=2) + "\n",
            0o644,
            self.log,
        )

    def _detect_native_host_architecture(self) -> str:
        """Detect the host ISA even when Decky's Python runs through FEX."""
        return detect_host_environment(
            self.log,
            armada_marker=ARMADA_DEVICE_ENV,
        ).native_architecture

    def _host_compatibility(
            self,
            archive_metadata: RendererArchiveMetadata,
    ) -> tuple[str, bool, Optional[str]]:
        """Return the detected host, support result, and a stable user message."""
        detected = self._detect_native_host_architecture()
        supported = archive_metadata.get("host_architectures", ["x86_64"])
        if detected in supported:
            return detected, True, None

        supported_text = ", ".join(supported)
        return detected, False, (
            "MAKO Renderer is disabled on this host: this package supports "
            f"native host architecture {supported_text}, but this device is "
            f"{detected}. The current release does not include a validated "
            "native Armada/AArch64 Renderer. Games remain on their normal "
            "Armada launch path."
        )

    def current_package_host_compatibility(
            self) -> tuple[str, bool, Optional[str]]:
        """Evaluate the bundled Renderer against the native host."""
        return self._host_compatibility(
            self._bundled_archive_metadata(PLUGIN_ROOT)
        )

    def _validate_host_architecture(
            self, archive_metadata: RendererArchiveMetadata) -> None:
        """Reject a Renderer archive built for a different native host ISA."""
        _detected, supported, error = self._host_compatibility(archive_metadata)
        if supported:
            return
        raise OSError(error or "MAKO Renderer package is incompatible with this host")

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

    def _read_engine_state(self) -> Optional[InstalledEngineState]:
        """Return the plugin-managed payload record, if one exists."""
        try:
            state = json.loads(self.engine_state_file.read_text(encoding="utf-8"))
            if not isinstance(state, dict):
                return None
            if not all(isinstance(state.get(key), str) and state[key] for key in ("archive", "version", "sha256hash")):
                return None
            return cast(InstalledEngineState, state)
        except (OSError, json.JSONDecodeError):
            return None

    def _write_active_renderer_state(
            self, archive_metadata: RendererArchiveMetadata) -> None:
        """Record that MAKO Decky's bundled payload is the active native Renderer."""
        state: ActiveRendererState = {
            "schema_version": ACTIVE_RENDERER_STATE_SCHEMA_VERSION,
            "owner": ACTIVE_RENDERER_OWNER_DECKY,
            "version": archive_metadata["version"],
            "sha256hash": archive_metadata["sha256hash"],
        }
        if "payload_sha256hash" in archive_metadata:
            state["payload_sha256hash"] = archive_metadata["payload_sha256hash"]
        write_managed_text_atomically(
            self.active_renderer_state_file,
            json.dumps(state, indent=2) + "\n",
            0o644,
            self.log,
        )

    def _read_active_renderer_state(self) -> Optional[ActiveRendererState]:
        """Return the latest installer's native Renderer identity, if valid."""
        try:
            state = json.loads(
                self.active_renderer_state_file.read_text(encoding="utf-8")
            )
            if not isinstance(state, dict):
                return None
            if state.get("schema_version") != ACTIVE_RENDERER_STATE_SCHEMA_VERSION:
                return None
            if state.get("owner") not in (
                ACTIVE_RENDERER_OWNER_DECKY,
                ACTIVE_RENDERER_OWNER_STANDALONE,
            ):
                return None
            if (
                state["owner"] == ACTIVE_RENDERER_OWNER_STANDALONE
                and not self.standalone_installer_state_file.is_file()
            ):
                return None
            if not isinstance(state.get("version"), str) or not state["version"]:
                return None
            for key in ("sha256hash", "payload_sha256hash"):
                checksum = state.get(key)
                if key in state and (
                    not isinstance(checksum, str)
                    or len(checksum) != 64
                    or any(character not in "0123456789abcdefABCDEF" for character in checksum)
                ):
                    return None
                if checksum is not None:
                    state[key] = checksum.lower()
            return cast(ActiveRendererState, state)
        except (OSError, json.JSONDecodeError, TypeError):
            return None

    def _active_manifest_owner(self) -> Optional[str]:
        """Identify which managed library the shared private manifest selects."""
        return manifest_owner(self.json_file, self.lib_file, self.standalone_lib_file)

    def _active_renderer_library_file(self) -> Path:
        """Return the native library selected by the shared private manifest."""
        if self._active_manifest_owner() == ACTIVE_RENDERER_OWNER_STANDALONE:
            return self.standalone_lib_file
        return self.lib_file

    def selected_vrr_lease_file(self) -> Path:
        """Return the VRR lease helper shipped with the selected native Renderer."""
        if self._active_manifest_owner() == ACTIVE_RENDERER_OWNER_STANDALONE:
            return self.standalone_vrr_lease_file
        return self.vrr_lease_file

    def _native_payload_exists(
            self, owner: str, expects_32bit: bool) -> bool:
        """Return whether the selected managed native payload is complete."""
        if owner == ACTIVE_RENDERER_OWNER_STANDALONE:
            required_files = (
                self.standalone_lib_file,
                self.standalone_vrr_lease_file,
                self.json_file,
                self.standalone_spatial_scaling_lib_file,
                self.spatial_scaling_json_file,
                self.registered_json_file,
                self.standalone_vkbasalt_lib_file,
                self.vkbasalt_manifest,
                *(self.renderer_vkbasalt_shader_dir / filename
                  for filename in VKBASALT_SHADER_ASSET_FILENAMES),
            )
            optional_32bit_files = (
                self.standalone_lib32_file,
                self.json32_file,
                self.standalone_spatial_scaling_lib32_file,
                self.spatial_scaling_json32_file,
                self.registered_json32_file,
                self.standalone_vkbasalt_lib32_file,
                self.vkbasalt_manifest32,
            )
        else:
            inventory = self._renderer_file_inventory()
            required_files = (
                *inventory.required_archive.values(),
                self.registered_json_file,
            )
            optional_32bit_files = (
                *inventory.optional_32bit_archive.values(),
                self.registered_json32_file,
            )
        return all(path.is_file() for path in required_files) and (
            not expects_32bit or
            all(path.is_file() for path in optional_32bit_files)
        )

    def prepare_active_standalone_for_decky(self) -> bool:
        """Create Decky's wrapper when a standalone payload is already active."""
        expected = self._bundled_archive_metadata(PLUGIN_ROOT)
        _host, host_supported, _error = self._host_compatibility(expected)
        if not host_supported:
            return False
        if self._active_manifest_owner() != ACTIVE_RENDERER_OWNER_STANDALONE:
            return False
        if not self.standalone_installer_state_file.is_file():
            return False
        if not self._native_payload_exists(
            ACTIVE_RENDERER_OWNER_STANDALONE,
            "32" in expected.get("architectures", ["64", "32"]),
        ):
            return False
        if self.mako_script_path.exists():
            return False

        self._create_mako_launch_script()
        return True

    def _renderer_file_inventory(self) -> RendererFileInventory:
        """Declare every file owned by a native Decky Renderer installation."""
        required_destinations = {
            f"bin/{VRR_LEASE_FILENAME}": self.vrr_lease_file,
            f"lib/{LIB_FILENAME}": self.lib_file,
            f"share/vulkan/implicit_layer.d/{JSON_FILENAME}": self.json_file,
            f"lib/{SPATIAL_SCALING_LIB_FILENAME}":
                self.spatial_scaling_lib_file,
            f"share/vulkan/implicit_layer.d/{SPATIAL_SCALING_JSON_FILENAME}":
                self.spatial_scaling_json_file,
            f"lib/vkbasalt/{VKBASALT_LIB_FILENAME}":
                self.vkbasalt_lib_file,
            f"share/mako-render/vulkan/vkbasalt.d/{VKBASALT_MANIFEST_FILENAME_64}":
                self.vkbasalt_manifest,
            **{
                f"share/mako-render/vkbasalt-shaders/{filename}":
                    self.renderer_vkbasalt_shader_dir / filename
                for filename in VKBASALT_SHADER_ASSET_FILENAMES
            },
        }
        optional_32bit_destinations = {
            f"lib32/{LIB_FILENAME}": self.lib32_file,
            f"share/vulkan/implicit_layer.d/{JSON32_FILENAME}": self.json32_file,
            f"lib32/{SPATIAL_SCALING_LIB_FILENAME}":
                self.spatial_scaling_lib32_file,
            f"share/vulkan/implicit_layer.d/{SPATIAL_SCALING_JSON32_FILENAME}":
                self.spatial_scaling_json32_file,
            f"lib32/vkbasalt/{VKBASALT_LIB_FILENAME}":
                self.vkbasalt_lib32_file,
            f"share/mako-render/vulkan/vkbasalt.d/{VKBASALT_MANIFEST_FILENAME_32}":
                self.vkbasalt_manifest32,
        }
        return RendererFileInventory(
            required_archive=required_destinations,
            optional_32bit_archive=optional_32bit_destinations,
            # Published 4.0 archives predate native Remote Play. They remain
            # installable; enabling the override requires the current helper.
            # MAKO FG is a separate program: the Renderer works without it.
            optional_archive={**{
                                  f"lib/{MAKO_FG_DIRECTORY}/{name}":
                                      self.lib_file.parent / MAKO_FG_DIRECTORY / name
                                  for name in MAKO_FG_FILES
                              },
                              f"bin/{CLI_FILENAME}": self.cli_file,
                              f"bin/{SHADER_TOOL_FILENAME}": self.cli_file.with_name(SHADER_TOOL_FILENAME),
                              SDR_HELPER_RELATIVE_PATH: self.remote_play_sdr_file,
                              "bin/mako-installer": self.standalone_install_prefix / "bin/mako-installer",
                              "bin/mako-remote-play": self.standalone_install_prefix / "bin/mako-remote-play",
                              "share/applications/io.github.eugeniosegala.mako.uninstaller.desktop":
                                  self.standalone_install_prefix / "share/applications/io.github.eugeniosegala.mako.uninstaller.desktop",
                              **{
                                  f"share/mako-render/mako_remote_play/{filename}":
                                      self.standalone_install_prefix / "share/mako-render/mako_remote_play" / filename
                                  for filename in ("__init__.py", "remote_play_core.py", "remote_play_launch.py", "managed_files.py")
                              }},
            generated=(
                self.registered_json_file, self.registered_json32_file,
                self.registered_spatial_scaling_json_file,
                self.registered_spatial_scaling_json32_file,
                self.gamescope_wsi_compatibility_manifest,
                self.gamescope_wsi_compatibility_library,
                self.mangohud_manifest, self.mangohud_manifest32,
                self.engine_state_file, self.active_renderer_state_file,
                self.mako_script_path, self.diagnostics_script_path,
            ),
        )

    def _uninstaller_desktop_text(self, source: Path) -> str:
        """Use the standalone installer's desktop quoting and path checks."""
        executable = str(self.standalone_install_prefix / "bin/mako-installer")
        if "\n" in executable or "\r" in executable:
            raise OSError("The MAKO installation path cannot contain a line break")
        executable = executable.replace("\\", "\\\\").replace('"', '\\"').replace("`", "\\`").replace("$", "\\$").replace("%", "%%")
        content = source.read_text(encoding="utf-8")
        if sum(line.startswith("Exec=") for line in content.splitlines()) != 1:
            raise OSError("The MAKO Renderer uninstaller desktop entry must contain one Exec line")
        return "\n".join(
            f'Exec="{executable}" --uninstall' if line.startswith("Exec=") else line
            for line in content.splitlines()
        ) + "\n"

    def _install_archive_file(self, source: Path, destination: Path, filename: str) -> None:
        """Publish a non-layer archive entry, including its desktop command."""
        if filename == "io.github.eugeniosegala.mako.uninstaller.desktop":
            write_managed_text_atomically(destination, self._uninstaller_desktop_text(source), 0o644, self.log)
        else:
            copy_managed_file_atomically(
                source, destination,
                0o755 if filename in (CLI_FILENAME, VRR_LEASE_FILENAME, SHADER_TOOL_FILENAME, MAKO_FG_HOST_FILENAME, "mako-installer", "mako-remote-play") else 0o644,
                self.log,
            )

    def _management_destinations(self) -> dict[str, Path]:
        return {
            name: path for name, path in self._renderer_file_inventory().optional_archive.items()
            if name not in (f"bin/{CLI_FILENAME}", f"bin/{SHADER_TOOL_FILENAME}", SDR_HELPER_RELATIVE_PATH)
            and f"/{MAKO_FG_DIRECTORY}/" not in name
        }

    def _packaged_management_updates(self) -> list[tuple[Path, Path]]:
        """Prepare only changed management destinations before acquiring backups."""
        root = PLUGIN_ROOT / BIN_DIR / "renderer-management"
        if not root.exists():
            return []
        management = self._management_destinations()
        if not all((root / name).is_file() for name in management):
            raise OSError("Packaged MAKO Renderer management tools are incomplete")
        updates = []
        for name, destination in management.items():
            source = root / name
            content = (self._uninstaller_desktop_text(source).encode("utf-8")
                       if name.endswith(".desktop") else source.read_bytes())
            owner_mode = 0o700 if name.startswith("bin/") else 0o600
            if (not destination.is_file() or destination.is_symlink()
                    or destination.read_bytes() != content
                    or destination.stat().st_mode & owner_mode != owner_mode):
                updates.append((source, destination))
        return updates

    def _sync_packaged_management_tools(
            self, updates: Optional[list[tuple[Path, Path]]] = None) -> bool:
        """Called inside the native lock/transaction; never replace layer bytes."""
        if updates is None:
            updates = self._packaged_management_updates()
        for source, destination in updates:
            destination.parent.mkdir(parents=True, exist_ok=True)
            self._install_archive_file(source, destination, source.name)
        return bool(updates)

    def ensure_native_uninstaller_if_needed(self) -> bool:
        """Give legacy Decky-only installs an exit path without replacing layers."""
        if (self._active_manifest_owner() != ACTIVE_RENDERER_OWNER_DECKY
                or not self.lib_file.is_file()):
            return False
        management = self._management_destinations()
        if (PLUGIN_ROOT / BIN_DIR / "renderer-management").exists():
            with self.native_installation_lock():
                if self._active_manifest_owner() != ACTIVE_RENDERER_OWNER_DECKY:
                    return False
                updates = self._packaged_management_updates()
                if not updates:
                    return False
                with managed_install_transaction([destination for _source, destination in updates], self.log):
                    return self._sync_packaged_management_tools(updates)
        # Compatibility for older plugin packages that supply tools only in
        # their native archive. Current packaging always carries current tools.
        if all(path.is_file() and (not name.startswith("bin/") or os.access(path, os.X_OK))
               for name, path in management.items()):
            return False
        metadata = self._bundled_archive_metadata(PLUGIN_ROOT)
        archive_path = PLUGIN_ROOT / BIN_DIR / metadata["name"]
        self._validate_archive_checksum(archive_path, metadata["sha256hash"])
        with self.native_installation_lock(), tarfile.open(archive_path, "r:xz") as archive:
            if self._active_manifest_owner() != ACTIVE_RENDERER_OWNER_DECKY:
                return False
            entries = {entry.name.removeprefix("./"): entry for entry in archive.getmembers() if entry.isfile()}
            # Old release archives do not provide these optional management tools.
            if not any(name in entries for name in management):
                return False
            if not all(name in entries for name in management):
                raise OSError("Bundled MAKO Renderer management tools are incomplete")
            with tempfile.TemporaryDirectory(prefix=".mako-management-", dir=self.standalone_install_prefix) as temporary:
                with managed_install_transaction(list(management.values()), self.log):
                    for index, (name, destination) in enumerate(management.items()):
                        source = archive.extractfile(entries[name])
                        assert source is not None
                        staged = Path(temporary) / str(index)
                        with source, staged.open("wb") as output:
                            shutil.copyfileobj(source, output)
                        destination.parent.mkdir(parents=True, exist_ok=True)
                        self._install_archive_file(staged, destination, Path(name).name)
        return True

    def _extract_and_install_files(self, archive_path: Path) -> None:
        """Install validated archive members at their managed destinations."""
        inventory = self._renderer_file_inventory()
        required_destinations = inventory.required_archive
        optional_32bit_destinations = inventory.optional_32bit_archive
        destinations = {
            **required_destinations,
            **optional_32bit_destinations,
            **inventory.optional_archive,
        }
        # Keep staging in MAKO's user-owned data directory. Armada currently
        # runs Decky through FEX, whose translated /tmp mount has produced
        # permission errors while handling native Vulkan-layer files.
        staging_parent = self.local_lib_dir.parent
        staging_parent.mkdir(parents=True, exist_ok=True)
        with tarfile.open(archive_path, "r:xz") as archive:
            with tempfile.TemporaryDirectory(
                    prefix=".mako-install-", dir=staging_parent) as temp_dir:
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
                has_32bit_scaling_library = (
                    self.spatial_scaling_lib32_file in staged_files
                )
                has_32bit_scaling_manifest = (
                    self.spatial_scaling_json32_file in staged_files
                )
                has_32bit_vkbasalt_library = (
                    self.vkbasalt_lib32_file in staged_files
                )
                has_32bit_vkbasalt_manifest = (
                    self.vkbasalt_manifest32 in staged_files
                )
                if (
                    has_32bit_library != has_32bit_manifest or
                    has_32bit_scaling_library !=
                        has_32bit_scaling_manifest or
                    has_32bit_library != has_32bit_scaling_library or
                    has_32bit_vkbasalt_library !=
                        has_32bit_vkbasalt_manifest or
                    has_32bit_library != has_32bit_vkbasalt_library
                ):
                    raise OSError(
                        "Archive contained an incomplete 32-bit MAKO layer chain"
                    )

                frame_generation_binaries = [staged_files[self.lib_file][0]]
                if has_32bit_library:
                    frame_generation_binaries.append(
                        staged_files[self.lib32_file][0]
                    )
                scaling_binaries = [
                    staged_files[self.spatial_scaling_lib_file][0]
                ]
                if has_32bit_scaling_library:
                    scaling_binaries.append(
                        staged_files[self.spatial_scaling_lib32_file][0]
                    )
                self._validate_layer_binary_identity(
                    MAKO_LAYER_BUILD_MARKER, *frame_generation_binaries
                )
                self._validate_layer_binary_identity(
                    SPATIAL_SCALING_LAYER_BUILD_MARKER,
                    *scaling_binaries,
                )
                vkbasalt_binaries = [
                    staged_files[self.vkbasalt_lib_file][0]
                ]
                if has_32bit_vkbasalt_library:
                    vkbasalt_binaries.append(
                        staged_files[self.vkbasalt_lib32_file][0]
                    )
                self._validate_vkbasalt_binary_identity(*vkbasalt_binaries)

                for destination, (temp_file, filename) in staged_files.items():
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    if filename == JSON_FILENAME:
                        self._copy_and_fix_json_file(
                            temp_file, destination, f"../../lib/{LIB_FILENAME}", "64"
                        )
                    elif filename == JSON32_FILENAME:
                        self._copy_and_fix_json_file(
                            temp_file, destination, f"../../lib32/{LIB_FILENAME}", "32"
                        )
                    elif filename == SPATIAL_SCALING_JSON_FILENAME:
                        self._copy_and_fix_json_file(
                            temp_file, destination,
                            f"../../lib/{SPATIAL_SCALING_LIB_FILENAME}", "64",
                            layer_name=SPATIAL_SCALING_LAYER_NAME,
                            description="MAKO Renderer spatial-scaling layer",
                            enable_environment={
                                SPATIAL_SCALING_LAYER_ENABLE_ENV: "1"
                            },
                            disable_environment={
                                SPATIAL_SCALING_LAYER_DISABLE_ENV: "1"
                            },
                        )
                    elif filename == SPATIAL_SCALING_JSON32_FILENAME:
                        self._copy_and_fix_json_file(
                            temp_file, destination,
                            f"../../lib32/{SPATIAL_SCALING_LIB_FILENAME}", "32",
                            layer_name=SPATIAL_SCALING_LAYER_NAME,
                            description="MAKO Renderer spatial-scaling layer",
                            enable_environment={
                                SPATIAL_SCALING_LAYER_ENABLE_ENV: "1"
                            },
                            disable_environment={
                                SPATIAL_SCALING_LAYER_DISABLE_ENV: "1"
                            },
                        )
                    elif filename == VKBASALT_MANIFEST_FILENAME_64:
                        self._copy_and_fix_json_file(
                            temp_file,
                            destination,
                            f"../../lib/vkbasalt/{VKBASALT_LIB_FILENAME}",
                            "64",
                            layer_name=VKBASALT_LAYER_NAME_64,
                            description="vkBasalt post processing layer (MAKO build)",
                            enable_environment={
                                VKBASALT_LAYER_ENABLE_ENV: "1"
                            },
                            disable_environment={
                                VKBASALT_LAYER_DISABLE_ENV: "1"
                            },
                        )
                    elif filename == VKBASALT_MANIFEST_FILENAME_32:
                        self._copy_and_fix_json_file(
                            temp_file,
                            destination,
                            f"../../lib32/vkbasalt/{VKBASALT_LIB_FILENAME}",
                            "32",
                            layer_name=VKBASALT_LAYER_NAME_32,
                            description="vkBasalt post processing layer (MAKO build)",
                            enable_environment={
                                VKBASALT_LAYER_ENABLE_ENV: "1"
                            },
                            disable_environment={
                                VKBASALT_LAYER_DISABLE_ENV: "1"
                            },
                        )
                    else:
                        self._install_archive_file(temp_file, destination, filename)
                    self.log.info("Installed %s to %s", filename, destination)

                shader_tool = self.cli_file.with_name(SHADER_TOOL_FILENAME)
                if shader_tool not in staged_files:
                    self._remove_if_exists(shader_tool)

                if not has_32bit_library:
                    # A 64-bit-only local test package must not leave a stale
                    # 32-bit layer from an older install discoverable.
                    self._remove_if_exists(self.lib32_file)
                    self._remove_if_exists(self.json32_file)
                    self._remove_if_exists(self.spatial_scaling_lib32_file)
                    self._remove_if_exists(self.spatial_scaling_json32_file)
                    self._remove_if_exists(self.vkbasalt_lib32_file)
                    self._remove_if_exists(self.vkbasalt_manifest32)

    @staticmethod
    def _validate_layer_binary_identity(
            build_marker: bytes, *layer_binaries: Path) -> None:
        """Reject payloads incompatible with this plugin's generated wrapper."""
        for layer_binary in layer_binaries:
            content = layer_binary.read_bytes()
            if build_marker not in content:
                raise OSError(
                    f"MAKO layer build marker is missing from {layer_binary}"
                )
            if MAKO_PROFILE_FALLBACK_MARKER not in content:
                raise OSError(
                    "MAKO layer does not support the profile-fallback wrapper "
                    f"protocol: {layer_binary}"
                )

    @staticmethod
    def _validate_vkbasalt_binary_identity(*layer_binaries: Path) -> None:
        """Reject a bundled file that is not the pinned vkBasalt layer."""
        for layer_binary in layer_binaries:
            content = layer_binary.read_bytes()
            if (
                b"vkBasalt_GetInstanceProcAddr" not in content
                or b"vkBasalt_GetDeviceProcAddr" not in content
            ):
                raise OSError(
                    f"vkBasalt layer entrypoints are missing from {layer_binary}"
                )

    def _copy_and_fix_json_file(
            self, src_file: Path, dst_file: Path,
            library_path: str, library_arch: str,
            *,
            layer_name: str = MAKO_LAYER_NAME,
            description: str = "MAKO Renderer graphics layer",
            enable_environment: Optional[Dict[str, str]] = None,
            disable_environment: Optional[Dict[str, str]] = None,
    ) -> None:
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

            layer["name"] = layer_name
            layer["description"] = description
            layer["library_path"] = library_path
            layer["library_arch"] = library_arch
            layer["enable_environment"] = enable_environment or {
                MAKO_LAYER_ENABLE_ENV: "1",
            }
            layer["disable_environment"] = disable_environment or {
                MAKO_LAYER_DISABLE_ENV: "1",
            }
            write_managed_text_atomically(
                dst_file,
                json.dumps(json_data, indent=2) + "\n",
                0o644,
                self.log,
            )
        except (json.JSONDecodeError, OSError, TypeError, ValueError) as error:
            raise OSError(
                f"Could not install Vulkan layer manifest {dst_file} from {src_file}: {error}"
            ) from error

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
        # Decky's wrapper uses its private scaling manifests. Selecting Decky
        # must not leave a standalone public registration on the old payload.
        self._remove_if_exists(self.registered_spatial_scaling_json_file)
        self._remove_if_exists(self.registered_spatial_scaling_json32_file)

    def migrate_gamescope_wsi_compatibility_manifest_if_needed(self) -> bool:
        """Validate and stage the host WSI manifest for installed Renderers.

        MAKO Decky upgrades do not necessarily reinstall MAKO Renderer, so the
        compatibility manifest must also be repairable during normal plugin
        startup. Keep the operation idempotent and leave uninstalled Renderers
        untouched.
        """
        if not self._active_renderer_library_file().is_file():
            return False

        source = (
            HOST_SYSTEM_IMPLICIT_LAYER_DIR /
            GAMESCOPE_WSI_MANIFEST_FILENAME_64
        )
        destination = self.gamescope_wsi_compatibility_manifest
        library_destination = self.gamescope_wsi_compatibility_library

        try:
            manifest = json.loads(source.read_text(encoding="utf-8"))
            layer = manifest.get("layer")
            if not isinstance(layer, dict):
                raise ValueError("missing layer object")
            if layer.get("name") != GAMESCOPE_WSI_LAYER_NAME_64:
                raise ValueError("unexpected layer identity")
            if layer.get("type") != "GLOBAL":
                raise ValueError("unexpected layer type")
            library_path = layer.get("library_path")
            if not isinstance(library_path, str) or not Path(library_path).is_absolute():
                raise ValueError("library_path must be absolute")
            library_source = Path(library_path)
            if not library_source.is_file():
                raise ValueError("library_path is unavailable")
            if layer.get("library_arch") not in (None, "64"):
                raise ValueError("manifest is not a 64-bit layer")
            if layer.get("enable_environment") != {
                GAMESCOPE_WSI_ENABLE_ENV: "1"
            }:
                raise ValueError("unexpected enable_environment gate")
            if layer.get("disable_environment") != {
                GAMESCOPE_WSI_DISABLE_ENV: "1"
            }:
                raise ValueError("unexpected disable_environment gate")
        except (OSError, json.JSONDecodeError, TypeError, ValueError) as error:
            destination.unlink(missing_ok=True)
            library_destination.unlink(missing_ok=True)
            self.log.warning(
                "Gamescope WSI compatibility remains unavailable: %s",
                error,
            )
            return False

        try:
            library_updated = not (
                library_destination.is_file()
                and library_destination.stat().st_mode & 0o777 == 0o755
                and library_destination.read_bytes()
                == library_source.read_bytes()
            )
        except OSError:
            library_updated = True
        if library_updated:
            copy_managed_file_atomically(
                library_source, library_destination, 0o755, self.log
            )
        layer["library_path"] = str(library_destination)
        manifest_content = json.dumps(manifest, indent=4) + "\n"
        manifest_updated = write_managed_text_atomically(
            destination, manifest_content, 0o644, self.log
        )
        self.log.info(
            "Installed guarded Gamescope WSI compatibility payload at %s",
            destination,
        )
        return library_updated or manifest_updated

    def _stage_guarded_host_manifest(
            self,
            source_names: tuple[str, ...],
            destination: Path,
            expected_layer_name: str,
            expected_library_arch: str,
            expected_enable_environment: Dict[str, str],
            expected_disable_environment: Dict[str, str],
    ) -> bool:
        """Stage one exact host layer identity without exposing its directory."""
        source = next((
            HOST_SYSTEM_IMPLICIT_LAYER_DIR / name
            for name in source_names
            if (HOST_SYSTEM_IMPLICIT_LAYER_DIR / name).is_file()
        ), None)
        if source is None:
            return self._remove_if_exists(destination)

        try:
            manifest = json.loads(source.read_text(encoding="utf-8"))
            layer = manifest.get("layer")
            if not isinstance(layer, dict):
                raise ValueError("missing layer object")
            if layer.get("name") != expected_layer_name:
                raise ValueError("unexpected layer identity")
            if layer.get("type") != "GLOBAL":
                raise ValueError("unexpected layer type")
            library_path = layer.get("library_path")
            if not isinstance(library_path, str) or not library_path:
                raise ValueError("missing library_path")
            if Path(library_path).is_absolute() and not Path(library_path).is_file():
                raise ValueError("absolute library_path is unavailable")
            if layer.get("library_arch") not in (
                    None, expected_library_arch):
                raise ValueError(
                    f"manifest is not a {expected_library_arch}-bit layer"
                )
            if layer.get("enable_environment") != expected_enable_environment:
                raise ValueError("unexpected enable_environment gate")
            if layer.get("disable_environment") != expected_disable_environment:
                raise ValueError("unexpected disable_environment gate")
            # The host manifests do not consistently declare architecture.
            # Stamp the validated filename/identity contract into the managed
            # copy so a directory containing both variants stays deterministic.
            layer["library_arch"] = expected_library_arch
            managed_content = json.dumps(manifest, indent=2) + "\n"
        except (OSError, json.JSONDecodeError, TypeError, ValueError) as error:
            changed = self._remove_if_exists(destination)
            self.log.warning(
                "Optional Vulkan layer %s remains unavailable: %s",
                expected_layer_name,
                error,
            )
            return changed

        try:
            if (
                    destination.is_file() and
                    destination.read_text(encoding="utf-8") == managed_content
            ):
                return False
        except OSError:
            pass

        write_managed_text_atomically(
            destination,
            managed_content,
            0o644,
            self.log,
        )
        self.log.info(
            "Installed guarded optional Vulkan manifest %s at %s",
            expected_layer_name,
            destination,
        )
        return True

    def refresh_guarded_postprocess_manifests_if_needed(self) -> bool:
        """Stage only the exact supported host post-process manifests.

        This is intentionally independent from profile selection. The wrapper
        exposes at most one private tool directory when its matching control
        is selected. Each directory may contain the validated 64-bit and
        32-bit identities for that one tool, never unrelated host layers.

        vkBasalt is part of the checksummed Renderer payload and is never
        replaced with a host-installed copy. Remove a damaged manifest if its
        matching bundled library disappears so the wrapper fails closed.
        """
        if not self._active_renderer_library_file().is_file():
            return False
        mangohud_changed = self._stage_guarded_host_manifest(
            (MANGOHUD_MANIFEST_FILENAME_64,),
            self.mangohud_manifest,
            MANGOHUD_LAYER_NAME_64,
            "64",
            {"MANGOHUD": "1"},
            {"DISABLE_MANGOHUD": "1"},
        )
        mangohud32_changed = self._stage_guarded_host_manifest(
            (MANGOHUD_MANIFEST_FILENAME_32,),
            self.mangohud_manifest32,
            MANGOHUD_LAYER_NAME_32,
            "32",
            {"MANGOHUD": "1"},
            {"DISABLE_MANGOHUD": "1"},
        )
        vkbasalt_changed = False
        for library, manifest in (
            (self.vkbasalt_lib_file, self.vkbasalt_manifest),
            (self.vkbasalt_lib32_file, self.vkbasalt_manifest32),
        ):
            if manifest.is_file() and not library.is_file():
                vkbasalt_changed = (
                    self._remove_if_exists(manifest) or vkbasalt_changed
                )
                self.log.warning(
                    "Removed bundled vkBasalt manifest without its library: %s",
                    manifest,
                )
        return (
            mangohud_changed or mangohud32_changed or
            vkbasalt_changed
        )

    def _create_config_file(self) -> None:
        """Create or update this plugin's private TOML config with detected DLL path.

        Preserve valid profiles; recreate defaults if the existing file cannot be read or validated.
        """
        if (
            self.config_file_path.exists()
            and self.config_file_path.stat().st_mode & 0o222 == 0
        ):
            raise OSError(
                "MAKO Renderer configuration is read-only at "
                f"{self.config_file_path}. Restore owner write permission, then "
                "retry. Installation does not override read-only user configuration."
            )

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

            except Exception as error:
                self.log.warning(
                    "MAKO Decky: Could not read or merge configuration at %s: %s; "
                    "replacing it with defaults",
                    self.config_file_path, error,
                )
                config = ConfigurationManager.get_defaults_with_dll_detection(dll_service)
                toml_content = ConfigurationManager.generate_toml_content(config)
        else:
            # No existing config file, create a new one with defaults
            config = ConfigurationManager.get_defaults_with_dll_detection(dll_service)
            toml_content = ConfigurationManager.generate_toml_content(config)
            self.log.info(f"Creating new config file")

        # Write config file
        write_managed_text_atomically(
            self.config_file_path, toml_content, 0o644, self.log,
        )
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
        config_service.local_lib_dir = self.local_lib_dir
        config_service.local_share_dir = self.local_share_dir
        config_service.standalone_install_prefix = self.standalone_install_prefix
        config_service.spatial_scaling_layer_dir = (
            self.spatial_scaling_layer_dir
        )
        config_service.gamescope_wsi_compatibility_dir = (
            self.gamescope_wsi_compatibility_dir
        )
        config_service.mangohud_layer_dir = self.mangohud_layer_dir
        config_service.vkbasalt_layer_dir = self.vkbasalt_layer_dir
        config_service.config_dir = self.config_dir
        config_service.config_file_path = self.config_file_path
        config_service.wrapper_profile_settings_path = self.wrapper_profile_settings_path
        config_service.profile_metadata_path = self.profile_metadata_path
        config_service.mako_script_path = self.mako_script_path

        profile_data = config_service._get_profile_data()
        script_content = config_service._generate_script_content_for_profile(profile_data)

        # Write the script file
        write_managed_text_atomically(
            self.mako_script_path,
            script_content,
            0o755,
            self.log,
        )
        self.log.info(f"Created MAKO launch script at {self.mako_script_path}")

    def _install_diagnostics_helper(self, plugin_dir: Path) -> None:
        """Install the packaged read-only diagnostic filter beside the wrapper."""
        source = self._diagnostics_helper_source(plugin_dir)
        copy_managed_file_atomically(
            source,
            self.diagnostics_script_path,
            0o755,
            self.log,
        )
        self.log.info("Installed diagnostics helper to %s", self.diagnostics_script_path)

    @staticmethod
    def _diagnostics_helper_source(plugin_dir: Path) -> Path:
        """Resolve the release-ZIP helper, with a source-tree development fallback."""
        candidates = (
            plugin_dir / BIN_DIR / DIAGNOSTICS_HELPER_FILENAME,
            plugin_dir.parent / "scripts" / DIAGNOSTICS_HELPER_FILENAME,
        )
        for candidate in candidates:
            if candidate.is_file():
                return candidate
        raise OSError(f"Bundled diagnostics helper not found at {candidates[0]}")

    def migrate_diagnostics_helper_if_needed(self) -> bool:
        """Install or refresh the helper without requiring an engine reinstall."""
        source = self._diagnostics_helper_source(PLUGIN_ROOT)
        try:
            current = self.diagnostics_script_path.read_bytes()
            bundled = source.read_bytes()
            executable = bool(self.diagnostics_script_path.stat().st_mode & 0o111)
            if current == bundled and executable:
                return False
        except OSError:
            pass

        self._install_diagnostics_helper(PLUGIN_ROOT)
        return True

    def get_launch_script_path(self) -> str:
        """Get the path to the MAKO launch script.

        Returns:
            String path to the launch script file
        """
        return str(self.mako_script_path)

    def check_installation(self) -> InstallationCheckResponse:
        """Check if MAKO Renderer is already installed

        Returns:
            InstallationCheckResponse with installation status and file paths
        """
        try:
            manifest_owner = self._active_manifest_owner()
            selected_owner = (
                ACTIVE_RENDERER_OWNER_STANDALONE
                if manifest_owner == ACTIVE_RENDERER_OWNER_STANDALONE
                else ACTIVE_RENDERER_OWNER_DECKY
            )
            selected_lib_file = (
                self.standalone_lib_file
                if selected_owner == ACTIVE_RENDERER_OWNER_STANDALONE
                else self.lib_file
            )
            selected_lib32_file = (
                self.standalone_lib32_file
                if selected_owner == ACTIVE_RENDERER_OWNER_STANDALONE
                else self.lib32_file
            )
            selected_scaling_lib_file = (
                self.standalone_spatial_scaling_lib_file
                if selected_owner == ACTIVE_RENDERER_OWNER_STANDALONE
                else self.spatial_scaling_lib_file
            )
            selected_scaling_lib32_file = (
                self.standalone_spatial_scaling_lib32_file
                if selected_owner == ACTIVE_RENDERER_OWNER_STANDALONE
                else self.spatial_scaling_lib32_file
            )
            lib_exists = selected_lib_file.exists()
            lib32_exists = selected_lib32_file.exists()
            json_exists = self.json_file.exists()
            json32_exists = self.json32_file.exists()
            scaling_lib_exists = selected_scaling_lib_file.exists()
            scaling_lib32_exists = selected_scaling_lib32_file.exists()
            scaling_json_exists = self.spatial_scaling_json_file.exists()
            scaling_json32_exists = self.spatial_scaling_json32_file.exists()
            registered_json_exists = self.registered_json_file.exists()
            registered_json32_exists = self.registered_json32_file.exists()
            script_exists = self.mako_script_path.exists()
            expected = self._bundled_archive_metadata(PLUGIN_ROOT)
            host_architecture, host_supported, host_error = (
                self._host_compatibility(expected)
            )
            expects_32bit = "32" in expected.get("architectures", ["64", "32"])
            installed = (
                manifest_owner == selected_owner
                and self._native_payload_exists(selected_owner, expects_32bit)
                and script_exists
            )
            # Files left by a pre-boundary build do not make an incompatible
            # native host supported. Keep their presence observable for manual
            # cleanup while withholding all "installed" and update claims.
            installed = installed and host_supported
            active_state = self._read_active_renderer_state()
            state = self._read_engine_state()
            if (
                active_state is not None
                and (
                    manifest_owner is None
                    or active_state["owner"] == manifest_owner
                )
            ):
                version_known = True
                installed_version = active_state["version"]
                active_checksum = active_state.get("sha256hash")
                update_required = installed and (
                    installed_version != expected["version"]
                    or (
                        active_state["owner"] == ACTIVE_RENDERER_OWNER_DECKY
                        and active_checksum != expected["sha256hash"]
                    )
                )
                if "payload_sha256hash" in expected:
                    # A version label cannot identify rebuilt binaries or an
                    # MAKO FG change. Compare the native manifest,
                    # independent of archive compression and installer owner.
                    update_required = installed and (
                        active_state.get("payload_sha256hash")
                        != expected["payload_sha256hash"]
                    )
                if (installed and not update_required
                        and active_state["owner"] == ACTIVE_RENDERER_OWNER_DECKY):
                    # The record matches; confirm the files still do too.
                    differences = self._installed_payload_differences(expected)
                    if differences:
                        self.log.warning(
                            "Installed MAKO Renderer files differ from the "
                            "bundled payload; offering an update: %s",
                            ", ".join(str(path) for path in differences),
                        )
                        update_required = True
            elif manifest_owner == ACTIVE_RENDERER_OWNER_STANDALONE:
                # Standalone installers predating active-renderer.json still
                # replace this shared manifest. Their exact version is unknown,
                # so offer Decky's bundled Renderer instead of trusting stale
                # installed-engine metadata from the previous Decky payload.
                version_known = False
                installed_version = None
                update_required = installed
            else:
                # Pre-active-state Decky installations remain readable until
                # either installer next selects a native Renderer.
                version_known = state is not None
                installed_version = state["version"] if state else None
                update_required = installed and (
                    state is None
                    or state["version"] != expected["version"]
                    or state["sha256hash"] != expected["sha256hash"]
                )

            self.log.info(
                "Installation check: lib64=%s, lib32=%s, private-json64=%s, "
                "private-json32=%s, scaling-lib64=%s, scaling-lib32=%s, "
                "scaling-json64=%s, scaling-json32=%s, "
                "registered-json64=%s, registered-json32=%s, script=%s",
                lib_exists, lib32_exists, json_exists, json32_exists,
                scaling_lib_exists, scaling_lib32_exists,
                scaling_json_exists, scaling_json32_exists,
                registered_json_exists, registered_json32_exists,
                script_exists,
            )

            return {
                "installed": installed,
                "lib_exists": lib_exists,
                "json_exists": json_exists,
                "script_exists": script_exists,
                "lib_path": str(selected_lib_file),
                "json_path": str(self.registered_json_file),
                "script_path": str(self.mako_script_path),
                "installed_engine_version": installed_version,
                "expected_engine_version": expected["version"],
                "engine_version_known": version_known,
                "engine_update_required": update_required,
                "host_architecture": host_architecture,
                "host_architecture_supported": host_supported,
                "system_renderer_notice": self._system_renderer_notice() if not installed else None,
                "error": host_error,
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
                "script_path": str(self.mako_script_path),
                "installed_engine_version": None,
                "expected_engine_version": None,
                "engine_version_known": False,
                "engine_update_required": False,
                "host_architecture": None,
                "host_architecture_supported": False,
                "system_renderer_notice": None,
                "error": str(e)
            }

    def _decky_renderer_files(self) -> list[Path]:
        """Return every native Renderer file directly managed by MAKO Decky."""
        return self._renderer_file_inventory().managed_paths()

    def _standalone_installer_entries(self) -> list[tuple[str, Path]]:
        """Validate the standalone installer's checksummed ownership record."""
        if not self.standalone_installer_state_file.is_file():
            return []

        entries: list[tuple[str, Path]] = []
        try:
            lines = self.standalone_installer_state_file.read_text(
                encoding="utf-8"
            ).splitlines()
        except (OSError, UnicodeError) as error:
            raise OSError(
                "Could not read the standalone MAKO Renderer ownership record: "
                f"{error}"
            ) from error

        for line in lines:
            checksum, separator, relative_text = line.partition("  ")
            relative_path = PurePosixPath(relative_text)
            if (
                not separator
                or len(checksum) != 64
                or any(
                    character not in "0123456789abcdefABCDEF"
                    for character in checksum
                )
                or not relative_text
                or relative_path.is_absolute()
                or relative_path == PurePosixPath(".")
                or ".." in relative_path.parts
            ):
                raise OSError(
                    "The standalone MAKO Renderer ownership record is invalid"
                )
            entries.append((
                checksum.lower(),
                self.standalone_install_prefix.joinpath(*relative_path.parts),
            ))

        if not entries:
            raise OSError(
                "The standalone MAKO Renderer ownership record is empty"
            )
        return entries

    @staticmethod
    def _file_sha256(path: Path) -> str:
        digest = hashlib.sha256()
        with path.open("rb") as input_file:
            for chunk in iter(lambda: input_file.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()

    def _remove_standalone_renderer_files(
            self, entries: list[tuple[str, Path]]) -> list[str]:
        """Remove checksummed files owned by the standalone archive installer."""
        if not entries:
            return []

        removed_files: list[str] = []
        for expected_checksum, file_path in entries:
            if not file_path.is_file():
                continue
            try:
                actual_checksum = self._file_sha256(file_path)
            except OSError as error:
                self.log.warning(
                    "Preserving unreadable standalone Renderer file %s: %s",
                    file_path,
                    error,
                )
                continue
            if actual_checksum != expected_checksum:
                self.log.warning(
                    "Preserving modified standalone Renderer file: %s",
                    file_path,
                )
                continue
            file_path.unlink()
            removed_files.append(str(file_path))

        if self.standalone_installer_state_file.exists():
            self.standalone_installer_state_file.unlink()
            removed_files.append(str(self.standalone_installer_state_file))
        try:
            self.standalone_installer_state_file.parent.rmdir()
        except OSError:
            pass
        return removed_files

    def _remove_empty_renderer_directories(
            self, file_paths: list[Path]) -> None:
        """Prune empty managed subdirectories without crossing ~/.local roots."""
        prefix = self.standalone_install_prefix
        for file_path in sorted(
            set(file_paths),
            key=lambda path: len(path.parts),
            reverse=True,
        ):
            try:
                relative_path = file_path.relative_to(prefix)
            except ValueError:
                continue
            if len(relative_path.parts) < 2:
                continue
            boundary = prefix / relative_path.parts[0]
            directory = file_path.parent
            while directory != boundary and boundary in directory.parents:
                try:
                    directory.rmdir()
                    self.log.info("Removed empty Renderer directory %s", directory)
                except OSError:
                    break
                directory = directory.parent
        # Keep the standard per-user bin, lib and share roots, which login PATH
        # setup and other software rely on. Only the multilib root is MAKO's.
        try:
            (prefix / "lib32").rmdir()
            self.log.info("Removed empty Renderer directory %s", prefix / "lib32")
        except OSError:
            pass

    def uninstall(self) -> UninstallationResponse:
        try:
            with self.native_installation_lock():
                return self.uninstall_locked()
        except OSError as error:
            return self._error_response(UninstallationResponse, str(error), message="", removed_files=None)

    def uninstall_locked(self) -> UninstallationResponse:
        """Uninstall the managed native Renderer while preserving profiles.

        The caller holds native_installation_lock() through any Remote Play
        restoration and native monitor shutdown preceding this file cleanup.

        MAKO Decky and the standalone archive select one active native Renderer.
        Removing it therefore cleans files supplied by either managed installer,
        while MAKO Decky itself remains installed when this RPC is used.

        Returns:
            UninstallationResponse with success status and removed files list
        """
        try:
            standalone_entries = self._standalone_installer_entries()
            decky_files = self._decky_renderer_files()
            if self._active_manifest_owner() == ACTIVE_RENDERER_OWNER_DECKY:
                # The selected Decky payload has legitimately replaced these
                # shared files since the standalone ledger was written.
                standalone_entries = [(checksum, path) for checksum, path in standalone_entries
                                      if path not in decky_files]
            removed_files = self._remove_standalone_renderer_files(standalone_entries)
            # Shared destinations can also be checksum-owned by the standalone
            # installer. Do not bypass its modified-file protection below.
            preserved = {path for _checksum, path in standalone_entries if path.exists() or path.is_symlink()}
            for file_path in decky_files:
                if file_path in preserved:
                    continue
                if self._remove_if_exists(file_path):
                    removed_files.append(str(file_path))
            self._remove_empty_renderer_directories(
                decky_files
                + [path for _checksum, path in standalone_entries]
                + [self.standalone_installer_state_file]
            )

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
        """Leave the independent native Renderer available after Decky removal."""
        self.log.info(
            "MAKO Decky removed; preserving MAKO Renderer, launchers, profiles, "
            "and the native Remote Play override. Use Uninstall MAKO Renderer "
            "or mako-installer --uninstall for explicit Renderer removal."
        )

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
            "current_profile": existing_profile_data.get(
                "current_profile", DEFAULT_PROFILE_NAME
            ),
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
            merged_data["profiles"][DEFAULT_PROFILE_NAME] = {
                k: v for k, v in default_config.items()
                if k not in ["dll", "allow_fp16"]  # Exclude global fields
            }
            merged_data["current_profile"] = DEFAULT_PROFILE_NAME
            self.log.info("No existing profiles found, created default profile")

        return ConfigurationManager._with_power_profiles(
            existing_profile_data, merged_data
        )
