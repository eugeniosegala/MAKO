"""Opt-in native Steam Remote Play entry-point installation and restoration."""

from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import stat
from typing import Iterator, Sequence, TypedDict

from .base_service import BaseService
from .managed_files import (
    copy_managed_file_atomically, managed_install_transaction,
    write_managed_text_atomically,
)
from .types import RemotePlayResponse, RuntimeContextState

REMOTE_PLAY_PROFILE = "Remote-Play"
MARKER = b"# MAKO_NATIVE_REMOTE_PLAY_OVERRIDE_V1"


class RemotePlayOverrideState(TypedDict):
    version: int
    enabled: bool
    profile: str
    wrapper_sha256: str


class RemotePlayService(BaseService):
    def __init__(self, logger=None):
        super().__init__(logger)
        self.client = self.user_home / ".local/share/Steam/ubuntu12_64/streaming_client"
        self.backup = self.client.with_name("streaming_client.mako-original")
        self.checksum = self.client.with_name("streaming_client.mako-original.sha256")
        self.state = self.config_dir / "native-remote-play.json"
        self.lock_path = self.config_dir / "native-remote-play.lock"
        self.proc_root = Path("/proc")

    @staticmethod
    def _digest(path: Path, expected_uid: int | None = None) -> str:
        descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        with os.fdopen(descriptor, "rb") as stream:
            identity = os.fstat(stream.fileno())
            if not stat.S_ISREG(identity.st_mode) or (expected_uid is not None and identity.st_uid != expected_uid):
                raise ValueError("Remote Play file identity changed")
            return hashlib.file_digest(stream, "sha256").hexdigest()

    @staticmethod
    def _native_client(path: Path) -> bool:
        if path.is_symlink() or not path.is_file() or not os.access(path, os.X_OK):
            return False
        with path.open("rb") as stream:
            header = stream.read(20)
        return len(header) >= 20 and header[:6] == b"\x7fELF\x02\x01" and header[18:20] == b"\x3e\0"

    def _installed(self) -> bool:
        if self.client.is_symlink():
            return False
        try:
            with self.client.open("rb") as stream:
                return MARKER in stream.read(256)
        except OSError:
            return False

    def running_pids(self) -> list[int]:
        """Exact executable paths, including the short Python entry-point phase."""
        paths = {str(self.client.resolve()), str(self.backup.resolve())}
        pids = []
        for proc in self.proc_root.glob("[0-9]*"):
            try:
                executable = os.readlink(proc / "exe").removesuffix(" (deleted)")
                arguments = (proc / "cmdline").read_bytes().split(b"\0") if Path(executable).name.startswith("python") else []
                if executable in paths or (
                    Path(executable).name.startswith("python") and
                    len(arguments) > 1 and os.fsdecode(arguments[1]) in paths
                ):
                    pids.append(int(proc.name))
            except (OSError, ValueError):
                continue
        return sorted(pids)

    def get_status(self, contexts: Sequence[RuntimeContextState] = ()) -> RemotePlayResponse:
        try:
            installed = self._installed()
            managed = installed or self.state.exists()
            pids = self.running_pids() if managed else []
            conflict = not installed and (self.backup.exists() or self.checksum.exists())
            return RemotePlayResponse(
                success=True, error=None,
                message="Steam client changed or an earlier override backup exists." if conflict else "",
                installed=installed, managed=managed,
                available=installed or self._native_client(self.client),
                running=bool(pids), pids=pids, profile_name=REMOTE_PLAY_PROFILE,
                frame_generation_active=installed and any(
                    context["pid"] in pids and context["phase"] == "active" and
                    context["frame_generation_active"] for context in contexts
                ), conflict=conflict,
            )
        except (OSError, ValueError) as error:
            return RemotePlayResponse(
                success=False, error=str(error), message=str(error),
                installed=False, managed=False, available=False, running=False,
                pids=[], profile_name=REMOTE_PLAY_PROFILE,
                frame_generation_active=False, conflict=False,
            )

    @contextmanager
    def mutation(self) -> Iterator[None]:
        """Fail promptly on a launch/mutation lease or a running native client."""
        self.config_dir.mkdir(parents=True, exist_ok=True)
        descriptor = os.open(self.lock_path, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600)
        with os.fdopen(descriptor, "r+") as lock:
            # Decky may run as root; the Steam user's self-contained wrapper reads this lock.
            owner = self.user_home.stat()
            os.fchown(lock.fileno(), owner.st_uid, owner.st_gid)
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as error:
                raise ValueError("Remote Play launch or another override change is in progress") from error
            if self.running_pids():
                raise ValueError("Close native Remote Play before changing its override")
            yield

    def _read_state(self) -> RemotePlayOverrideState:
        if self.state.is_symlink():
            raise ValueError("Remote Play state is a symlink")
        raw = json.loads(self.state.read_text())
        if not isinstance(raw, dict) or raw.get("version") != 1 or raw.get("enabled") is not True or raw.get("profile") != REMOTE_PLAY_PROFILE:
            raise ValueError("Invalid Remote Play override state")
        digest = raw.get("wrapper_sha256")
        if not isinstance(digest, str) or re.fullmatch(r"[0-9a-f]{64}", digest) is None:
            raise ValueError("Invalid Remote Play wrapper checksum")
        return RemotePlayOverrideState(version=1, enabled=True, profile=REMOTE_PLAY_PROFILE, wrapper_sha256=digest)

    def _validate_backup(self) -> RemotePlayOverrideState:
        if not self._native_client(self.backup):
            raise ValueError("Original Steam client backup is unavailable or invalid")
        if self.checksum.is_symlink() or self._digest(self.backup) != self.checksum.read_text().strip():
            raise ValueError("Original Steam client backup checksum mismatch")
        return self._read_state()

    def install_locked(self) -> None:
        """Called under mutation(), after the canonical profile is prepared."""
        if self._installed():
            state = self._validate_backup()
            if self._digest(self.client) != state["wrapper_sha256"]:
                raise ValueError("Steam client wrapper changed; refusing replacement")
            return
        for path in (self.backup, self.checksum, self.state):
            if path.exists() or path.is_symlink():
                raise ValueError("Existing Remote Play override files; remove or recover them first")
        experiment = self.user_home / ".local/share/vulkan/implicit_layer.d/VkLayer_MAKO_native_remote_play.json"
        if experiment.exists() or experiment.is_symlink():
            raise ValueError("Remove the experimental Remote Play Vulkan manifest first")
        if not self._native_client(self.client):
            raise ValueError("Native x86_64 Steam streaming client is unavailable")
        original_stat = self.client.stat()
        if original_stat.st_uid != self.user_home.stat().st_uid or original_stat.st_mode & (stat.S_ISUID | stat.S_ISGID):
            raise ValueError("Steam streaming client has unexpected ownership or privileges")
        if not self.mako_script_path.is_file() or not os.access(self.mako_script_path, os.X_OK):
            raise ValueError("Install MAKO Renderer first")
        payload = Path(__file__).with_name("remote_play_launch.py").read_text().replace(
            "home = Path('__MAKO_USER_HOME__')", f"home = Path({str(self.user_home)!r})",
        )
        compile(payload, "streaming_client", "exec")
        original_hash = self._digest(self.client, original_stat.st_uid)
        paths = [self.client, self.backup, self.checksum, self.state]
        with managed_install_transaction(paths, self.log):
            # Keep a privileged copy private until its bytes have been verified.
            copy_managed_file_atomically(self.client, self.backup, 0o600, self.log)
            if self._digest(self.backup) != original_hash or self._digest(self.client, original_stat.st_uid) != original_hash:
                raise ValueError("Steam updated during override installation")
            os.chmod(self.backup, stat.S_IMODE(original_stat.st_mode), follow_symlinks=False)
            write_managed_text_atomically(self.checksum, original_hash + "\n", 0o600, self.log)
            state = RemotePlayOverrideState(
                version=1, enabled=True, profile=REMOTE_PLAY_PROFILE,
                wrapper_sha256=hashlib.sha256(payload.encode()).hexdigest(),
            )
            write_managed_text_atomically(self.state, json.dumps(state) + "\n", 0o600, self.log)
            write_managed_text_atomically(self.client, payload, stat.S_IMODE(original_stat.st_mode), self.log)
            for path in paths:
                os.chown(path, original_stat.st_uid, original_stat.st_gid, follow_symlinks=False)

    def install(self) -> None:
        with self.mutation():
            self.install_locked()

    def remove(self) -> None:
        with self.mutation():
            state = self._validate_backup()
            installed = self._installed()
            if self.client.is_symlink() or not self.client.is_file():
                raise ValueError("Steam client entry is missing or a symlink; manual recovery required")
            if installed and self._digest(self.client) != state["wrapper_sha256"]:
                raise ValueError("Steam client wrapper changed; refusing to overwrite it")
            if not installed and not self._native_client(self.client):
                raise ValueError("Steam client entry changed unexpectedly; manual recovery required")
            with managed_install_transaction([self.client, self.backup, self.checksum, self.state], self.log):
                if installed:
                    os.replace(self.backup, self.client)
                else:
                    # Steam already replaced the entry point. Retire our stale
                    # backup; never restore it over the updated Steam binary.
                    self.backup.unlink()
                self.checksum.unlink()
                self.state.unlink()

    def restore_before_uninstall(self) -> None:
        if self._installed() or self.state.exists():
            self.remove()
