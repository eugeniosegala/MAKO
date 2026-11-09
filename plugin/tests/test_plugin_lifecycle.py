"""Regression tests for the Decky plugin lifecycle."""

import asyncio
import fcntl
import os
import signal
import subprocess
import sys
import time
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock, Mock, patch
from pathlib import Path
import tempfile


class _Logger:
    def __getattr__(self, _name):
        return lambda *_args, **_kwargs: None


sys.modules.setdefault("decky", SimpleNamespace(logger=_Logger()))

from py_modules.mako_plugin.plugin import Plugin  # noqa: E402


class PluginLifecycleTests(unittest.TestCase):
    def test_main_runs_every_current_startup_maintenance_task(self):
        calls = []
        plugin = Plugin.__new__(Plugin)
        plugin._start_flatpak_vrr_monitor = AsyncMock()
        plugin._start_frame_recording_shortcut = AsyncMock()
        plugin.configuration_service = SimpleNamespace(
            enforce_unsupported_host_passthrough_if_needed=lambda: calls.append(
                "host-passthrough"
            ) or False,
            migrate_profile_metadata_if_needed=lambda: calls.append(
                "profile-metadata"
            ) or False,
            migrate_launch_script_if_needed=lambda: calls.append(
                "launch-script"
            ) or False,
        )
        plugin.installation_service = SimpleNamespace(
            current_package_host_compatibility=lambda: (
                "x86_64", True, None
            ),
            prepare_active_standalone_for_decky=lambda: calls.append(
                "standalone-adoption"
            ) or False,
            ensure_native_uninstaller_if_needed=lambda: False,
            migrate_gamescope_wsi_compatibility_manifest_if_needed=lambda: calls.append(
                "gamescope-wsi-manifest"
            ) or False,
            refresh_guarded_postprocess_manifests_if_needed=lambda: calls.append(
                "postprocess-manifests"
            ) or False,
            migrate_diagnostics_helper_if_needed=lambda: calls.append(
                "diagnostics-helper"
            ) or False,
        )
        plugin.flatpak_service = SimpleNamespace(
            disable_incompatible_host_overrides=lambda: calls.append(
                "flatpak-host-boundary"
            ) or {"success": True, "disabled_apps": []},
        )

        plugin.remote_play_service = SimpleNamespace(
            refresh_installed_payload=lambda: calls.append("remote-play-payload") or False,
        )
        async def inline(function, *arguments):
            return function(*arguments)
        with patch("py_modules.mako_plugin.plugin.asyncio.to_thread", inline):
            asyncio.run(plugin._main())
        plugin._start_flatpak_vrr_monitor.assert_awaited_once()
        plugin._start_frame_recording_shortcut.assert_awaited_once()

        self.assertEqual(calls, [
            "profile-metadata",
            "launch-script",
            "standalone-adoption",
            "gamescope-wsi-manifest",
            "postprocess-manifests",
            "diagnostics-helper",
            "remote-play-payload",
        ])

    def test_main_stops_before_migrations_on_unsupported_host(self):
        calls = []
        plugin = Plugin.__new__(Plugin)
        plugin._start_flatpak_vrr_monitor = AsyncMock()
        plugin._start_frame_recording_shortcut = AsyncMock()
        plugin.configuration_service = SimpleNamespace(
            enforce_unsupported_host_passthrough_if_needed=lambda: calls.append(
                "host-passthrough"
            ) or True,
            migrate_profile_metadata_if_needed=lambda: calls.append(
                "profile-metadata"
            ),
        )
        plugin.installation_service = SimpleNamespace(
            current_package_host_compatibility=lambda: (
                "aarch64", False, "unsupported"
            ),
        )
        plugin.flatpak_service = SimpleNamespace(
            disable_incompatible_host_overrides=lambda: calls.append(
                "flatpak-host-boundary"
            ) or {
                "success": True,
                "disabled_apps": ["org.example.Game"],
            },
        )

        asyncio.run(plugin._main())
        plugin._start_flatpak_vrr_monitor.assert_not_called()
        plugin._start_frame_recording_shortcut.assert_not_called()

        self.assertEqual(calls, [
            "host-passthrough",
            "flatpak-host-boundary",
        ])

    def test_monitor_lifecycle_is_idempotent_and_does_not_kill_game_leases(self):
        async def exercise():
            with tempfile.TemporaryDirectory() as temporary:
                home = Path(temporary)
                helper = home / "mako-vrr-lease"
                helper.touch()
                plugin = Plugin.__new__(Plugin)
                plugin.installation_service = SimpleNamespace(
                    user_home=home,
                    selected_vrr_lease_file=lambda: helper,
                )
                plugin.configuration_service = SimpleNamespace(config_file_path=home / "conf.toml")
                process = Mock(returncode=None, wait=AsyncMock(return_value=0))
                with patch("asyncio.create_subprocess_exec", AsyncMock(return_value=process)) as spawn:
                    await plugin._start_flatpak_vrr_monitor()
                    await plugin._start_flatpak_vrr_monitor()
                    spawn.assert_awaited_once()
                    self.assertEqual(spawn.call_args.args, (str(helper), "--watch-flatpak"))
                    self.assertEqual(spawn.call_args.kwargs["env"]["HOME"], str(home))
                    self.assertEqual(
                        spawn.call_args.kwargs["env"]["MAKO_VRR_WATCH_PARENT"], str(os.getpid())
                    )
                    await plugin._unload()
                    await plugin._stop_flatpak_vrr_monitor()
                    process.kill.assert_called_once()
                    process.terminate.assert_not_called()
        asyncio.run(exercise())

    def test_unresponsive_monitor_stops_before_loader_shutdown_deadline(self):
        async def exercise(shutdown):
            process = await asyncio.create_subprocess_exec(
                sys.executable, "-u", "-c",
                "import signal,time; "
                "signal.signal(signal.SIGTERM, signal.SIG_IGN); "
                "print('ready'); time.sleep(30)",
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.DEVNULL,
            )
            plugin = Plugin.__new__(Plugin)
            plugin._flatpak_vrr_monitor = process
            plugin.remote_play_service = SimpleNamespace(
                restore_before_uninstall=Mock(),
            )
            plugin.installation_service = SimpleNamespace(
                cleanup_on_uninstall=Mock(),
            )
            try:
                self.assertEqual(await process.stdout.readline(), b"ready\n")
                # Leave margin inside Decky's observed five-second deadline.
                await asyncio.wait_for(getattr(plugin, shutdown)(), timeout=4.5)
                self.assertEqual(await asyncio.wait_for(process.wait(), timeout=1), -signal.SIGKILL)
                self.assertIsNone(plugin._flatpak_vrr_monitor)
                if shutdown == "_uninstall":
                    plugin.remote_play_service.restore_before_uninstall.assert_not_called()
                    plugin.installation_service.cleanup_on_uninstall.assert_called_once()
            finally:
                if process.returncode is None:
                    process.kill()
                    await process.wait()

        for shutdown in ("_unload", "_uninstall"):
            with self.subTest(shutdown=shutdown):
                asyncio.run(exercise(shutdown))

    def test_shortcut_watch_starts_once_and_stops_on_shutdown(self):
        async def exercise(shutdown):
            plugin = Plugin.__new__(Plugin)
            plugin._stop_flatpak_vrr_monitor = AsyncMock()
            plugin.remote_play_service = SimpleNamespace(restore_before_uninstall=Mock())
            plugin.installation_service = SimpleNamespace(cleanup_on_uninstall=Mock())
            watching = asyncio.Event()

            async def watch(*_arguments):
                watching.set()
                await asyncio.Event().wait()

            with patch("py_modules.mako_plugin.plugin.frame_recording.watch_shortcut", Mock(side_effect=watch)) as start:
                await plugin._start_frame_recording_shortcut()
                await plugin._start_frame_recording_shortcut()
                await watching.wait()
                task = plugin._frame_recording_shortcut
                await getattr(plugin, shutdown)()
                # The cancelled watch finishes on the next loop turn, if Loader grants one.
                await asyncio.sleep(0)
            start.assert_called_once()
            self.assertTrue(task.cancelled())
            self.assertIsNone(plugin._frame_recording_shortcut)

        for shutdown in ("_unload", "_uninstall"):
            with self.subTest(shutdown=shutdown):
                asyncio.run(exercise(shutdown))

    def test_monitor_reap_timeout_is_bounded_and_prevents_duplicate_discovery(self):
        async def exercise():
            async def never_reaped():
                await asyncio.Event().wait()

            process = Mock(returncode=None, wait=AsyncMock(side_effect=never_reaped))
            plugin = Plugin.__new__(Plugin)
            plugin._flatpak_vrr_monitor = process
            with patch("py_modules.mako_plugin.plugin.decky.logger") as logger:
                await asyncio.wait_for(plugin._stop_flatpak_vrr_monitor(), timeout=4.5)
                logger.warning.assert_called()
            process.terminate.assert_called_once()
            process.kill.assert_called_once()
            self.assertIs(plugin._flatpak_vrr_monitor, process)
            with patch("asyncio.create_subprocess_exec", AsyncMock()) as spawn:
                await plugin._start_flatpak_vrr_monitor()
                spawn.assert_not_awaited()
            process.returncode = -signal.SIGKILL
            await plugin._stop_flatpak_vrr_monitor()
            self.assertIsNone(plugin._flatpak_vrr_monitor)

        asyncio.run(exercise())

    def test_monitor_exit_racing_with_termination_is_reaped(self):
        async def exercise():
            process = Mock(returncode=None, wait=AsyncMock(return_value=0))
            process.terminate.side_effect = ProcessLookupError
            plugin = Plugin.__new__(Plugin)
            plugin._flatpak_vrr_monitor = process
            await plugin._stop_flatpak_vrr_monitor()
            process.wait.assert_awaited_once()
            process.kill.assert_not_called()
            self.assertIsNone(plugin._flatpak_vrr_monitor)

        asyncio.run(exercise())

    def test_monitor_stop_preserves_a_replacement_started_after_exit(self):
        async def exercise():
            waiting = asyncio.Event()
            reaped = asyncio.Event()

            async def wait():
                waiting.set()
                await reaped.wait()
                return 0

            process = Mock(returncode=None, wait=AsyncMock(side_effect=wait))
            replacement = Mock(returncode=None)
            plugin = Plugin.__new__(Plugin)
            plugin._flatpak_vrr_monitor = process
            stopping = asyncio.create_task(plugin._stop_flatpak_vrr_monitor())
            await waiting.wait()
            process.returncode = 0
            plugin._flatpak_vrr_monitor = replacement
            reaped.set()
            await stopping
            self.assertIs(plugin._flatpak_vrr_monitor, replacement)
            replacement.terminate.assert_not_called()
            replacement.kill.assert_not_called()

        asyncio.run(exercise())

    def test_loader_shutdown_hooks_finish_without_suspending(self):
        # Decky Loader 3.3.1 closes the plugin socket right after SIGTERM; its
        # listener then starves the event loop, so a suspended hook never resumes.
        async def exercise(shutdown):
            async def never_reaped():
                await asyncio.Event().wait()

            process = Mock(returncode=None, wait=AsyncMock(side_effect=never_reaped))
            plugin = Plugin.__new__(Plugin)
            plugin._flatpak_vrr_monitor = process
            shortcut = asyncio.create_task(never_reaped())
            plugin._frame_recording_shortcut = shortcut
            plugin.remote_play_service = SimpleNamespace(restore_before_uninstall=Mock())
            # Real file work, so offloading it to a thread would have to suspend.
            cleanup = Mock(side_effect=lambda: time.sleep(0.05))
            plugin.installation_service = SimpleNamespace(cleanup_on_uninstall=cleanup)
            hook = getattr(plugin, shutdown)()
            try:
                hook.send(None)  # Any await that yields here would never resume.
            except StopIteration:
                pass
            else:
                hook.close()
                self.fail(f"{shutdown} suspended before finishing")
            process.kill.assert_called_once()
            self.assertIsNone(plugin._flatpak_vrr_monitor)
            await asyncio.sleep(0)
            self.assertTrue(shortcut.cancelled())
            self.assertIsNone(plugin._frame_recording_shortcut)
            plugin.remote_play_service.restore_before_uninstall.assert_not_called()
            self.assertEqual(cleanup.called, shutdown == "_uninstall")

        for shutdown in ("_unload", "_uninstall"):
            with self.subTest(shutdown=shutdown):
                asyncio.run(exercise(shutdown))

    @unittest.skipUnless(sys.platform.startswith("linux"), "parent-death signals are Linux-only")
    def test_loader_stop_sequence_completes_unload_and_uninstall(self):
        for mode in ("unload", "uninstall"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as temporary:
                home = Path(temporary)
                backend, watcher = self._start_decky_backend(home, mode)
                try:
                    backend.send_signal(signal.SIGTERM)
                    # A hook that suspends leaves the backend spinning until this times out.
                    self.assertEqual(backend.wait(timeout=3), 0)
                    self.assertFalse((home / "restored").exists())
                    self.assertEqual((home / "cleaned").exists(), mode == "uninstall")
                    self._assert_exits(watcher)
                finally:
                    _stop_backend(backend, watcher)

    @unittest.skipUnless(sys.platform.startswith("linux"), "parent-death signals are Linux-only")
    def test_discovery_exits_when_decky_kills_the_backend(self):
        with tempfile.TemporaryDirectory() as temporary:
            backend, watcher = self._start_decky_backend(Path(temporary), "unload")
            try:
                # Loader's SIGKILL leaves no chance for _unload to end the monitor.
                backend.kill()
                backend.wait()
                self._assert_exits(watcher)
            finally:
                _stop_backend(backend, watcher)

    def _start_decky_backend(self, home: Path, mode: str) -> tuple[subprocess.Popen, int]:
        """Run the real Plugin and helper under Decky Loader 3.3.1's stop sequence."""
        lock = home / "mako-vrr-flatpak-watch.lock"
        # exec keeps the backend as the helper's parent while isolating its lock.
        (home / "mako-vrr-lease").write_text(
            f'#!/bin/sh\nXDG_RUNTIME_DIR="{home}" exec "{sys.executable}" "{HELPER}" "$@"\n'
        )
        (home / "mako-vrr-lease").chmod(0o755)
        log = (home / "backend.log").open("w")
        self.addCleanup(log.close)
        backend = subprocess.Popen(
            (sys.executable, "-c", DECKY_BACKEND, str(home), mode),
            cwd=Path(__file__).resolve().parents[1],
            stdout=subprocess.DEVNULL, stderr=log,
        )
        deadline = time.monotonic() + 10
        while not lock.exists() or _lock_is_free(lock):
            if backend.poll() is not None or time.monotonic() > deadline:
                _stop_backend(backend, None)
                self.fail((home / "backend.log").read_text() or "discovery never started")
            time.sleep(0.05)
        return backend, int((home / "watcher.pid").read_text())

    def _assert_exits(self, watcher: int) -> None:
        deadline = time.monotonic() + 5
        while _running(watcher):
            self.assertLess(time.monotonic(), deadline, "orphaned discovery kept running")
            time.sleep(0.05)


HELPER = Path(__file__).resolve().parents[2] / "engine/scripts/mako-vrr-lease"

# Mirrors decky_loader.plugin.sandboxed_plugin 3.3.1: SIGTERM schedules shutdown(),
# then the socket listener spins on EOF and never yields the event loop again.
DECKY_BACKEND = """\
import asyncio, signal, sys
from pathlib import Path
from types import SimpleNamespace
quiet = lambda *_: None
sys.modules['decky'] = SimpleNamespace(logger=SimpleNamespace(info=quiet, warning=quiet, error=quiet))
from py_modules.mako_plugin.plugin import Plugin
home, mode = Path(sys.argv[1]), sys.argv[2]
mark = lambda name: lambda: (home / name).touch()
launcher = home / 'mako-vrr-lease'
plugin = Plugin.__new__(Plugin)
plugin.installation_service = SimpleNamespace(
    user_home=home, vrr_lease_file=launcher, selected_vrr_lease_file=lambda: launcher,
    cleanup_on_uninstall=mark('cleaned'))
plugin.configuration_service = SimpleNamespace(config_file_path=home / 'conf.toml')
plugin.remote_play_service = SimpleNamespace(restore_before_uninstall=mark('restored'))

async def shutdown():
    await plugin._unload()
    if mode == 'uninstall':
        await plugin._uninstall()
    loop.call_soon_threadsafe(loop.stop)
    sys.exit(0)

def starve():
    while True:
        pass

def terminate():
    asyncio.ensure_future(shutdown())
    loop.call_soon(starve)

async def main():
    await plugin._start_flatpak_vrr_monitor()
    plugin._frame_recording_shortcut = asyncio.ensure_future(asyncio.Event().wait())
    (home / 'watcher.pid').write_text(str(plugin._flatpak_vrr_monitor.pid))

loop = asyncio.new_event_loop()
asyncio.set_event_loop(loop)
loop.add_signal_handler(signal.SIGTERM, terminate)
loop.create_task(main())
try:
    loop.run_forever()
except SystemExit:
    pass
finally:
    loop.close()
"""


def _stop_backend(backend: subprocess.Popen, watcher: int | None) -> None:
    if backend.poll() is None:
        backend.kill()
        backend.wait()
    if watcher is not None and _running(watcher) and str(HELPER).encode() in \
            Path(f"/proc/{watcher}/cmdline").read_bytes():
        os.kill(watcher, signal.SIGKILL)  # Never leave a failed run's orphan behind.


def _lock_is_free(path: Path) -> bool:
    descriptor = os.open(path, os.O_RDWR)
    try:
        fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        return True
    except BlockingIOError:
        return False
    finally:
        os.close(descriptor)


def _running(pid: int) -> bool:
    try:
        state = Path(f"/proc/{pid}/stat").read_text().rsplit(") ", 1)[1].split()[0]
    except (OSError, IndexError):
        return False
    return state != "Z"  # An unreaped orphan has still exited.


if __name__ == "__main__":
    unittest.main()
