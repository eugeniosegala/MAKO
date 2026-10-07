"""Opt-in native Steam Remote Play entry-point installation and restoration."""

from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
from typing import Iterator, Sequence, TypedDict

import logging
from .remote_play_launch import (
    launch_settings, open_regular, selected_profile, check_sdr_helper, SDR_HELPER_RELATIVE_PATH,
)
from .managed_files import (
    copy_managed_file_atomically, managed_install_transaction,
    write_managed_text_atomically, sync_managed_directory,
)
class RemotePlayResponse(TypedDict):
    success: bool
    error: str | None
    message: str
    installed: bool
    managed: bool
    available: bool
    running: bool
    pids: list[int]
    profile_name: str
    frame_generation_active: bool
    conflict: bool


class RuntimeContextState(TypedDict):
    pid: int
    phase: str
    frame_generation_active: bool

MARKER = b"# MAKO_NATIVE_REMOTE_PLAY_OVERRIDE_V3"


class RemotePlayOverrideState(TypedDict):
    version: int
    enabled: bool
    wrapper_sha256: str
    launcher_path: str
    configuration_path: str


class RemotePlayOverride:
    def __init__(self, user_home: Path, runner: Path, logger=None, config_file: Path | None = None):
        self.user_home = user_home
        self.config_dir = user_home / '.config/mako-render'
        self.mako_script_path = runner
        self.config_file_path = config_file or self.config_dir / 'conf.toml'
        self._requested_runner = self.mako_script_path
        self._requested_configuration = self.config_file_path
        self.log = logger or logging.getLogger('MAKO Renderer')
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
        descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        with os.fdopen(descriptor, "rb") as stream:
            if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
                return False
            header = stream.read(20)
        return len(header) >= 20 and header[:6] == b"\x7fELF\x02\x01" and header[18:20] == b"\x3e\0"

    def _installed(self) -> bool:
        if self.client.is_symlink():
            return False
        try:
            descriptor = os.open(self.client, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
            with os.fdopen(descriptor, "rb") as stream:
                header = stream.read(256) if stat.S_ISREG(os.fstat(stream.fileno()).st_mode) else b''
                return MARKER in header
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
                script = None
                if len(arguments) > 1 and not arguments[1].startswith(b"-"):
                    script = Path(os.fsdecode(arguments[1]))
                    if not script.is_absolute():
                        script = Path(os.readlink(proc / "cwd")) / script
                if executable in paths or (script is not None and str(script.resolve()) in paths):
                    pids.append(int(proc.name))
            except (OSError, ValueError):
                continue
        return sorted(pids)

    def get_status(self, contexts: Sequence[RuntimeContextState] = ()) -> RemotePlayResponse:
        try:
            installed = self._installed()
            managed = installed or self.state.exists()
            pids = self.running_pids() if managed else []
            profile_name = ""
            if installed:
                try:
                    state = self._read_state(update_paths=False)
                    profile_name = selected_profile(Path(state["configuration_path"]))["name"]
                except (OSError, ValueError, TypeError, UnicodeError):
                    pass
                for pid in pids:
                    try:
                        executable = os.readlink(self.proc_root / str(pid) / "exe").removesuffix(" (deleted)")
                        if executable != str(self.backup.resolve()):
                            continue
                        environment = (self.proc_root / str(pid) / "environ").read_bytes().split(b"\0")
                        launched = next((entry[len(b"MAKO_PROFILE="):].decode() for entry in environment
                                         if entry.startswith(b"MAKO_PROFILE=")), "")
                        follows_selection = b"MAKO_FOLLOW_CURRENT_PROFILE=1" in environment
                        if launched and not (follows_selection and profile_name):
                            profile_name = launched
                            break
                    except (OSError, ValueError, UnicodeError):
                        continue
            conflict = not installed and (self.backup.exists() or self.checksum.exists())
            return RemotePlayResponse(
                success=True, error=None,
                message=("Steam client changed or an earlier override backup exists. "
                         "Close any streams, remove the Remote Play override, then enable it again.") if conflict else "",
                installed=installed, managed=managed,
                available=installed or self._native_client(self.client),
                running=bool(pids), pids=pids, profile_name=profile_name,
                frame_generation_active=installed and any(
                    context["pid"] in pids and context["phase"] == "active" and
                    context["frame_generation_active"] for context in contexts
                ), conflict=conflict,
            )
        except (OSError, ValueError) as error:
            return RemotePlayResponse(
                success=False, error=str(error), message=str(error),
                installed=False, managed=False, available=False, running=False,
                pids=[], profile_name="",
                frame_generation_active=False, conflict=False,
            )

    @contextmanager
    def mutation(self) -> Iterator[None]:
        """Fail promptly on a launch/mutation lease or a running native client."""
        self.config_dir.mkdir(parents=True, exist_ok=True)
        descriptor = os.open(self.lock_path, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600)
        with os.fdopen(descriptor, "r+") as lock:
            # Decky may run as root; the Steam user's self-contained wrapper reads this lock.
            identity = os.fstat(lock.fileno())
            if not stat.S_ISREG(identity.st_mode) or identity.st_nlink != 1:
                raise ValueError("Remote Play launch lock is not a private regular file")
            owner = self.user_home.stat()
            os.fchown(lock.fileno(), owner.st_uid, owner.st_gid)
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as error:
                raise ValueError("Remote Play launch or another override change is in progress") from error
            if self.running_pids():
                raise ValueError("Close native Remote Play before changing its override")
            yield

    def _read_state(self, *, update_paths: bool = True) -> RemotePlayOverrideState:
        raw = json.loads(self._read_record(self.state))
        if not isinstance(raw, dict) or type(raw.get("version")) is not int or raw.get("version") != 3 or raw.get("enabled") is not True:
            raise ValueError("Invalid Remote Play override state")
        digest = raw.get("wrapper_sha256")
        if not isinstance(digest, str) or re.fullmatch(r"[0-9a-f]{64}", digest) is None:
            raise ValueError("Invalid Remote Play wrapper checksum")
        stored_runner = raw.get('launcher_path')
        if not isinstance(stored_runner, str):
            raise ValueError('Invalid Remote Play launcher path')
        runner = Path(stored_runner)
        if not runner.is_absolute() or runner.name not in ('mako-run', 'mako-launch'):
            raise ValueError('Invalid Remote Play launcher path')
        configuration = raw.get('configuration_path')
        if not isinstance(configuration, str) or not Path(configuration).is_absolute():
            raise ValueError('Invalid Remote Play configuration path')
        if update_paths:
            self.config_file_path = Path(configuration)
            self.mako_script_path = runner
        return RemotePlayOverrideState(version=raw['version'], enabled=True,
                                       wrapper_sha256=digest, launcher_path=str(runner), configuration_path=configuration)

    @staticmethod
    def _read_record(path: Path) -> str:
        descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        with os.fdopen(descriptor, "rb") as stream:
            if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
                raise ValueError("Remote Play record is not a regular file")
            content = stream.read(4097)
            if len(content) > 4096:
                raise ValueError("Remote Play record is too large")
            return content.decode("utf-8")

    def _validate_backup(self, *, for_removal: bool = False) -> RemotePlayOverrideState:
        if not self._native_client(self.backup):
            raise ValueError("Original Steam client backup is unavailable or invalid")
        identity = self.backup.stat()
        if identity.st_uid != self.user_home.stat().st_uid or identity.st_mode & (stat.S_ISUID | stat.S_ISGID):
            raise ValueError("Original Steam client backup has unexpected ownership or privileges")
        if self._digest(self.backup) != self._read_record(self.checksum).strip():
            raise ValueError("Original Steam client backup checksum mismatch")
        try:
            return self._read_state()
        except (OSError, ValueError):
            # A lost sidecar must not strand an exactly known managed wrapper.
            # Unfamiliar/altered payloads still require manual recovery.
            if not for_removal:
                raise
            saved_runner = self.mako_script_path
            for runner in dict.fromkeys((saved_runner, self.user_home / '.local/bin/mako-run',
                                         self.user_home / '.local/bin/mako-launch')):
                self.mako_script_path = runner
                expected = hashlib.sha256(self._payload().encode()).hexdigest()
                if self._digest(self.client) == expected:
                    return RemotePlayOverrideState(version=3, enabled=True,
                                                   wrapper_sha256=expected, launcher_path=str(runner),
                                                   configuration_path=str(self.config_file_path))
            self.mako_script_path = saved_runner
            raise

    def _payload(self) -> str:
        return Path(__file__).with_name("remote_play_launch.py").read_text().replace(
            "home = Path('__MAKO_USER_HOME__')", f"home = Path({str(self.user_home)!r})",
        ).replace("runner=Path('__MAKO_RUNNER_PATH__')", f"runner=Path({str(self.mako_script_path)!r})").replace(
            "config=Path('__MAKO_CONFIG_PATH__')", f"config=Path({str(self.config_file_path)!r})").replace(
            "sdr_helper=Path('__MAKO_SDR_HELPER_PATH__')",
            f"sdr_helper=Path({str(self.mako_script_path.parent.parent / SDR_HELPER_RELATIVE_PATH)!r})")

    @contextmanager
    def transaction(self, extra_paths: Sequence[Path] = ()) -> Iterator[None]:
        """Rollback MAKO writes without restoring over a foreign Steam update."""
        expected = {hashlib.sha256(self._payload().encode()).hexdigest()}
        for path in (self.client, self.backup):
            if path.is_file() and not path.is_symlink():
                expected.add(self._digest(path))
        def owned(path: Path) -> bool:
            if path != self.client:
                return True
            try:
                return self._digest(path) in expected
            except (OSError, ValueError):
                return False
        paths = [self.client, self.backup, self.checksum, self.state,
                 self.config_file_path.parent / 'vkbasalt/current-profile.conf', *extra_paths]
        with managed_install_transaction(paths, self.log, rollback_guard=owned):
            yield

    @staticmethod
    def _check_interpreter() -> None:
        try:
            result = subprocess.run(
                ["/usr/bin/python3", "-I", "-c", "import fcntl, hashlib, tomllib; assert hasattr(hashlib, 'file_digest')"],
                capture_output=True, timeout=5, check=False,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise ValueError("Remote Play requires a working system Python 3.11 or newer") from error
        if result.returncode:
            raise ValueError("Remote Play requires a working system Python 3.11 or newer")

    def install_locked(self) -> None:
        """Called under mutation(), after the canonical profile is prepared."""
        installed = self._installed()
        if not installed:
            self.mako_script_path = self._requested_runner
            self.config_file_path = self._requested_configuration
        if not self.mako_script_path.is_absolute() or self.mako_script_path.name not in ('mako-run', 'mako-launch'):
            raise ValueError('Remote Play requires an absolute MAKO launcher path')
        if not self.config_file_path.is_absolute():
            raise ValueError('Remote Play requires an absolute configuration path')
        if installed:
            state = self._validate_backup()
            if self._digest(self.client) != state["wrapper_sha256"]:
                raise ValueError("Steam client wrapper changed; refusing replacement")
            self._check_launcher()
            payload = self._payload()
            if hashlib.sha256(payload.encode()).hexdigest() != state["wrapper_sha256"]:
                self._check_interpreter()
                compile(payload, "streaming_client", "exec")
                with self.transaction():
                    self._commit_wrapper(payload, state["wrapper_sha256"], self.backup.stat())
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
        self._check_launcher()
        self._check_interpreter()
        payload = self._payload()
        compile(payload, "streaming_client", "exec")
        original_hash = self._digest(self.client, original_stat.st_uid)
        with self.transaction():
            # Keep a privileged copy private until its bytes have been verified.
            copy_managed_file_atomically(self.client, self.backup, 0o600, self.log)
            if self._digest(self.backup) != original_hash or self._digest(self.client, original_stat.st_uid) != original_hash:
                raise ValueError("Steam updated during override installation")
            os.chown(self.backup, original_stat.st_uid, original_stat.st_gid, follow_symlinks=False)
            os.chmod(self.backup, stat.S_IMODE(original_stat.st_mode), follow_symlinks=False)
            owner = (original_stat.st_uid, original_stat.st_gid)
            write_managed_text_atomically(self.checksum, original_hash + "\n", 0o600, self.log, owner=owner)
            self._commit_wrapper(payload, original_hash, original_stat)

    def _check_launcher(self) -> None:
        if not self.mako_script_path.is_file() or not os.access(self.mako_script_path, os.X_OK):
            raise ValueError("Install MAKO Renderer first")
        try:
            check_sdr_helper(self.mako_script_path.parent.parent / SDR_HELPER_RELATIVE_PATH)
        except (OSError, ValueError) as error:
            raise ValueError("Install the current MAKO Renderer Remote Play SDR helper") from error
        if self.mako_script_path.name == 'mako-run':
            descriptor = os.open(self.mako_script_path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
            with os.fdopen(descriptor, 'rb') as stream:
                if stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
                    marker = b'MAKO_LAUNCH_RENDERER_REQUIRED'
                    tail = b''
                    while chunk := stream.read(65536):
                        content = tail + chunk
                        if marker in content:
                            return
                        tail = content[-len(marker):]
                raise ValueError("Update MAKO Decky before enabling Remote Play, or restore the override and enable it from Qt.")

    def _commit_wrapper(self, payload: str, previous_hash: str, permissions: os.stat_result) -> None:
        profile = selected_profile(self.config_file_path)
        default_shader = Path(os.environ.get('XDG_CONFIG_HOME') or self.user_home / '.config') / 'vkBasalt/vkBasalt.conf'
        settings = launch_settings(profile, self.config_file_path, default_shader=default_shader)
        content = 'effects = none\n'
        if settings['MAKO_LAUNCH_VKBASALT_CONFIG']:
            source = Path(settings['MAKO_LAUNCH_VKBASALT_CONFIG'])
            if source.exists() or source.is_symlink():
                with open_regular(source) as stream:
                    content = stream.read().decode('utf-8')
        write_managed_text_atomically(
            self.config_file_path.parent / 'vkbasalt/current-profile.conf', content, 0o644,
            self.log, owner=(permissions.st_uid, permissions.st_gid))
        state = RemotePlayOverrideState(
            version=3, enabled=True,
            wrapper_sha256=hashlib.sha256(payload.encode()).hexdigest(),
            launcher_path=str(self.mako_script_path),
            configuration_path=str(self.config_file_path),
        )
        write_managed_text_atomically(self.state, json.dumps(state) + "\n", 0o600, self.log, owner=(permissions.st_uid, permissions.st_gid))
        # Commit readable recovery dependencies before exposing the wrapper.
        descriptor = os.open(self.backup, os.O_RDONLY | os.O_NOFOLLOW)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
        sync_managed_directory(self.client.parent)
        sync_managed_directory(self.config_dir)
        def unchanged() -> bool:
            return not self.running_pids() and self._digest(self.client, permissions.st_uid) == previous_hash
        write_managed_text_atomically(self.client, payload, stat.S_IMODE(permissions.st_mode), self.log,
                                     owner=(permissions.st_uid, permissions.st_gid), replace_guard=unchanged)
        sync_managed_directory(self.client.parent)

    def install(self) -> None:
        with self.mutation():
            self.install_locked()

    def remove(self) -> None:
        with self.mutation():
            if not self.backup.exists() and not self.backup.is_symlink() and self._native_client(self.client):
                # Restoration already moved the original, or Steam replaced it.
                # Never replace that working native client just to clear state.
                self._read_state()
                with self.transaction():
                    self.checksum.unlink(missing_ok=True)
                    sync_managed_directory(self.client.parent)
                    self.state.unlink()
                    sync_managed_directory(self.config_dir)
                return
            state = self._validate_backup(for_removal=True)
            installed = self._installed()
            if self.client.is_symlink() or not self.client.is_file():
                raise ValueError("Steam client entry is missing or a symlink; manual recovery required")
            if installed and self._digest(self.client) != state["wrapper_sha256"]:
                raise ValueError("Steam client wrapper changed; refusing to overwrite it")
            if not installed and not self._native_client(self.client):
                raise ValueError("Steam client entry changed unexpectedly; manual recovery required")
            with self.transaction():
                if installed and self._digest(self.client) == state["wrapper_sha256"]:
                    os.replace(self.backup, self.client)
                elif self._native_client(self.client):
                    # Steam already replaced the entry point. Retire our stale
                    # backup; never restore it over the updated Steam binary.
                    self.backup.unlink()
                else:
                    raise ValueError("Steam client changed during restoration")
                sync_managed_directory(self.client.parent)
                self.checksum.unlink()
                sync_managed_directory(self.client.parent)
                self.state.unlink(missing_ok=True)
                sync_managed_directory(self.config_dir)

    def refresh_installed_payload(self) -> bool:
        """Upgrade only an already enabled, intact override; never install implicitly."""
        if not self._installed():
            return False
        self._read_state()
        if self._digest(self.client) == hashlib.sha256(self._payload().encode()).hexdigest():
            return False
        self.install()
        return True

    def restore_before_uninstall(self) -> None:
        if self._installed() or self.state.exists():
            self.remove()
