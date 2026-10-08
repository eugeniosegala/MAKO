"""Discover game processes without inspecting unrelated applications."""

from pathlib import Path
import os
import re
from typing import Dict, Iterable, Optional

from .constants import SCRIPT_NAME, STEAM_APP_ID_ENV_KEYS
from .launcher_exclusions_generated import EXCLUDED_WINDOWS_LAUNCHERS

_HELPER_PROCESS_NAMES = {
    "audio.crutility",
    "bash",
    "bwrap",
    "comet",
    "conhost.exe",
    "crbrowsermain",
    "crgpumain",
    "crrenderermain",
    "explorer.exe",
    "flatpak",
    "gamemoderun",
    "gameoverlayui",
    "gamescope",
    "heroic-run",
    Path(SCRIPT_NAME).name,
    "network.crutili",
    "ntoskrnl.exe",
    "plugplay.exe",
    "pressure-vessel-wrap",
    "proton",
    "pv-adverb",
    "pv-bwrap",
    "reaper",
    "rpcss.exe",
    "services.exe",
    "sh",
    "srt-bwrap",
    "steam",
    "steam.exe",
    "steam-runtime-launch-client",
    "steamwebhelper",
    "storage.crutili",
    "svchost.exe",
    "tabtip.exe",
    "video_capture.c",
    "winedevice.exe",
    "wine",
    "wine64",
    "wineboot.exe",
    "winemenubuilder.exe",
    "wine-preloader",
    "wine64-preloader",
    "wineserver",
    "xalia.exe",
    "xdg-dbus-proxy",
} | EXCLUDED_WINDOWS_LAUNCHERS | {
    name[:15] for name in EXCLUDED_WINDOWS_LAUNCHERS
}

_WINDOWS_EXECUTABLE = re.compile(r"([^/\\\s\x00]+\.exe)(?:\x00|\s|$)", re.IGNORECASE)
_PYTHON_HELPER = re.compile(r"python(?:\d+(?:\.\d+)*)?", re.IGNORECASE)


def _read_environment(path: Path) -> Dict[str, str]:
    values: Dict[str, str] = {}
    for item in path.read_bytes().split(b"\0"):
        if b"=" not in item:
            continue
        raw_key, raw_value = item.split(b"=", 1)
        key = raw_key.decode("utf-8", errors="ignore")
        values[key] = raw_value.decode("utf-8", errors="ignore")
    return values


def _clean_candidate(value: str) -> Optional[str]:
    candidate = value.strip().strip('"\'').replace("\\", "/").rsplit("/", 1)[-1]
    lowered = candidate.lower()
    if (
        not candidate
        or lowered in _HELPER_PROCESS_NAMES
        or _PYTHON_HELPER.fullmatch(lowered)
    ):
        return None
    if lowered.startswith(("pressure-vessel", "steam-runtime-")):
        return None
    if candidate.startswith(".") or lowered.endswith((".so", ".dll")):
        return None
    return candidate


def is_matchable_process_name(value: str) -> bool:
    """Return whether a process identity is specific enough for a profile."""
    return _clean_candidate(str(value)) is not None


def _candidate_names(process_dir: Path) -> Iterable[str]:
    try:
        executable = os.readlink(process_dir / "exe")
    except OSError:
        executable = ""

    try:
        mapped_content = (process_dir / "maps").read_text(
            encoding="utf-8", errors="ignore"
        )
    except OSError:
        mapped_content = ""

    identity = executable
    if "wine" in executable or "proton" in executable:
        for line in mapped_content.splitlines():
            if not line.lower().endswith(".exe"):
                continue
            mapped_executable = _WINDOWS_EXECUTABLE.search(line)
            if mapped_executable:
                identity = mapped_executable.group(1)
                break
    # Match the Renderer's executable authority before considering mutable
    # thread names or a future game's executable in launcher arguments.
    if identity.replace("\\", "/").rsplit("/", 1)[-1].lower() in EXCLUDED_WINDOWS_LAUNCHERS:
        return

    candidate = _clean_candidate(executable)
    if candidate:
        yield candidate

    try:
        candidate = _clean_candidate(
            (process_dir / "comm").read_text(encoding="utf-8", errors="ignore")
        )
        if candidate:
            yield candidate
    except OSError:
        pass

    # Proton's Linux process is normally a Wine loader. Its mapped Windows
    # executable and command line contain the identity used by the renderer's
    # own active_in matcher.
    try:
        command_line = (process_dir / "cmdline").read_bytes().decode(
            "utf-8", errors="ignore"
        ).replace("\0", " ")
    except OSError:
        command_line = ""
    for content in (command_line, mapped_content):
        for match in _WINDOWS_EXECUTABLE.finditer(content):
            candidate = _clean_candidate(match.group(1))
            if candidate:
                yield candidate


def detect_processes_for_steam_app(
    app_id: str,
    proc_root: Path = Path("/proc"),
) -> list[str]:
    """Return executable names belonging to one running Steam application.

    The environment gate is intentional: the scanner never collects process
    names from a different game, Decky, Steam, or another plugin.
    """
    normalized_app_id = str(app_id).strip()
    if not normalized_app_id:
        return []

    candidates: set[str] = set()
    try:
        process_dirs = list(proc_root.iterdir())
    except OSError:
        return []

    for process_dir in process_dirs:
        if not process_dir.name.isdigit() or not process_dir.is_dir():
            continue
        try:
            environment = _read_environment(process_dir / "environ")
        except (OSError, ValueError):
            continue
        if normalized_app_id not in {
            environment.get(key, "").strip() for key in STEAM_APP_ID_ENV_KEYS
        }:
            continue
        candidates.update(_candidate_names(process_dir))

    return sorted(candidates, key=str.casefold)
