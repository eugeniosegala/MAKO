#!/usr/bin/env python3
"""Portable safety tests for the game-scoped Gamescope VRR lease."""

from importlib.machinery import SourceFileLoader
from importlib.util import module_from_spec, spec_from_loader
from pathlib import Path
import io
import fcntl
import json
import os
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch


source = Path(__file__).with_name("mako-vrr-lease")
loader = SourceFileLoader("mako_vrr_lease", str(source))
spec = spec_from_loader(loader.name, loader)
module = module_from_spec(spec)
loader.exec_module(module)


class VrrLeaseTests(unittest.TestCase):
    def test_namespace_publisher_requires_live_matching_kernel_lock(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "game-frame-generation-1.json"
            pid = os.getpid()
            tick = module.process_identity(pid)[1]
            with path.with_name(path.name + ".lock").open("w") as lock:
                self.assertIsNone(module.namespace_publisher(path, pid, tick, module.lock_owners()))
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                owners = module.lock_owners()
                self.assertEqual(module.namespace_publisher(path, pid, tick, owners), pid)
                self.assertIsNone(module.namespace_publisher(path, pid + 1, tick, owners))
                self.assertIsNone(module.namespace_publisher(path, pid, tick + 1, owners))
                fcntl.flock(lock, fcntl.LOCK_UN)
                self.assertIsNone(module.namespace_publisher(path, pid, tick, module.lock_owners()))

    def test_namespace_translation_retains_launch_and_profile_authorization(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            pid = os.getpid()
            tick = module.process_identity(pid)[1]
            record = {"pid": 99999999, "process_start_ticks": tick, "requested": {
                "name": "Game", "gamescope_vrr_mode": "off",
                "frame_generation_provisioned": True, "frame_generation_enabled": True}}
            (path / "game-frame-generation-1.json").write_text(json.dumps(record))
            with patch.object(module, "namespace_publisher", return_value=pid):
                self.assertEqual(module.publishers(path), [module.Publisher(pid, tick, "off")])
                self.assertEqual(module.active_mode(path, pid, tick, "unknown", set()), "off")
                self.assertIsNone(module.active_mode(path, pid, tick + 1, "unknown", set()))

    def test_untrusted_status_symlink_and_oversize_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "record.json"
            path.write_text("{}")
            link = Path(temporary) / "link.json"
            link.symlink_to(path)
            with self.assertRaises(OSError):
                module.read_status(link)
            path.write_text(" " * 65537)
            with self.assertRaises(ValueError):
                module.read_status(path)
            fifo = Path(temporary) / "fifo.json"
            os.mkfifo(fifo)
            with self.assertRaises(ValueError):
                module.read_status(fifo)

    def test_flatpak_session_matches_compositor_and_filters_game_environment(self):
        from unittest.mock import mock_open
        import struct
        payload = (b"DISPLAY=:1\0GAMESCOPE_WAYLAND_DISPLAY=gamescope-0\0"
                   b"WAYLAND_DISPLAY=gamescope-0\0PATH=/bad\0LD_PRELOAD=/bad\0"
                   b"HOME=/bad\0MAKO_CONFIG=/bad\0")
        peer = Mock()
        peer.getsockopt.return_value = struct.pack("3i", 101, os.geteuid(), 0)
        connection = Mock()
        connection.__enter__ = Mock(return_value=peer)
        connection.__exit__ = Mock(return_value=False)
        with patch.object(Path, "is_file", return_value=True), \
             patch.object(Path, "stat", return_value=SimpleNamespace(st_uid=os.geteuid())), \
             patch.object(Path, "open", mock_open(read_data=payload)), \
             patch.object(module.socket, "socket", return_value=connection), \
             patch.object(module, "xroot_properties", return_value={"GAMESCOPE_PID": 101}) as root, \
             patch.object(module, "process_identity", return_value=(1, 42)), \
             patch.object(module, "config_path", return_value=Path("/trusted/conf.toml")):
            environment = module.flatpak_environment(20, 42)
            self.assertEqual(environment["PATH"], "/usr/bin:/bin")
            self.assertEqual(environment["MAKO_CONFIG"], "/trusted/conf.toml")
            self.assertNotEqual(environment["HOME"], "/bad")
            self.assertNotIn("LD_PRELOAD", environment)
            root.return_value = {"GAMESCOPE_PID": 102}
            self.assertIsNone(module.flatpak_environment(20, 42))
            root.return_value = {"GAMESCOPE_PID": 101}
            self.assertIsNone(module.flatpak_environment(20, 43))

    def test_host_monitor_reuses_lease_and_retries_failed_service(self):
        with patch.object(module, "STOP", False), \
             patch.object(module.signal, "signal"), \
             patch.object(module, "runtime_lock", return_value=None), \
             patch.object(module, "publishers", return_value=[module.Publisher(20, 42, "off")]), \
             patch.object(module, "flatpak_environment", return_value={"PATH": "/usr/bin:/bin"}), \
             patch.object(module.time, "monotonic", side_effect=[0, 0, 1, 30, 30]), \
             patch.object(module.time, "sleep") as sleep, \
             patch.object(module.subprocess, "run", side_effect=[
                 SimpleNamespace(returncode=0), SimpleNamespace(returncode=3),
                 SimpleNamespace(returncode=0)]) as run:
            def stop_after_three(_seconds):
                if sleep.call_count == 3:
                    module.STOP = True
            sleep.side_effect = stop_after_three
            self.assertEqual(module.watch_flatpak(), 0)
            self.assertEqual(run.call_count, 3)
            self.assertEqual(run.call_args.args[0][-3:], ("--start", "20", "20-42-0"))

    def test_follow_steam_does_not_start_a_flatpak_lease(self):
        with patch.object(module, "STOP", False), \
             patch.object(module.signal, "signal"), \
             patch.object(module, "runtime_lock", return_value=None), \
             patch.object(module, "publishers", return_value=[module.Publisher(20, 42, "follow-steam")]), \
             patch.object(module, "flatpak_environment") as session, \
             patch.object(module.time, "sleep", side_effect=lambda _: setattr(module, "STOP", True)), \
             patch.object(module.subprocess, "run") as run:
            self.assertEqual(module.watch_flatpak(), 0)
            session.assert_not_called()
            run.assert_not_called()

    def test_standalone_watcher_is_not_bound_to_its_shell(self):
        environment = {key: value for key, value in os.environ.items()
                       if key != module.WATCH_OWNER_ENV}
        with patch.dict(os.environ, environment, clear=True), patch("ctypes.CDLL") as libc:
            self.assertIsNone(module.watch_owner())
            libc.assert_not_called()

    def test_owned_watcher_arms_parent_death_signal(self):
        with patch.dict(os.environ, {module.WATCH_OWNER_ENV: "1234"}), \
             patch("ctypes.CDLL") as libc:
            self.assertEqual(module.watch_owner(), 1234)
            libc.return_value.prctl.assert_called_once_with(
                module.PR_SET_PDEATHSIG, int(signal.SIGTERM), 0, 0, 0)

    def test_watcher_whose_owner_already_died_never_discovers(self):
        # Our own PID is never our parent, as after an owner dies before arming.
        with patch.dict(os.environ, {module.WATCH_OWNER_ENV: str(os.getpid())}), \
             patch("ctypes.CDLL"), \
             patch.object(module, "STOP", False), \
             patch.object(module.signal, "signal"), \
             patch.object(module, "runtime_lock", return_value=None), \
             patch.object(module, "publishers") as discovered, \
             patch.object(module.time, "sleep") as sleep:
            self.assertEqual(module.watch_flatpak(), 0)
            discovered.assert_not_called()
            sleep.assert_not_called()

    def test_second_watcher_waits_for_the_discovery_lock(self):
        with tempfile.TemporaryDirectory() as temporary:
            holder = os.open(Path(temporary) / "mako-vrr-flatpak-watch.lock",
                             os.O_CREAT | os.O_RDWR, 0o600)
            fcntl.flock(holder, fcntl.LOCK_EX)

            def release_then_stop(_seconds):
                if sleep.call_count == 1:
                    os.close(holder)
                else:
                    module.STOP = True

            with patch.dict(os.environ, {"XDG_RUNTIME_DIR": temporary}), \
                 patch.object(module, "STOP", False), \
                 patch.object(module.signal, "signal"), \
                 patch.object(module, "watch_owner", return_value=None), \
                 patch.object(module, "publishers", return_value=[]) as discovered, \
                 patch.object(module.time, "sleep", side_effect=release_then_stop) as sleep:
                self.assertEqual(module.watch_flatpak(), 0)
            self.assertEqual(sleep.call_count, 2)
            discovered.assert_called_once()

    @unittest.skipUnless(sys.platform.startswith("linux"), "parent-death signals are Linux-only")
    def test_owned_watcher_exits_after_its_backend_is_killed(self):
        with tempfile.TemporaryDirectory() as temporary:
            (Path(temporary) / "runtime-state").mkdir()
            lock = Path(temporary) / "mako-vrr-flatpak-watch.lock"
            # Stand in for a Decky backend that SIGKILL ends before _unload completes.
            backend = subprocess.Popen((
                sys.executable, "-c",
                "import os, subprocess, sys, time\n"
                f"env = dict(os.environ, {module.WATCH_OWNER_ENV}=str(os.getpid()))\n"
                "watcher = subprocess.Popen((sys.executable, sys.argv[1], '--watch-flatpak'), env=env,\n"
                "                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)\n"
                "print(watcher.pid, flush=True)\n"
                "time.sleep(60)\n",
                str(source),
            ), stdout=subprocess.PIPE, text=True, env={
                "PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": temporary,
                "XDG_RUNTIME_DIR": temporary, "MAKO_CONFIG": str(Path(temporary) / "conf.toml"),
            })
            watcher = None
            try:
                watcher = int(backend.stdout.readline())
                deadline = time.monotonic() + 10
                while not lock.exists() or self._lock_is_free(lock):
                    self.assertLess(time.monotonic(), deadline, "watcher never started discovery")
                    time.sleep(0.05)
                backend.kill()
                backend.wait()
                deadline = time.monotonic() + 5
                while self._running(watcher):
                    self.assertLess(time.monotonic(), deadline, "orphaned watcher kept running")
                    time.sleep(0.05)
                self.assertTrue(self._lock_is_free(lock))
            finally:
                if backend.poll() is None:
                    backend.kill()
                    backend.wait()
                backend.stdout.close()
                if watcher is not None and self._running(watcher) and str(source).encode() in \
                        Path(f"/proc/{watcher}/cmdline").read_bytes():
                    os.kill(watcher, signal.SIGKILL)  # Never leave a failed run's orphan behind.

    @staticmethod
    def _lock_is_free(path: Path) -> bool:
        descriptor = os.open(path, os.O_RDWR)
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
            return True
        except BlockingIOError:
            return False
        finally:
            os.close(descriptor)

    @staticmethod
    def _running(pid: int) -> bool:
        try:
            state = Path(f"/proc/{pid}/stat").read_text().rsplit(") ", 1)[1].split()[0]
        except (OSError, IndexError):
            return False
        return state != "Z"  # An unreaped orphan has still exited.

    def test_decision_log_emits_only_on_state_changes(self):
        output = io.StringIO()
        with patch.object(module.sys, "stderr", output):
            decisions = module.DecisionLog()
            decisions.record("no-matched-frame-generation-context")
            decisions.record("no-matched-frame-generation-context")
            decisions.record("write-confirmed-off")
        self.assertEqual(output.getvalue().splitlines(), [
            "MAKO Renderer: Gamescope VRR lease decision=no-matched-frame-generation-context",
            "MAKO Renderer: Gamescope VRR lease decision=write-confirmed-off",
        ])

    def test_only_a_vrr_capable_active_display_allows_an_override(self):
        with patch.object(module.subprocess, "run", return_value=
                          subprocess.CompletedProcess([], 0,
                              " - Display Flags: 0x3\n", "")):
            self.assertFalse(module.read_vrr_capability())
        with patch.object(module.subprocess, "run", return_value=
                          subprocess.CompletedProcess([], 0,
                              " - Display Flags: 0x7\n", "")):
            self.assertTrue(module.read_vrr_capability())
        with patch.object(module.subprocess, "run", return_value=
                          subprocess.CompletedProcess([], 0,
                              "gamescope_control info unavailable\n", "")):
            self.assertIsNone(module.read_vrr_capability())

    def test_display_identity_changes_when_gamescope_socket_is_replaced(self):
        with patch.dict(os.environ, {
            "XDG_RUNTIME_DIR": "/run/user/1000",
            "GAMESCOPE_WAYLAND_DISPLAY": "gamescope-test",
        }), patch.object(Path, "stat", side_effect=[
            SimpleNamespace(st_mode=0o140777, st_dev=12, st_ino=34),
            SimpleNamespace(st_mode=0o140777, st_dev=12, st_ino=35),
        ]):
            original = module.display_identity()
            self.assertEqual(original, (12, 34))
            self.assertNotEqual(module.display_identity(), original)

    def test_gamescope_vrr_read_uses_the_verified_x_root(self):
        with patch.object(module, "read_xroot_vrr", return_value=(True, "ok")):
            self.assertEqual(module.read_vrr(), (True, "ok"))
        with patch.object(module, "read_xroot_vrr", return_value=(None, "x-display-unavailable")):
            self.assertEqual(module.read_vrr(), (None, "x-display-unavailable"))

    def test_xroot_readback_matches_the_gamescope_pid_and_root_server(self):
        properties = {
            ":2": {"GAMESCOPE_PID": 123, "GAMESCOPE_XWAYLAND_SERVER_ID": 2},
            ":0": {"GAMESCOPE_PID": 123, "GAMESCOPE_XWAYLAND_SERVER_ID": 0,
                   "GAMESCOPE_VRR_ENABLED": 0},
        }
        with patch.dict(os.environ, {"DISPLAY": ":2"}), \
             patch.object(module, "xroot_properties",
                          side_effect=lambda display: properties.get(display)):
            self.assertEqual(module.read_xroot_vrr(), (False, "ok"))
            properties[":0"]["GAMESCOPE_PID"] = 999
            self.assertEqual(module.read_xroot_vrr(), (None, "x-root-unverified"))

    def test_xroot_query_parses_only_cardinal_properties(self):
        output = ("GAMESCOPE_PID(CARDINAL) = 123\n"
                  "GAMESCOPE_XWAYLAND_SERVER_ID(CARDINAL) = 0\n"
                  "GAMESCOPE_VRR_ENABLED(CARDINAL) = 1\n")
        with patch.object(module.subprocess, "run", return_value=
                          subprocess.CompletedProcess([], 0, output, "")):
            self.assertEqual(module.xroot_properties(":0"), {
                "GAMESCOPE_PID": 123,
                "GAMESCOPE_XWAYLAND_SERVER_ID": 0,
                "GAMESCOPE_VRR_ENABLED": 1,
            })

    def test_verified_x_session_without_vrr_feedback_fails_closed(self):
        with patch.object(module, "read_xroot_vrr", return_value=(None, "x-vrr-unavailable")):
            self.assertEqual(module.read_vrr(), (None, "x-vrr-unavailable"))

    def test_gamescope_vrr_write_uses_the_verified_root_property(self):
        with patch.object(module, "XROOT_CACHE", (":2", 123, ":0")), \
             patch.object(module, "read_xroot_vrr", return_value=(True, "ok")), \
             patch.object(module, "read_vrr", return_value=(False, "ok")), \
             patch.object(module.subprocess, "run", return_value=
                          subprocess.CompletedProcess([], 0, "", "")) as run:
            self.assertTrue(module.write_vrr(False, True))
            self.assertEqual(run.call_args.args[0], (
                "xprop", "-display", ":0", "-root", "-f",
                "GAMESCOPE_VRR_ENABLED", "32c", "-set", "GAMESCOPE_VRR_ENABLED", "0",
            ))
        with patch.object(module, "read_xroot_vrr", return_value=(None, "x-root-unverified")), \
             patch.object(module.subprocess, "run") as run:
            self.assertFalse(module.write_vrr(False, True))
            run.assert_not_called()
        with patch.object(module, "XROOT_CACHE", (":2", 123, ":0")), \
             patch.object(module, "read_xroot_vrr", return_value=(False, "ok")), \
             patch.object(module.subprocess, "run") as run:
            self.assertFalse(module.write_vrr(False, True))
            run.assert_not_called()

    def test_service_starts_outside_game_scope_with_session_environment(self):
        with patch.dict(os.environ, {
            "GAMESCOPE_WAYLAND_DISPLAY": "gamescope-test",
            "WAYLAND_DISPLAY": "gamescope-test",
            "DISPLAY": ":1",
            "XAUTHORITY": "/tmp/gamescope-test-xauthority",
            "XDG_RUNTIME_DIR": "/run/user/1000",
            "MAKO_CONFIG": "/tmp/mako-game.toml",
            "MAKO_GAMESCOPE_COMPOSITE_FORCE": "1",
        }), patch.object(module.shutil, "which", return_value="/usr/bin/systemd-run"), \
             patch.object(Path, "stat", return_value=SimpleNamespace(
                 st_mode=0o140777, st_dev=12, st_ino=34)), \
             patch.object(module, "process_identity", return_value=(1, 9876)), \
             patch.object(module.subprocess, "run",
                          return_value=subprocess.CompletedProcess([], 0, "", "")) as run:
            self.assertEqual(module.start_service("123", "123-4-5"), 0)
            command = run.call_args.args[0]
            self.assertIn("--user", command)
            self.assertIn("--collect", command)
            self.assertIn("--setenv=GAMESCOPE_WAYLAND_DISPLAY=gamescope-test", command)
            self.assertIn("--setenv=DISPLAY=:1", command)
            self.assertIn("--setenv=XAUTHORITY=/tmp/gamescope-test-xauthority", command)
            self.assertIn("--setenv=MAKO_CONFIG=/tmp/mako-game.toml", command)
            self.assertIn("--setenv=MAKO_GAMESCOPE_COMPOSITE_FORCE=1", command)
            self.assertEqual(command[-5:], ["123", "123-4-5", "9876", "12", "34"])

    def test_composite_force_reads_a_missing_property_as_scanout(self):
        with patch.object(module, "verified_root_properties", return_value=(
                {"GAMESCOPE_PID": 1, "GAMESCOPE_XWAYLAND_SERVER_ID": 0}, "ok")):
            self.assertEqual(module.read_composite_force(), (False, "ok"))
        with patch.object(module, "verified_root_properties", return_value=(
                {"GAMESCOPE_COMPOSITE_FORCE": 1}, "ok")):
            self.assertEqual(module.read_composite_force(), (True, "ok"))
        with patch.object(module, "verified_root_properties",
                          return_value=(None, "x-root-unverified")):
            self.assertEqual(module.read_composite_force(), (None, "x-root-unverified"))

    def test_composite_force_write_uses_the_verified_root_property(self):
        with patch.object(module, "XROOT_CACHE", (":2", 123, ":0")), \
             patch.object(module, "read_composite_force",
                          side_effect=[(False, "ok"), (True, "ok")]), \
             patch.object(module.subprocess, "run", return_value=
                          subprocess.CompletedProcess([], 0, "", "")) as run:
            self.assertTrue(module.write_composite_force(True, False))
            self.assertEqual(run.call_args.args[0], (
                "xprop", "-display", ":0", "-root", "-f", "GAMESCOPE_COMPOSITE_FORCE",
                "32c", "-set", "GAMESCOPE_COMPOSITE_FORCE", "1",
            ))
        with patch.object(module, "XROOT_CACHE", (":2", 123, ":0")), \
             patch.object(module, "read_composite_force", return_value=(True, "ok")), \
             patch.object(module.subprocess, "run") as run:
            self.assertFalse(module.write_composite_force(True, False))
            run.assert_not_called()

    def test_composite_lease_holds_for_the_game_and_restores_scanout(self):
        state = {"value": False}
        clock = {"now": 0.0}

        def write(value, expected):
            self.assertEqual(state["value"], expected)
            state["value"] = value
            return True

        with patch.object(module, "read_composite_force",
                          side_effect=lambda: (state["value"], "ok")) as read, \
             patch.object(module, "write_composite_force", side_effect=write), \
             patch.object(module.time, "monotonic", side_effect=lambda: clock["now"]):
            lease = module.CompositeLease()
            self.assertEqual(lease.update(), "write-confirmed")
            self.assertTrue(state["value"])
            reads = read.call_count
            self.assertEqual(lease.update(), "holding")
            self.assertEqual(read.call_count, reads)
            clock["now"] += module.HOLD_CHECK_SECONDS
            self.assertEqual(lease.update(), "holding")
            self.assertTrue(lease.restore())
            self.assertFalse(state["value"])
            self.assertFalse(lease.applied)

    def test_composite_lease_never_takes_over_or_restores_an_external_value(self):
        state = {"value": True}
        clock = {"now": 0.0}
        with patch.object(module, "read_composite_force",
                          side_effect=lambda: (state["value"], "ok")), \
             patch.object(module, "write_composite_force") as write, \
             patch.object(module.time, "monotonic", side_effect=lambda: clock["now"]):
            lease = module.CompositeLease()
            self.assertEqual(lease.update(), "already-composited")
            self.assertEqual(lease.update(), "yielded")
            self.assertTrue(lease.restore())
            write.assert_not_called()
            self.assertTrue(state["value"])

        state["value"] = False
        with patch.object(module, "read_composite_force",
                          side_effect=lambda: (state["value"], "ok")), \
             patch.object(module, "write_composite_force",
                          side_effect=lambda value, expected: state.update(value=value) or True) as write, \
             patch.object(module.time, "monotonic", side_effect=lambda: clock["now"]):
            lease = module.CompositeLease()
            self.assertEqual(lease.update(), "write-confirmed")
            state["value"] = False  # The user or Steam turned composition off.
            clock["now"] += module.HOLD_CHECK_SECONDS
            self.assertEqual(lease.update(), "yielded-to-external-change")
            self.assertTrue(lease.restore())
            self.assertEqual(write.call_count, 1)
            self.assertFalse(state["value"])

    def test_composite_lease_keeps_trying_until_gamescope_is_readable(self):
        with patch.object(module, "read_composite_force",
                          return_value=(None, "x-root-unverified")), \
             patch.object(module, "write_composite_force") as write:
            lease = module.CompositeLease()
            self.assertEqual(lease.update(), "query-x-root-unverified")
            self.assertFalse(lease.yielded)
            write.assert_not_called()

    def test_restore_only_while_value_is_still_ours(self):
        state = {"value": True}
        clock = {"now": 0.0}

        def write(value, expected):
            self.assertEqual(state["value"], expected)
            state["value"] = value
            return True

        with patch.object(module, "read_vrr", side_effect=lambda: (state["value"], "ok")), \
             patch.object(module, "write_vrr", side_effect=write), \
             patch.object(module.time, "monotonic", side_effect=lambda: clock["now"]):
            lease = module.VrrLease()
            self.assertEqual(lease.update("off"), "write-confirmed-off")
            self.assertFalse(state["value"])
            self.assertEqual(lease.update("off"), "holding-off")
            lease.restore()
            self.assertTrue(state["value"])

            lease.update("off")
            state["value"] = True  # Steam changed the live session.
            clock["now"] += module.HOLD_CHECK_SECONDS
            self.assertEqual(lease.update("off"), "yielded-to-external-change")
            self.assertTrue(lease.yielded)
            lease.restore()
            self.assertTrue(state["value"])

    def test_stable_hold_skips_subprocess_reads_but_restoration_reads_fresh(self):
        state = {"value": True}
        clock = {"now": 0.0}

        def write(value, expected):
            self.assertEqual(state["value"], expected)
            state["value"] = value
            return True

        with patch.object(module, "read_vrr", side_effect=lambda: (state["value"], "ok")) as read, \
             patch.object(module, "write_vrr", side_effect=write), \
             patch.object(module.time, "monotonic", side_effect=lambda: clock["now"]):
            lease = module.VrrLease()
            self.assertEqual(lease.update("off"), "write-confirmed-off")
            for second in range(1, int(module.HOLD_CHECK_SECONDS)):
                clock["now"] = float(second)
                self.assertEqual(lease.update("off"), "holding-off")
            self.assertEqual(read.call_count, 1)
            self.assertTrue(lease.restore())
            self.assertEqual(read.call_count, 2)
            self.assertTrue(state["value"])

    def test_stable_hold_detects_external_change_at_next_check(self):
        state = {"value": True}
        clock = {"now": 0.0}

        def write(value, expected):
            self.assertEqual(state["value"], expected)
            state["value"] = value
            return True

        with patch.object(module, "read_vrr", side_effect=lambda: (state["value"], "ok")) as read, \
             patch.object(module, "write_vrr", side_effect=write), \
             patch.object(module.time, "monotonic", side_effect=lambda: clock["now"]):
            lease = module.VrrLease()
            self.assertEqual(lease.update("off"), "write-confirmed-off")
            state["value"] = True  # Steam's live choice takes effect immediately.
            clock["now"] = module.HOLD_CHECK_SECONDS - 1
            self.assertEqual(lease.update("off"), "holding-off")
            self.assertEqual(read.call_count, 1)
            clock["now"] = module.HOLD_CHECK_SECONDS
            self.assertEqual(lease.update("off"), "yielded-to-external-change")
            self.assertTrue(lease.yielded)
            self.assertEqual(read.call_count, 2)
            self.assertTrue(lease.restore())
            self.assertTrue(state["value"])

    def test_noop_and_failures_have_distinct_decisions(self):
        with patch.object(module, "read_vrr", return_value=(False, "ok")), \
             patch.object(module, "write_vrr") as write:
            lease = module.VrrLease()
            self.assertEqual(lease.update("off"), "already-off")
            write.assert_not_called()

        with patch.object(module, "read_vrr", return_value=(None, "x-display-unavailable")):
            self.assertEqual(module.VrrLease().update("on"), "query-x-display-unavailable")

        with patch.object(module, "read_vrr", return_value=(True, "ok")), \
             patch.object(module, "write_vrr", return_value=False):
            lease = module.VrrLease()
            self.assertEqual(lease.update("off"), "write-not-verified")
            self.assertTrue(lease.baseline)
            self.assertFalse(lease.applied)

    def test_failed_restore_keeps_the_original_value_for_retry(self):
        with patch.object(module, "read_vrr", side_effect=[(True, "ok"), (None, "x-display-unavailable"), (False, "ok")]), \
             patch.object(module, "write_vrr", return_value=True) as write:
            lease = module.VrrLease()
            lease.update("off")
            self.assertFalse(lease.restore())
            self.assertEqual(lease.applied, False)
            self.assertTrue(lease.restore())
            self.assertIsNone(lease.applied)
            self.assertEqual(write.call_args_list[-1].args, (True, False))

    def test_exit_restoration_retries_transient_failure(self):
        lease = SimpleNamespace(restore=Mock(side_effect=[False, True]))
        with patch.object(module, "display_identity", return_value=(12, 34)), \
             patch.object(module.time, "sleep") as sleep:
            self.assertTrue(module.restore_on_exit(
                lease, module.DecisionLog(), (12, 34)))
        self.assertEqual(lease.restore.call_count, 2)
        sleep.assert_called_once_with(module.POLL_SECONDS)

    def test_exit_restoration_stops_after_bounded_failures(self):
        lease = SimpleNamespace(restore=Mock(return_value=False))
        output = io.StringIO()
        with patch.object(module, "display_identity", return_value=(12, 34)), \
             patch.object(module.time, "sleep") as sleep, \
             patch.object(module.sys, "stderr", output):
            self.assertFalse(module.restore_on_exit(
                lease, module.DecisionLog(), (12, 34)))
        self.assertEqual(lease.restore.call_count, module.RESTORE_ATTEMPTS)
        self.assertEqual(sleep.call_count, module.RESTORE_ATTEMPTS - 1)
        self.assertIn("decision=restore-unverified-at-exit", output.getvalue())

    def test_exit_restoration_skips_replacement_session(self):
        lease = SimpleNamespace(restore=Mock())
        with patch.object(module, "display_identity", return_value=(12, 35)):
            self.assertTrue(module.restore_on_exit(
                lease, module.DecisionLog(), (12, 34)))
        lease.restore.assert_not_called()

    def test_unverified_write_keeps_the_baseline_for_restoration(self):
        with patch.object(module, "read_vrr", side_effect=[(True, "ok"), (False, "ok")]), \
             patch.object(module, "write_vrr", side_effect=[False, True]) as write:
            lease = module.VrrLease()
            self.assertEqual(lease.update("off"), "write-not-verified")
            self.assertTrue(lease.restore())
            self.assertEqual(write.call_args_list[-1].args, (True, False))

    def test_delayed_readback_does_not_yield_or_lose_restoration(self):
        with patch.object(module, "read_vrr", side_effect=[
                (True, "ok"), (True, "ok"), (False, "ok"), (False, "ok")]), \
             patch.object(module, "write_vrr", side_effect=[False, True]) as write:
            lease = module.VrrLease()
            self.assertEqual(lease.update("off"), "write-not-verified")
            self.assertEqual(lease.update("off"), "awaiting-write-readback")
            self.assertFalse(lease.yielded)
            self.assertEqual(lease.update("off"), "write-confirmed-off")
            self.assertTrue(lease.restore())
            self.assertEqual(write.call_args_list[-1].args, (True, False))

    def test_live_profile_change_rearms_after_steam_change(self):
        state = {"value": True}
        clock = {"now": 0.0}

        def write(value, expected):
            self.assertEqual(state["value"], expected)
            state["value"] = value
            return True

        with patch.object(module, "read_vrr", side_effect=lambda: (state["value"], "ok")), \
             patch.object(module, "write_vrr", side_effect=write), \
             patch.object(module.time, "monotonic", side_effect=lambda: clock["now"]):
            lease = module.VrrLease()
            lease.update("off")
            state["value"] = True
            clock["now"] += module.HOLD_CHECK_SECONDS
            lease.update("off")
            lease.update("on")
            self.assertFalse(lease.yielded)
            lease.update("off")
            self.assertFalse(state["value"])

    def test_only_live_status_from_this_launch_selects_profile(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            status_dir = root / "runtime-state"
            status_dir.mkdir()
            pid = os.getpid()
            tick = module.process_identity(pid)[1]
            status = {"pid": pid, "process_start_ticks": tick,
                      "requested": {"name": "Game", "gamescope_vrr_mode": "off",
                                    "frame_generation_provisioned": True,
                                    "frame_generation_enabled": True}}
            (status_dir / f"{pid}-{tick}-frame-generation-1.json").write_text(json.dumps(status))
            observed = set()
            self.assertEqual(
                module.active_mode(status_dir, pid, tick, "123-456", observed),
                "off",
            )
            status["requested"]["frame_generation_enabled"] = False
            (status_dir / f"{pid}-{tick}-frame-generation-1.json").write_text(json.dumps(status))
            self.assertEqual(
                module.active_mode(status_dir, pid, tick, "123-456", observed),
                "follow-steam",
            )
            status["process_start_ticks"] += 1
            (status_dir / f"{pid}-{tick}-frame-generation-1.json").write_text(json.dumps(status))
            self.assertIsNone(module.active_mode(status_dir, pid, tick, "123-456", observed))


if __name__ == "__main__":
    unittest.main()
