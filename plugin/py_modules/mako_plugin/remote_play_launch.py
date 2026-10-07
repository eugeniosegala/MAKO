#!/usr/bin/python3
# MAKO_NATIVE_REMOTE_PLAY_OVERRIDE_V3
"""Self-contained Steam entry point; installed independently of either UI."""

import fcntl
import hashlib
import json
import os
import re
import stat
from pathlib import Path
import sys
from typing import Callable, Mapping, TypedDict


class LaunchPaths(TypedDict):
    original: Path
    checksum: Path
    state: Path
    config: Path
    runner: Path
    lock: Path


class LaunchSettings(TypedDict):
    MAKO_LAUNCH_RENDERER_REQUIRED: str
    MAKO_LAUNCH_SPATIAL_REQUIRED: str
    MAKO_LAUNCH_WSI_REQUIRED: str
    MAKO_LAUNCH_EXTERNAL_LAYER: str
    MAKO_LAUNCH_VKBASALT_CONFIG: str


# Native streaming uses Vulkan. Never inherit a launcher's OpenGL/Zink override.
REMOVED_ENVIRONMENT = (
    "GALLIUM_DRIVER", "MESA_LOADER_DRIVER_OVERRIDE", "__GLX_VENDOR_LIBRARY_NAME",
    "ENABLE_MAKO", "MAKO_PROFILE", "MAKO_PROFILE_FALLBACK", "MAKO_CONFIG",
    "MAKO_FOLLOW_CURRENT_PROFILE", "MAKO_ENV",
    "ENABLE_VKBASALT", "VKBASALT_CONFIG_FILE", "MAKO_PRESENT_DIAGNOSTICS",
    "ENABLE_MAKO_SPATIAL_SCALING", "DISABLE_MAKO_SPATIAL_SCALING",
    "ENABLE_GAMESCOPE_WSI", "MAKO_SPLIT_LAYER_CHAIN", "MANGOHUD",
    "VKBASALT_CONFIG_RELOAD", "MAKO_EXTERNAL_VULKAN_LAYER",
)
MANAGED_LAYERS = (
    "VK_LAYER_MAKO_render", "VK_LAYER_MAKO_spatial_scaling",
    "VK_LAYER_FROG_gamescope_wsi_x86_64", "VK_LAYER_VKBASALT_post_processing",
    "VK_LAYER_MANGOHUD_overlay_x86_64", "VK_LAYER_MANGOHUD_overlay_x86",
)
PASSTHROUGH_DISABLE_ENVIRONMENT = (
    "DISABLE_MAKO", "DISABLE_MAKO_SPATIAL_SCALING", "DISABLE_GAMESCOPE_WSI",
    "DISABLE_VKBASALT", "DISABLE_MANGOHUD",
)


def open_regular(path: Path):
    """Reject redirected/special launch inputs without waiting on a FIFO."""
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    stream = os.fdopen(descriptor, "rb")
    if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
        stream.close()
        raise ValueError("launch input is not a regular file")
    return stream


def read_record(path: Path) -> str:
    with open_regular(path) as stream:
        content = stream.read(4097)
        if len(content) > 4096:
            raise ValueError("launch record is too large")
        return content.decode("utf-8")


def selected_profile(config: Path) -> dict:
    """Resolve the saved editor selection once, before launching a stream."""
    # Disabled status and activation's interpreter check also run on Python 3.10.
    try:
        import tomllib
    except ImportError as error:
        raise ValueError("Remote Play requires a working system Python 3.11 or newer") from error
    with open_regular(config) as stream:
        content = stream.read().decode("utf-8")
    data = tomllib.loads(content)
    if data.get("version") != 2:
        raise ValueError("unsupported configuration version")
    profiles = data.get("profile", [])
    if not isinstance(profiles, list) or not profiles or any(
        not isinstance(profile, dict) or not isinstance(profile.get("name"), str)
        or not profile["name"] for profile in profiles
    ):
        raise ValueError("selected profile unavailable")
    name = data.get("current_profile")
    # Preserve editor selections saved by older Decky versions on first launch.
    if not isinstance(name, str) or not any(profile['name'] == name for profile in profiles):
        for line in content.splitlines():
            match = re.fullmatch(r'\s*#\s*decky-current-profile\s*=\s*"([^"]+)"\s*', line)
            if match and any(profile['name'] == match[1] for profile in profiles):
                name = match[1]
                break
    return next((profile for profile in profiles if profile["name"] == name),
                next((profile for profile in profiles if profile["name"] == "mako"), profiles[0]))


def launch_settings(profile: dict, config: Path, *, default_shader: Path | None = None,
                    follow_current: bool = False) -> LaunchSettings:
    """Read only process-start discovery inputs; Renderer owns actual settings."""
    modes = [profile]
    for mode in ('handheld', 'docked'):
        if mode in profile:
            if not isinstance(profile[mode], dict):
                raise ValueError('invalid power settings')
            modes.append(profile[mode])
    def required(field: str, default: bool) -> bool:
        values = [mode.get(field, profile.get(field, default)) for mode in modes]
        if any(type(value) is not bool for value in values):
            raise ValueError('invalid layer discovery setting')
        return any(values)
    sidecar = config.parent / 'profile-wrapper-settings.json'
    settings = {}
    if sidecar.exists() or sidecar.is_symlink():
        with open_regular(sidecar) as stream:
            root = json.load(stream)
        if not isinstance(root, dict) or root.get('version') != 1 or not isinstance(root.get('profiles'), dict):
            raise ValueError('invalid wrapper settings')
        settings = root['profiles'].get(profile['name'], {})
        if not isinstance(settings, dict):
            raise ValueError('invalid profile wrapper settings')
    external = settings.get('external_vulkan_layer', '')
    if external not in ('', 'vkbasalt', 'mangohud'):
        raise ValueError('unsupported external layer')
    wsi = settings.get('gamescope_wsi_compatibility', False)
    if type(wsi) is not bool:
        raise ValueError('invalid WSI setting')
    scaling = required('scaling_enabled', False)
    shader_name = 'profile-' + hashlib.sha256(profile['name'].encode()).hexdigest()[:12] + '.conf'
    metadata = config.parent / 'profile-metadata.json'
    if metadata.exists() or metadata.is_symlink():
        with open_regular(metadata) as stream:
            root = json.load(stream)
        if not isinstance(root, dict) or root.get('version') != 1 or not isinstance(root.get('profiles'), dict):
            raise ValueError('invalid profile metadata')
        entry = root['profiles'].get(profile['name'], {})
        if not isinstance(entry, dict):
            raise ValueError('invalid profile metadata entry')
        app_id = entry.get('steam_app_id')
        if isinstance(app_id, str) and app_id.isascii() and app_id.isdigit():
            shader_name = 'steam-' + app_id + '.conf'
    shader = config.parent / 'vkbasalt' / shader_name
    if profile['name'] == 'mako':
        shader = default_shader if default_shader is not None else config.parent.parent / 'vkBasalt' / 'vkBasalt.conf'
    if follow_current:
        # Editors replace this cache when saving/selecting an ordinary profile.
        # vkBasalt can keep its startup filename and reload the selected content.
        shader = config.parent / 'vkbasalt' / 'current-profile.conf'
    return LaunchSettings(
        MAKO_LAUNCH_RENDERER_REQUIRED='1' if required('frame_generation_provisioned', True) or scaling else '0',
        MAKO_LAUNCH_SPATIAL_REQUIRED='1' if scaling and wsi else '0',
        MAKO_LAUNCH_WSI_REQUIRED='1' if wsi else '0',
        MAKO_LAUNCH_EXTERNAL_LAYER=external,
        MAKO_LAUNCH_VKBASALT_CONFIG=str(shader) if external == 'vkbasalt' else '',
    )


def launch(paths: LaunchPaths, arguments: list[str], execute: Callable,
           environment: Mapping[str, str] | None = None, *,
           allow_override: bool = True) -> int:
    """Validate the original, then delegate feature/power setup to the MAKO launcher."""
    clean = dict(os.environ if environment is None else environment)
    for key in REMOVED_ENVIRONMENT:
        clean.pop(key, None)
    clean.update({key: "1" for key in PASSTHROUGH_DISABLE_ENVIRONMENT})
    if "VK_INSTANCE_LAYERS" in clean:
        clean["VK_INSTANCE_LAYERS"] = ":".join(
            layer for layer in clean["VK_INSTANCE_LAYERS"].split(":")
            if layer not in MANAGED_LAYERS
        )
    for key in ('MAKO_LAUNCH_RENDERER_REQUIRED', 'MAKO_LAUNCH_SPATIAL_REQUIRED', 'MAKO_LAUNCH_WSI_REQUIRED',
                'MAKO_LAUNCH_EXTERNAL_LAYER', 'MAKO_LAUNCH_VKBASALT_CONFIG'):
        clean.pop(key, None)
    original = paths["original"]
    try:
        if original.is_symlink() or not original.is_file() or not os.access(original, os.X_OK):
            raise ValueError("original client unavailable")
        with open_regular(original) as stream:
            header = stream.read(20)
            if len(header) < 20 or header[:6] != b"\x7fELF\x02\x01" or header[18:20] != b"\x3e\0":
                raise ValueError("original client is not native x86_64 ELF")
            stream.seek(0)
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        if digest != read_record(paths["checksum"]).strip():
            raise ValueError("original client checksum mismatch")
    except (OSError, ValueError) as error:
        print(f"MAKO Renderer: Remote Play original validation failed: {error}", file=sys.stderr)
        return 126

    try:
        if not allow_override:
            raise ValueError("override launch lock unavailable")
        state = json.loads(read_record(paths["state"]))
        if not isinstance(state, dict) or type(state.get('version')) is not int or state.get("version") != 3 or state.get("enabled") is not True:
            raise ValueError("override disabled")
        profile = selected_profile(paths["config"])
        profile_name = profile["name"]
        if not paths["runner"].is_file() or not os.access(paths["runner"], os.X_OK):
            raise ValueError("MAKO launcher unavailable")
        default_shader = None
        if paths['runner'].name == 'mako-launch':
            if clean.get('XDG_CONFIG_HOME'):
                default_shader = Path(clean['XDG_CONFIG_HOME']) / 'vkBasalt' / 'vkBasalt.conf'
            elif clean.get('HOME'):
                default_shader = Path(clean['HOME']) / '.config/vkBasalt/vkBasalt.conf'
            else:
                default_shader = Path('/etc/vkBasalt.conf')
        settings = launch_settings(profile, paths['config'], default_shader=default_shader,
                                   follow_current=True)
        active = dict(clean)
        active.pop("DISABLE_MAKO", None)
        active.pop("DISABLE_MAKO_SPATIAL_SCALING", None)
        active["MAKO_PROFILE"] = profile_name
        active["MAKO_FOLLOW_CURRENT_PROFILE"] = '1'
        active['MAKO_CONFIG'] = str(paths['config'])
        if paths['runner'].name == 'mako-launch':
            if settings['MAKO_LAUNCH_EXTERNAL_LAYER'] == 'vkbasalt':
                active['ENABLE_VKBASALT'] = '1'
                active.pop('DISABLE_VKBASALT', None)
                active['VKBASALT_CONFIG_FILE'] = settings['MAKO_LAUNCH_VKBASALT_CONFIG']
        else:
            active.update(settings)
        # Existing launchers own layer setup and disabled-feature passthrough;
        # the Renderer owns settings, model availability, and power selection.
        command = [str(paths["runner"]), "/usr/bin/env",
                   "-u", "GALLIUM_DRIVER", "-u", "MESA_LOADER_DRIVER_OVERRIDE",
                   "-u", "__GLX_VENDOR_LIBRARY_NAME", str(original), *arguments]
        execute(command[0], command, active)
        return 0
    except (OSError, ValueError, TypeError, KeyError) as error:
        print(f"MAKO Renderer: Remote Play passthrough: {error}", file=sys.stderr)
    try:
        execute(str(original), [str(original), *arguments], clean)
        return 0
    except OSError as error:
        print(f"MAKO Renderer: Remote Play original launch failed: {error}", file=sys.stderr)
        return 126


def main() -> int:
    root = Path(__file__).resolve().parent
    home = Path('__MAKO_USER_HOME__')
    config_dir = home / ".config/mako-render"
    paths = LaunchPaths(
        original=root / "streaming_client.mako-original",
        checksum=root / "streaming_client.mako-original.sha256",
        state=config_dir / "native-remote-play.json",
        config=Path('__MAKO_CONFIG_PATH__'),
        runner=Path('__MAKO_RUNNER_PATH__'),
        lock=config_dir / "native-remote-play.lock",
    )
    # Keep the shared lease through the exec chain and the stream lifetime.
    # This also protects the handoff while mako-run is still preparing its environment.
    try:
        descriptor = os.open(paths["lock"], os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK)
        with os.fdopen(descriptor, "r") as lock:
            identity = os.fstat(lock.fileno())
            if not stat.S_ISREG(identity.st_mode) or identity.st_nlink != 1:
                raise OSError("launch lock is not a private regular file")
            os.set_inheritable(descriptor, True)
            fcntl.flock(lock, fcntl.LOCK_SH)
            return launch(paths, sys.argv[1:], os.execve)
    except OSError as error:
        print(f"MAKO Renderer: Remote Play launch lock unavailable: {error}", file=sys.stderr)
        return launch(paths, sys.argv[1:], os.execve, allow_override=False)


if __name__ == "__main__":
    sys.exit(main())
