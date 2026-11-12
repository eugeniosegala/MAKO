"""Test-recording requests and status for the running game's Renderer."""

import asyncio
import json
import os
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import Mock, patch

from py_modules.mako_plugin import frame_recording

# An Xbox 360 pad's key capabilities (BTN_A to BTN_THUMBR) and a keyboard's.
GAMEPAD_KEYS = "7cdb000000000000 0 0 0 0\n"
KEYBOARD_KEYS = "1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe\n"


def pressed(*codes):
    keys = bytearray(96)
    for code in codes:
        keys[code // 8] |= 1 << (code % 8)
    return bytes(keys)


class _Done(Exception):
    pass


class FrameRecordingTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.home = Path(self.folder.name)
        self.root = frame_recording.recordings_root(self.home)

    def tearDown(self):
        self.folder.cleanup()

    def arm(self, age=0.0):
        self.root.mkdir(parents=True, exist_ok=True)
        marker = self.root / "armed.4242"
        marker.write_text("process=game.exe\nmode=ring\nseconds=10\ndelay=5\nbudget_bytes=8589934592\n")
        stamp = time.time() - age
        os.utime(marker, (stamp, stamp))
        return marker

    def test_requests_need_an_armed_running_game(self):
        result = frame_recording.request_recording(self.home)
        self.assertFalse(result["success"])
        self.assertIn("MAKO_RECORD_FRAMES=1", result["error"])
        self.assertFalse((self.root / "request").exists())

    def test_request_is_written_atomically_with_its_time(self):
        self.arm()
        before = time.time()
        self.assertTrue(frame_recording.request_recording(self.home)["success"])
        text = (self.root / "request").read_text()
        self.assertTrue(text.startswith("requested=") and before - 1 <= float(text[10:]) <= time.time() + 1)
        self.assertEqual(sorted(p.name for p in self.root.iterdir()), ["armed.4242", "request"])
        status = frame_recording.recording_status(self.home)
        self.assertTrue(status["pending"])
        self.assertEqual(status["armed"], [{"process": "game.exe", "mode": "ring", "seconds": 10.0, "delay": 5.0}])

    def test_markers_go_quiet_then_are_removed(self):
        marker = self.arm(age=10)
        self.assertEqual(frame_recording.armed_games(self.root), [])
        self.assertTrue(marker.exists())
        os.utime(marker, (time.time() - 120, time.time() - 120))
        self.assertEqual(frame_recording.armed_games(self.root), [])
        self.assertFalse(marker.exists())

    def test_status_reports_the_newest_recordings_and_renderer_line(self):
        for name, frames in (("20261026-100000-game", 3), ("20261026-110000-game", 5)):
            directory = self.root / name
            directory.mkdir(parents=True)
            (directory / "recording.json").write_text(json.dumps(dict(
                width=1280, height=720, end_reason="complete", frames=[{}] * frames, dropped=[7])))
        (self.root / "20261026-120000-game").mkdir()
        (self.root / "last-status").write_text("recording 20261026-120000-game\n")
        status = frame_recording.recording_status(self.home)
        self.assertEqual(status["status"], "recording 20261026-120000-game")
        self.assertFalse(status["pending"])
        self.assertEqual([r["name"] for r in status["recordings"]],
                         ["20261026-120000-game", "20261026-110000-game", "20261026-100000-game"])
        self.assertFalse(status["recordings"][0]["complete"])
        self.assertEqual((status["recordings"][1]["frames"], status["recordings"][1]["dropped"]), (5, 1))

    def test_missing_directory_is_an_empty_status(self):
        status = frame_recording.recording_status(self.home)
        self.assertEqual((status["success"], status["recordings"], status["status"]), (True, [], ""))


class FrameRecordingShortcutTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.home = Path(self.folder.name)
        self.root = frame_recording.recordings_root(self.home)
        self.root.mkdir(parents=True)

    def tearDown(self):
        self.folder.cleanup()

    def arm(self, mode):
        (self.root / "armed.4242").write_text(f"process=game.exe\nmode={mode}\nseconds=10\ndelay=5\n")

    def test_finds_only_devices_with_both_stick_buttons(self):
        sysfs = self.home / "sys"
        for name, keys in (("event5", KEYBOARD_KEYS), ("event20", GAMEPAD_KEYS), ("event21", None)):
            (sysfs / name / "device/capabilities").mkdir(parents=True)
            if keys:
                (sysfs / name / "device/capabilities/key").write_text(keys)
        self.assertEqual(frame_recording.gamepad_devices(sysfs, Path("/dev/input")), ["/dev/input/event20"])

    def test_both_sticks_must_be_pressed(self):
        self.assertTrue(frame_recording.sticks_pressed(pressed(frame_recording.BTN_THUMBL, frame_recording.BTN_THUMBR)))
        self.assertFalse(frame_recording.sticks_pressed(pressed(frame_recording.BTN_THUMBL)))
        self.assertFalse(frame_recording.sticks_pressed(pressed(frame_recording.BTN_THUMBR, 0x130)))

    def watch(self, states, **patches):
        """Run the watcher over successive pressed-key reads; return how often it saved."""
        presses = []
        reads = iter(states)

        async def save():
            presses.append(True)

        def read(_descriptor):
            try:
                return next(reads)
            except StopIteration:
                raise _Done from None

        # os.close is patched process-wide, and Python 3.14 closes a pidfd probe
        # while building an event loop, so build the loop before patching.
        with asyncio.Runner() as runner:
            runner.get_loop()
            with patch.object(frame_recording, "SHORTCUT_POLL_SECONDS", 0), \
                    patch.object(frame_recording, "read_pressed_keys", read), \
                    patch.object(frame_recording, "gamepad_devices", patches.get("devices", Mock(return_value=["/dev/input/event20"]))), \
                    patch.object(frame_recording.os, "open", patches.get("open", Mock(return_value=99))), \
                    patch.object(frame_recording.os, "close", patches.get("close", Mock())):
                with self.assertRaises(_Done):
                    runner.run(frame_recording.watch_shortcut(self.home, save))
        return len(presses)

    def test_both_sticks_request_once_per_press_in_either_mode(self):
        both = pressed(frame_recording.BTN_THUMBL, frame_recording.BTN_THUMBR)
        left = pressed(frame_recording.BTN_THUMBL)
        for mode in ("ring", "burst"):
            with self.subTest(mode=mode):
                self.arm(mode)
                close = Mock()
                self.assertEqual(self.watch([left, both, both, left, both, pressed()], close=close), 2)
                close.assert_called_once_with(99)

    def test_gamepads_are_not_read_without_an_armed_game(self):
        devices, opened = Mock(), Mock()
        checks = []
        real = frame_recording.armed_games

        def armed(root):
            checks.append(root)
            if len(checks) == 3:
                raise _Done
            return real(root)

        with patch.object(frame_recording, "SHORTCUT_IDLE_SECONDS", 0), \
                patch.object(frame_recording, "armed_games", armed), \
                patch.object(frame_recording, "gamepad_devices", devices), \
                patch.object(frame_recording.os, "open", opened):
            with self.assertRaises(_Done):
                asyncio.run(frame_recording.watch_shortcut(self.home, Mock()))
        devices.assert_not_called()
        opened.assert_not_called()

    def test_an_unreadable_gamepad_is_reported_once_and_skipped(self):
        self.arm("ring")
        messages = []
        checks = []
        real = frame_recording.armed_games

        def armed(root):
            checks.append(root)
            if len(checks) == 3:
                raise _Done
            return real(root)

        with patch.object(frame_recording, "SHORTCUT_IDLE_SECONDS", 0), \
                patch.object(frame_recording, "armed_games", armed), \
                patch.object(frame_recording, "gamepad_devices", Mock(return_value=["/dev/input/event20"])), \
                patch.object(frame_recording.os, "open", Mock(side_effect=PermissionError("denied"))):
            with self.assertRaises(_Done):
                asyncio.run(frame_recording.watch_shortcut(self.home, Mock(), messages.append))
        self.assertEqual(messages, ["MAKO Decky: cannot read gamepad /dev/input/event20: denied"])


if __name__ == "__main__":
    unittest.main()
