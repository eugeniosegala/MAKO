"""Test recordings of the real frames a running game presents.

Only a game launched with ``MAKO_RECORD_FRAMES=1`` (burst) or
``MAKO_RECORD_FRAMES=ring`` (rolling) in its launch options records. Its MAKO
Renderer writes an ``armed.<pid>`` marker under
``~/.local/share/mako-render/captures`` and polls for a ``request``. In burst
mode a fresh request pauses frame generation and records a few seconds after a
delay; in ring mode the Renderer keeps the last seconds of real frames in memory
while frame generation stays on, and a request saves them. Pressing both sticks
(L3 + R3) in game is the only way to request one. Recordings feed offline
frame-generation evaluation in MAKO FG.
"""

import asyncio
import fcntl
import json
import os
import struct
import time
from pathlib import Path
from typing import Any, Awaitable, Callable, Dict, List

RECORDINGS_DIR = ".local/share/mako-render/captures"
LAUNCH_OPTION = "MAKO_RECORD_FRAMES=1"


def recordings_root(home: Path) -> Path:
    return home / RECORDINGS_DIR


# The Renderer refreshes its marker every 2 s while the game presents frames.
MARKER_LIVE_SECONDS = 5
MARKER_STALE_SECONDS = 60


def armed_games(root: Path, now: float | None = None) -> List[Dict[str, Any]]:
    """Running games whose launch armed recording; long-stale markers are removed."""
    now = time.time() if now is None else now
    games = []
    for marker in sorted(root.glob("armed.*")) if root.is_dir() else []:
        age = now - marker.stat().st_mtime
        if age > MARKER_STALE_SECONDS:
            marker.unlink(missing_ok=True)
        if age > MARKER_LIVE_SECONDS:
            continue
        fields = dict(line.split("=", 1) for line in marker.read_text().splitlines() if "=" in line)
        games.append({"process": fields.get("process", ""), "mode": fields.get("mode", "burst"),
                      "seconds": float(fields.get("seconds", 3)), "delay": float(fields.get("delay", 5))})
    return games


def _write_request(root: Path) -> None:
    staged = root / f"request.{os.getpid()}.tmp"
    staged.write_text(f"requested={time.time():.3f}\n")
    os.replace(staged, root / "request")


def request_recording(home: Path) -> Dict[str, Any]:
    """Atomically ask an armed running game for one recording."""
    root = recordings_root(home)
    try:
        if not armed_games(root):
            return {"success": False, "error": f"Recording is not armed; add {LAUNCH_OPTION} to the launch options"}
        _write_request(root)
    except OSError as error:
        return {"success": False, "error": str(error)}
    return {"success": True, "directory": str(root)}


def recording_status(home: Path, limit: int = 3) -> Dict[str, Any]:
    """The renderer's last status line and the newest recordings."""
    root = recordings_root(home)
    status_path = root / "last-status"
    try:
        status = status_path.read_text().strip() if status_path.is_file() else ""
        directories = sorted((d for d in root.iterdir() if d.is_dir()), reverse=True) if root.is_dir() else []
    except OSError as error:
        return {"success": False, "error": str(error)}
    recordings = []
    for directory in directories[:limit]:
        index = directory / "recording.json"
        entry: Dict[str, Any] = {"name": directory.name, "complete": index.is_file()}
        if index.is_file():
            try:
                data = json.loads(index.read_text())
                entry.update(frames=len(data.get("frames", [])), dropped=len(data.get("dropped", [])),
                             width=data.get("width"), height=data.get("height"),
                             end_reason=data.get("end_reason"))
            except (OSError, ValueError):
                entry["complete"] = False
        recordings.append(entry)
    try:
        armed = armed_games(root)
    except OSError:
        armed = []
    return {"success": True, "pending": (root / "request").exists(), "status": status,
            "armed": armed, "launch_option": LAUNCH_OPTION,
            "recordings": recordings, "directory": str(root)}


# Pressing both sticks requests a recording without Steam's screenshot chord. Steam Input presents every controller to games as a virtual gamepad
# (created through uinput), and udev lets the logged-in user read gamepads, so
# reading the pressed buttons works for any controller without root.
SHORTCUT_EVENT = "frame_recording_shortcut"
BTN_THUMBL = 0x13D
BTN_THUMBR = 0x13E
_KEY_BYTES = 96  # KEY_MAX + 1 bits
_EVIOCGKEY = (2 << 30) | (_KEY_BYTES << 16) | (ord("E") << 8) | 0x18
_LONG_BITS = struct.calcsize("l") * 8
# Gamepads are read only while an armed game runs: its marker is checked every
# 2 s, and its gamepads' pressed buttons 20 times a second.
SHORTCUT_IDLE_SECONDS = 2.0
SHORTCUT_POLL_SECONDS = 0.05


def capability_has_keys(text: str, *codes: int) -> bool:
    """Whether a sysfs ``capabilities/key`` bitmap (hex longs, most significant first) has every code."""
    value = 0
    for word in text.split():
        value = (value << _LONG_BITS) | int(word, 16)
    return all(value >> code & 1 for code in codes)


def gamepad_devices(input_root: Path = Path("/sys/class/input"),
                    device_root: Path = Path("/dev/input")) -> List[str]:
    """Event devices with both stick buttons."""
    devices = []
    for entry in sorted(input_root.glob("event*")):
        try:
            keys = (entry / "device/capabilities/key").read_text()
        except OSError:
            continue
        if capability_has_keys(keys, BTN_THUMBL, BTN_THUMBR):
            devices.append(str(device_root / entry.name))
    return devices


def sticks_pressed(keys: bytes) -> bool:
    return all(keys[code // 8] >> (code % 8) & 1 for code in (BTN_THUMBL, BTN_THUMBR))


def read_pressed_keys(descriptor: int) -> bytes:
    keys = bytearray(_KEY_BYTES)
    fcntl.ioctl(descriptor, _EVIOCGKEY, keys)
    return bytes(keys)


async def watch_shortcut(home: Path, pressed: Callable[[], Awaitable[None]],
                         log: Callable[[str], None] = lambda _message: None) -> None:
    """Await ``pressed`` each time both sticks go down together while an armed game runs."""
    root = recordings_root(home)
    devices: Dict[str, int] = {}
    unreadable: set[str] = set()
    held = False
    next_scan = 0.0
    try:
        while True:
            now = time.monotonic()
            if now >= next_scan:
                next_scan = now + SHORTCUT_IDLE_SECONDS
                try:
                    wanted = gamepad_devices() if armed_games(root) else []
                except (OSError, ValueError):
                    wanted = []
                for path in [path for path in devices if path not in wanted]:
                    os.close(devices.pop(path))
                for path in wanted:
                    if path in devices:
                        continue
                    try:
                        devices[path] = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_CLOEXEC)
                        log(f"MAKO Decky: L3 + R3 on {path} requests a test recording")
                    except OSError as error:
                        if path not in unreadable:
                            unreadable.add(path)
                            log(f"MAKO Decky: cannot read gamepad {path}: {error}")
            if not devices:
                held = False
                await asyncio.sleep(max(next_scan - time.monotonic(), 0.0))
                continue
            both = False
            for path, descriptor in list(devices.items()):
                try:
                    both = both or sticks_pressed(read_pressed_keys(descriptor))
                except OSError:
                    os.close(devices.pop(path))
            if both and not held:
                await pressed()
            held = both
            await asyncio.sleep(SHORTCUT_POLL_SECONDS)
    finally:
        for descriptor in devices.values():
            os.close(descriptor)
