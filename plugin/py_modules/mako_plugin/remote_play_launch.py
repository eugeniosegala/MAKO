#!/usr/bin/python3
# MAKO_NATIVE_REMOTE_PLAY_OVERRIDE_V1
"""Self-contained Steam entry point; installed independently of the plugin files."""

import fcntl
import hashlib
import json
import os
from pathlib import Path
import sys
import tomllib
from typing import Callable, Mapping, TypedDict


class LaunchPaths(TypedDict):
    original: Path
    checksum: Path
    state: Path
    config: Path
    runner: Path
    lock: Path


# Native streaming uses Vulkan. Never inherit a launcher's OpenGL/Zink override.
REMOVED_ENVIRONMENT = (
    "GALLIUM_DRIVER", "MESA_LOADER_DRIVER_OVERRIDE", "__GLX_VENDOR_LIBRARY_NAME",
    "ENABLE_MAKO", "MAKO_PROFILE", "MAKO_PROFILE_FALLBACK", "MAKO_CONFIG",
    "ENABLE_VKBASALT", "VKBASALT_CONFIG_FILE", "MAKO_PRESENT_DIAGNOSTICS",
    "ENABLE_MAKO_SPATIAL_SCALING", "DISABLE_MAKO_SPATIAL_SCALING",
)


def launch(paths: LaunchPaths, arguments: list[str], execute: Callable,
           environment: Mapping[str, str] | None = None, *,
           allow_override: bool = True) -> int:
    """Validate the original, then delegate all feature/power setup to mako-run."""
    clean = dict(os.environ if environment is None else environment)
    for key in REMOVED_ENVIRONMENT:
        clean.pop(key, None)
    clean["DISABLE_MAKO"] = "1"
    clean["DISABLE_MAKO_SPATIAL_SCALING"] = "1"
    if "VK_INSTANCE_LAYERS" in clean:
        clean["VK_INSTANCE_LAYERS"] = ":".join(
            layer for layer in clean["VK_INSTANCE_LAYERS"].split(":")
            if layer not in {"VK_LAYER_MAKO_render", "VK_LAYER_MAKO_spatial_scaling", "VK_LAYER_VKBASALT_post_processing"}
        )
    original = paths["original"]
    try:
        if original.is_symlink() or not original.is_file() or not os.access(original, os.X_OK):
            raise ValueError("original client unavailable")
        if paths["checksum"].is_symlink():
            raise ValueError("checksum is a symlink")
        with original.open("rb") as stream:
            header = stream.read(20)
            if len(header) < 20 or header[:6] != b"\x7fELF\x02\x01" or header[18:20] != b"\x3e\0":
                raise ValueError("original client is not native x86_64 ELF")
            stream.seek(0)
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        if digest != paths["checksum"].read_text().strip():
            raise ValueError("original client checksum mismatch")
    except (OSError, ValueError) as error:
        print(f"MAKO Decky: Remote Play original validation failed: {error}", file=sys.stderr)
        return 126

    try:
        if not allow_override:
            raise ValueError("override launch lock unavailable")
        state = json.loads(paths["state"].read_text())
        if not isinstance(state, dict) or state.get("version") != 1 or state.get("enabled") is not True:
            raise ValueError("override disabled")
        profile_name = state.get("profile")
        if not isinstance(profile_name, str) or not profile_name:
            raise ValueError("invalid profile identity")
        with paths["config"].open("rb") as stream:
            config = tomllib.load(stream)
        if config.get("version") != 2 or not any(
            isinstance(profile, dict) and profile.get("name") == profile_name
            for profile in config.get("profile", [])
        ):
            raise ValueError("Remote Play profile unavailable")
        if not paths["runner"].is_file() or not os.access(paths["runner"], os.X_OK):
            raise ValueError("mako-run unavailable")
        active = dict(clean)
        active.pop("DISABLE_MAKO", None)
        active.pop("DISABLE_MAKO_SPATIAL_SCALING", None)
        active["MAKO_PROFILE"] = profile_name
        # mako-run owns provisioning, DLL detection, power sets, shaders, and
        # disabled-feature passthrough. Do not maintain another configuration parser.
        command = [str(paths["runner"]), "/usr/bin/env",
                   "-u", "GALLIUM_DRIVER", "-u", "MESA_LOADER_DRIVER_OVERRIDE",
                   "-u", "__GLX_VENDOR_LIBRARY_NAME", str(original), *arguments]
        execute(command[0], command, active)
        return 0
    except (OSError, ValueError, TypeError, KeyError) as error:
        print(f"MAKO Decky: Remote Play passthrough: {error}", file=sys.stderr)
    try:
        execute(str(original), [str(original), *arguments], clean)
        return 0
    except OSError as error:
        print(f"MAKO Decky: Remote Play original launch failed: {error}", file=sys.stderr)
        return 126


def main() -> int:
    root = Path(__file__).resolve().parent
    home = Path('__MAKO_USER_HOME__')
    config_dir = home / ".config/mako-render"
    paths = LaunchPaths(
        original=root / "streaming_client.mako-original",
        checksum=root / "streaming_client.mako-original.sha256",
        state=config_dir / "native-remote-play.json",
        config=config_dir / "conf.toml",
        runner=home / ".local/bin/mako-run",
        lock=config_dir / "native-remote-play.lock",
    )
    # Keep the shared lease through the exec chain and the stream lifetime.
    # This also protects the handoff while mako-run is still preparing its environment.
    try:
        descriptor = os.open(paths["lock"], os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
        os.set_inheritable(descriptor, True)
        with os.fdopen(descriptor, "r") as lock:
            fcntl.flock(lock, fcntl.LOCK_SH)
            return launch(paths, sys.argv[1:], os.execve)
    except OSError as error:
        print(f"MAKO Decky: Remote Play launch lock unavailable: {error}", file=sys.stderr)
        return launch(paths, sys.argv[1:], os.execve, allow_override=False)


if __name__ == "__main__":
    sys.exit(main())
