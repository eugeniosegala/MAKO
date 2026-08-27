"""Pure generation of MAKO Decky's managed game-launch wrapper.

The wrapper is disposable generated cache. Canonical profile and sidecar data
enter through explicit arguments, and this module returns shell text without
reading or writing user files. Compatibility migrations remain in the service
that owns those persisted inputs.
"""

from dataclasses import dataclass
from pathlib import Path
import re
import shlex
from typing import Any, Callable, Dict, Optional

from .config_schema import DEFAULT_PROFILE_NAME, ProfileData
from .config_schema_generated import (
    ConfigurationData,
    get_script_generation_logic,
)
from .constants import (
    COMPETING_LSFG_DISABLE_ENVS,
    DXVK_HDR_ENV,
    EXTERNAL_VULKAN_LAYER_ENV,
    EXTERNAL_VULKAN_LAYER_MANGOHUD,
    EXTERNAL_VULKAN_LAYER_VKBASALT,
    GAMESCOPE_WSI_DISABLE_ENV,
    GAMESCOPE_WSI_ENABLE_ENV,
    HDR_EXPOSURE_DISABLE_ENV,
    MAKO_CONFIG_ENV,
    MAKO_LAYER_DISABLE_ENV,
    MAKO_LAYER_ENABLE_ENV,
    MAKO_PROFILE_ENV,
    MAKO_PROFILE_FALLBACK_ENV,
    STEAM_APP_ID_ENV_KEYS,
    PRESENT_ACQUIRE_TIMEOUT_ENV,
    PRESENT_ACQUIRE_TIMEOUT_MS,
    PRESENT_DIAGNOSTICS_ENV,
    PRESENT_DIAGNOSTICS_LOG_ENV,
    PRESENT_DIAGNOSTICS_LOG_FILENAME,
    PRESENT_DIAGNOSTICS_RETAINED_SESSION_COUNT,
    VK_ADD_IMPLICIT_LAYER_PATH_ENV,
    VK_IMPLICIT_LAYER_PATH_ENV,
)
from .profile_storage import (
    ProfileMetadata,
    WrapperProfileSettings,
    config_for_profile,
    processes_for_config,
)


WRAPPER_FORMAT_VERSION = 47
WRAPPER_FORMAT_MARKER = f"# mako-wrapper-format: {WRAPPER_FORMAT_VERSION}"
HOST_COMPATIBILITY_MARKER = "# mako-host-compatibility: aarch64-passthrough-v1"
DIAGNOSTICS_DEFAULT_MARKER = (
    "# development presentation diagnostics default: disabled"
)
REQUIRED_WRAPPER_EXPORTS = (
    f"export {PRESENT_ACQUIRE_TIMEOUT_ENV}=",
    f"export {PRESENT_DIAGNOSTICS_ENV}=",
    f"export {MAKO_LAYER_ENABLE_ENV}=1",
    *(f"export {variable}=1" for variable in COMPETING_LSFG_DISABLE_ENVS),
    f"export {GAMESCOPE_WSI_DISABLE_ENV}=1",
    f"unset {GAMESCOPE_WSI_ENABLE_ENV}",
    "mako_gamescope_wsi_required=",
    f"export {EXTERNAL_VULKAN_LAYER_ENV}=",
    f"export {VK_IMPLICIT_LAYER_PATH_ENV}=",
    f"unset {VK_ADD_IMPLICIT_LAYER_PATH_ENV}",
    f"export {MAKO_PROFILE_FALLBACK_ENV}=",
    "mako_diagnostics_default=",
)
OBSOLETE_WRAPPER_EXPORTS = (
    "DXVK_FRAME_RATE",
    "PROTON_USE_WOW64",
    "MAKO_PRESENT_RECOVERY_RECREATE",
    "MAKO_EXPERIMENTAL_HDR",
    "VK_INSTANCE_LAYERS",
)


def is_current_wrapper(
        content: str,
        wrapper_format_marker: str = WRAPPER_FORMAT_MARKER,
        host_compatibility_marker: str = HOST_COMPATIBILITY_MARKER,
        diagnostics_default_marker: str = DIAGNOSTICS_DEFAULT_MARKER,
        required_exports: tuple[str, ...] = REQUIRED_WRAPPER_EXPORTS,
        obsolete_exports: tuple[str, ...] = OBSOLETE_WRAPPER_EXPORTS,
) -> bool:
    """Return whether generated cache satisfies every current safety marker."""
    return (
        wrapper_format_marker in content
        and host_compatibility_marker in content
        and diagnostics_default_marker in content
        and all(export in content for export in required_exports)
        and not any(export in content for export in obsolete_exports)
    )


@dataclass(frozen=True)
class WrapperGenerationContext:
    """Paths and compatibility inputs embedded in one generated wrapper."""

    wrapper_format_marker: str
    host_compatibility_marker: str
    diagnostics_default_marker: str
    config_dir: Path
    config_file_path: Path
    local_share_dir: Path
    gamescope_wsi_compatibility_dir: Path
    mangohud_layer_dir: Path
    vkbasalt_layer_dir: Path
    flatpak_implicit_layer_dir: str
    gamescope_wsi_manifest_filename_64: str
    mangohud_manifest_filename_64: str
    mangohud_manifest_filename_32: str
    vkbasalt_manifest_filename_64: str
    vkbasalt_manifest_filename_32: str
    armada_device_env: Path
    armada_game_launch: Path


def has_active_in(config: ConfigurationData) -> bool:
    """Return whether an engine profile can select itself by process name."""
    active_in = config.get("active_in", "")
    if isinstance(active_in, (list, tuple)):
        return bool(active_in)
    return bool(str(active_in).strip())


def profile_selection_lines(
        profile_name: str,
        config: ConfigurationData,
        automatic_matching_enabled: Optional[bool] = None,
        active_predicate: Callable[
            [ConfigurationData], bool
        ] = has_active_in,
) -> list[str]:
    """Keep the renderer active while allowing automatic live matching."""
    if automatic_matching_enabled is None:
        automatic_matching_enabled = active_predicate(config)

    matching_comment = (
        "# MAKO Renderer prefers active_in matches and uses this profile only as a fallback."
        if automatic_matching_enabled
        else "# Keep the default renderer context active so a newly captured profile can take over live."
    )
    return [
        matching_comment,
        f"# A caller-provided {MAKO_PROFILE_ENV} remains an explicit hard override.",
        f'if [ -z "${{{MAKO_PROFILE_ENV}:-}}" ]; then',
        f"    export {MAKO_PROFILE_FALLBACK_ENV}={shlex.quote(profile_name)}",
        "fi",
    ]


def hdr_activation_lines(config: Dict[str, Any]) -> list[str]:
    """Keep the packaged Decky launcher on its proven SDR contract."""
    del config
    return [
        f"export {HDR_EXPOSURE_DISABLE_ENV}=1",
        f"unset {DXVK_HDR_ENV}",
    ]


def script_configuration_lines(
        config: ConfigurationData,
        hdr_lines: Callable[
            [Dict[str, Any]], list[str]
        ] = hdr_activation_lines,
) -> list[str]:
    """Generate wrapper settings without repeating forced compatibility exports."""
    lines = get_script_generation_logic()(config)
    # A Vulkan layer chain cannot gain Gamescope WSI after instance creation.
    # The Scaling Engine provisions that presentation path even while its live
    # method is Native; the explicit compatibility switch provisions the same
    # path for FG-only profiles. Keep the combined decision shell-local.
    lines.append(
        "mako_gamescope_wsi_required="
        f"{1 if (config.get('scaling_enabled', False) or config.get('gamescope_wsi_compatibility', False)) else 0}"
    )
    for line in hdr_lines(config):
        if line not in lines:
            lines.append(line)
    return lines


def unsupported_host_passthrough_lines(
        armada_device_env: Path,
        armada_game_launch: Path,
        indent: str = "",
) -> list[str]:
    """Disable MAKO and preserve Armada's launcher exactly once."""
    device_env = armada_device_env.as_posix()
    game_launch = armada_game_launch.as_posix()
    return [
        f"{indent}unset {MAKO_LAYER_ENABLE_ENV}",
        f"{indent}export {MAKO_LAYER_DISABLE_ENV}=1",
        f'{indent}armada_game_launch="{game_launch}"',
        f'{indent}if [ -f "{device_env}" ] && [ -x "$armada_game_launch" ]; then',
        f'{indent}    for argument in "$@"; do',
        f'{indent}        if [ "$argument" = "$armada_game_launch" ]; then',
        f'{indent}            exec "$@"',
        f"{indent}        fi",
        f"{indent}    done",
        f'{indent}    exec "$armada_game_launch" "$@"',
        f"{indent}fi",
        f'{indent}exec "$@"',
    ]


def host_compatibility_guard_lines(
        armada_device_env: Path,
        armada_game_launch: Path,
        compatibility_marker: str = HOST_COMPATIBILITY_MARKER,
        passthrough_lines: Optional[list[str]] = None,
) -> list[str]:
    """Bypass MAKO before any exports on unsupported AArch64 hosts."""
    device_env = armada_device_env.as_posix()
    return [
        compatibility_marker,
        'mako_native_arch="$(uname -m 2>/dev/null || true)"',
        f'if [ -f "{device_env}" ] || [ "$mako_native_arch" = "aarch64" ] || [ "$mako_native_arch" = "arm64" ]; then',
        "    # This release has no validated native AArch64 Renderer.",
        *(
            passthrough_lines
            if passthrough_lines is not None
            else unsupported_host_passthrough_lines(
                armada_device_env,
                armada_game_launch,
                "    ",
            )
        ),
        "fi",
    ]


def layer_environment_lines(context: WrapperGenerationContext) -> list[str]:
    """Activate MAKO through its deterministic Vulkan discovery boundary."""
    if PRESENT_DIAGNOSTICS_RETAINED_SESSION_COUNT != 3:
        raise ValueError("the managed diagnostics rotation requires three sessions")
    diagnostics_log_path = context.config_dir / PRESENT_DIAGNOSTICS_LOG_FILENAME
    gamescope_wsi_manifest = shlex.quote(str(
        context.gamescope_wsi_compatibility_dir /
        context.gamescope_wsi_manifest_filename_64
    ))
    gamescope_wsi_layer_dir = shlex.quote(str(
        context.gamescope_wsi_compatibility_dir
    ))
    mangohud_manifest = shlex.quote(str(
        context.mangohud_layer_dir / context.mangohud_manifest_filename_64
    ))
    mangohud_manifest32 = shlex.quote(str(
        context.mangohud_layer_dir / context.mangohud_manifest_filename_32
    ))
    mangohud_layer_dir = shlex.quote(str(context.mangohud_layer_dir))
    vkbasalt_manifest = shlex.quote(str(
        context.vkbasalt_layer_dir / context.vkbasalt_manifest_filename_64
    ))
    vkbasalt_manifest32 = shlex.quote(str(
        context.vkbasalt_layer_dir / context.vkbasalt_manifest_filename_32
    ))
    vkbasalt_layer_dir = shlex.quote(str(context.vkbasalt_layer_dir))
    return [
        f'export {PRESENT_ACQUIRE_TIMEOUT_ENV}="${{{PRESENT_ACQUIRE_TIMEOUT_ENV}:-{PRESENT_ACQUIRE_TIMEOUT_MS}}}"',
        # Presentation logging is intentionally opt-in for every build.
        # Slow-path records are synchronous and can distort the timing problem
        # being measured when a compositor is already congested.
        f'export {PRESENT_DIAGNOSTICS_ENV}="${{{PRESENT_DIAGNOSTICS_ENV}:-0}}"',
        f"export {MAKO_LAYER_ENABLE_ENV}=1",
        *(f"export {variable}=1" for variable in COMPETING_LSFG_DISABLE_ENVS),
        f"export {GAMESCOPE_WSI_DISABLE_ENV}=1",
        f"unset {GAMESCOPE_WSI_ENABLE_ENV}",
        f'mako_external_vulkan_layer="${{{EXTERNAL_VULKAN_LAYER_ENV}:-}}"',
        f"unset {EXTERNAL_VULKAN_LAYER_ENV}",
        f"mako_gamescope_wsi_layer_dir={gamescope_wsi_layer_dir}",
        f"mako_mangohud_layer_dir={mangohud_layer_dir}",
        f"mako_vkbasalt_layer_dir={vkbasalt_layer_dir}",
        "unset MANGOHUD",
        "unset ENABLE_VKBASALT",
        f"if [ -d {shlex.quote(context.flatpak_implicit_layer_dir)} ]; then",
        f"    mako_implicit_layer_path={shlex.quote(context.flatpak_implicit_layer_dir)}",
        "else",
        f"    mako_implicit_layer_path={shlex.quote(str(context.local_share_dir))}",
        # Gamescope WSI runs above MAKO. Its internal Wayland surface gives
        # MAKO a variable lower surface while the game keeps its render extent.
        # Scaling Engine and explicit WSI compatibility own this independent
        # presentation boundary; MangoHud or vkBasalt may be admitted below
        # MAKO as a separate, mutually exclusive post-process choice.
        '    if [ "${mako_gamescope_wsi_required:-0}" = 1 ] && [ -r '
        f"{gamescope_wsi_manifest} ]; then",
        f"        unset {GAMESCOPE_WSI_DISABLE_ENV}",
        f"        export {GAMESCOPE_WSI_ENABLE_ENV}=1",
        "        export NODEVICE_SELECT=1",
        "        export DISABLE_LAYER_MESA_ANTI_LAG=1",
        # Search order is dispatch order for these staged implicit manifests:
        # Gamescope WSI must intercept the application surface first and call
        # down into MAKO with its internal variable Wayland surface. Reversing
        # these directories leaves MAKO on the fixed X11 surface and scaling
        # cannot obtain a distinct presentation extent.
        '        mako_implicit_layer_path="$mako_gamescope_wsi_layer_dir:$mako_implicit_layer_path"',
        "    fi",
        '    case "$mako_external_vulkan_layer" in',
        f"        {EXTERNAL_VULKAN_LAYER_MANGOHUD})",
        f"            if [ -r {mangohud_manifest} ] || [ -r {mangohud_manifest32} ]; then",
        "                unset DISABLE_MANGOHUD",
        "                export MANGOHUD=1",
        "                export NODEVICE_SELECT=1",
        "                export DISABLE_LAYER_MESA_ANTI_LAG=1",
        '                mako_implicit_layer_path="$mako_implicit_layer_path:$mako_mangohud_layer_dir"',
        "            fi",
        "            ;;",
        f"        {EXTERNAL_VULKAN_LAYER_VKBASALT})",
        f"            if [ -r {vkbasalt_manifest} ] || [ -r {vkbasalt_manifest32} ]; then",
        "                unset DISABLE_VKBASALT",
        "                export ENABLE_VKBASALT=1",
        "                export NODEVICE_SELECT=1",
        "                export DISABLE_LAYER_MESA_ANTI_LAG=1",
        '                mako_implicit_layer_path="$mako_implicit_layer_path:$mako_vkbasalt_layer_dir"',
        "            fi",
        "            ;;",
        "    esac",
        "fi",
        "unset mako_gamescope_wsi_required",
        "unset mako_external_vulkan_layer",
        "unset mako_gamescope_wsi_layer_dir",
        "unset mako_mangohud_layer_dir",
        "unset mako_vkbasalt_layer_dir",
        f'export {VK_IMPLICIT_LAYER_PATH_ENV}="$mako_implicit_layer_path"',
        f"unset {VK_ADD_IMPLICIT_LAYER_PATH_ENV}",
        f"export {MAKO_CONFIG_ENV}={shlex.quote(str(context.config_file_path))}",
        "# Heroic can discard a game's stderr. Capture opt-in engine diagnostics here instead.",
        f"mako_diagnostics_default={shlex.quote(str(diagnostics_log_path))}",
        f'if [ "${{{PRESENT_DIAGNOSTICS_ENV}:-0}}" != "0" ]; then',
        f'    mako_diagnostics_log="${{{PRESENT_DIAGNOSTICS_LOG_ENV}:-$mako_diagnostics_default}}"',
        '    mako_diagnostics_previous="${mako_diagnostics_log}.1"',
        '    mako_diagnostics_oldest="${mako_diagnostics_log}.2"',
        "    mako_diagnostics_rotation_ready=1",
        '    if [ -f "$mako_diagnostics_log" ]; then',
        (
            '        if [ -f "$mako_diagnostics_previous" ] && ! mv -f -- '
            '"$mako_diagnostics_previous" "$mako_diagnostics_oldest" '
            "2>/dev/null; then"
        ),
        "            mako_diagnostics_rotation_ready=0",
        "        fi",
        (
            '        if [ "$mako_diagnostics_rotation_ready" = 1 ] && '
            '! mv -f -- "$mako_diagnostics_log" '
            '"$mako_diagnostics_previous" 2>/dev/null; then'
        ),
        "            mako_diagnostics_rotation_ready=0",
        "        fi",
        "    fi",
        (
            '    if [ "$mako_diagnostics_rotation_ready" = 1 ] && '
            ': > "$mako_diagnostics_log" 2>/dev/null; then'
        ),
        '        exec 2>> "$mako_diagnostics_log"',
        "    fi",
        "fi",
    ]


def wrapper_profile_configuration_lines(
        profile_data: ProfileData,
        profile_settings: WrapperProfileSettings,
        metadata: ProfileMetadata,
        profile_config: Callable[
            [ProfileData, str, WrapperProfileSettings], ConfigurationData
        ] = config_for_profile,
        config_lines: Callable[
            [ConfigurationData], list[str]
        ] = script_configuration_lines,
) -> list[str]:
    """Select launcher-only settings by explicit profile or Steam app ID."""
    current_profile = profile_data["current_profile"]
    app_id_fallback = ""
    for environment_name in reversed(STEAM_APP_ID_ENV_KEYS):
        app_id_fallback = f"${{{environment_name}:-{app_id_fallback}}}"
    app_profiles = [
        (entry.get("steam_app_id"), profile_name)
        for profile_name, entry in metadata.items()
        if re.fullmatch(r"\d+", str(entry.get("steam_app_id") or ""))
    ]
    process_profiles = [
        (profile_name, processes_for_config(config))
        for profile_name, config in profile_data["profiles"].items()
        if profile_name != DEFAULT_PROFILE_NAME
        and processes_for_config(config)
    ]

    lines = [
        f'mako_wrapper_profile="${{{MAKO_PROFILE_ENV}:-}}"',
        "mako_wrapper_profile_from_identity=0",
        f'mako_wrapper_app_id="{app_id_fallback}"',
        'if [ -z "$mako_wrapper_profile" ]; then',
        '    if [ -n "$mako_wrapper_app_id" ]; then',
        '        case "$mako_wrapper_app_id" in',
    ]
    for app_id, profile_name in app_profiles:
        lines.extend([
            f"            {app_id})",
            f"                mako_wrapper_profile={shlex.quote(profile_name)}",
            "                mako_wrapper_profile_from_identity=1",
            "                ;;",
        ])
    lines.extend([
        "            *)",
        f"                mako_wrapper_profile={shlex.quote(DEFAULT_PROFILE_NAME if DEFAULT_PROFILE_NAME in profile_data['profiles'] else current_profile)}",
        "                ;;",
        "        esac",
        "    else",
        '        case " $* " in',
    ])
    for profile_name, processes in process_profiles:
        patterns = "|".join(
            f"*{shlex.quote(process_name)}*" for process_name in processes
        )
        lines.extend([
            f"            {patterns})",
            f"                mako_wrapper_profile={shlex.quote(profile_name)}",
            "                mako_wrapper_profile_from_identity=1",
            "                ;;",
        ])
    lines.extend([
        "            *)",
        f"                mako_wrapper_profile={shlex.quote(current_profile)}",
        "                ;;",
        "        esac",
        "    fi",
        "fi",
        'case "$mako_wrapper_profile" in',
    ])

    for profile_name in profile_data["profiles"]:
        config = profile_config(
            profile_data,
            profile_name,
            profile_settings,
        )
        lines.append(f"    {shlex.quote(profile_name)})")
        lines.extend(
            f"        {line}" for line in config_lines(config)
        )
        lines.append("        ;;")

    fallback_config = profile_config(
        profile_data,
        current_profile,
        profile_settings,
    )
    lines.append("    *)")
    lines.extend(
        f"        {line}"
        for line in config_lines(fallback_config)
    )
    lines.extend([
        "        ;;",
        "esac",
        'if [ "$mako_wrapper_profile_from_identity" = "1" ]; then',
        f'    export {MAKO_PROFILE_ENV}="$mako_wrapper_profile"',
        "fi",
    ])
    return lines


def assemble_script_content(
        context: WrapperGenerationContext,
        host_guard_lines: list[str],
        configuration_lines: list[str],
        layer_lines: list[str],
        selection_lines: list[str],
) -> str:
    """Assemble a single-profile wrapper from explicit generated sections."""
    lines = [
        "#!/bin/bash",
        context.wrapper_format_marker,
        context.diagnostics_default_marker,
        "# mako launch script generated by MAKO Decky",
        "# This script sets up the environment for mako to work with the plugin configuration",
    ]
    lines.extend(host_guard_lines)
    lines.extend(configuration_lines)
    lines.extend(layer_lines)
    lines.extend(selection_lines)
    lines.append('exec "$@"')
    return "\n".join(lines) + "\n"


def assemble_profile_script_content(
        current_profile: str,
        context: WrapperGenerationContext,
        host_guard_lines: list[str],
        profile_configuration_lines: list[str],
        layer_lines: list[str],
        selection_lines: list[str],
) -> str:
    """Assemble a multi-profile wrapper from explicit generated sections."""
    lines = [
        "#!/bin/bash",
        context.wrapper_format_marker,
        context.diagnostics_default_marker,
        f"# Current profile: {current_profile}",
    ]
    lines.extend(host_guard_lines)
    lines.extend(profile_configuration_lines)
    lines.extend(layer_lines)
    lines.extend(selection_lines)
    lines.append('exec "$@"')
    return "\n".join(lines) + "\n"


def generate_script_content(
        config: ConfigurationData,
        context: WrapperGenerationContext,
) -> str:
    """Generate the isolated single-profile launch script."""
    return assemble_script_content(
        context,
        host_compatibility_guard_lines(
            context.armada_device_env,
            context.armada_game_launch,
            context.host_compatibility_marker,
        ),
        script_configuration_lines(config),
        layer_environment_lines(context),
        profile_selection_lines(DEFAULT_PROFILE_NAME, config),
    )


def generate_profile_script_content(
        profile_data: ProfileData,
        profile_settings: WrapperProfileSettings,
        metadata: ProfileMetadata,
        context: WrapperGenerationContext,
) -> str:
    """Generate the isolated multi-profile launch script."""
    current_profile = profile_data["current_profile"]
    fallback_profile = (
        DEFAULT_PROFILE_NAME
        if DEFAULT_PROFILE_NAME in profile_data["profiles"]
        else current_profile
    )
    fallback_config = config_for_profile(
        profile_data,
        fallback_profile,
        profile_settings,
    )
    automatic_matching_enabled = any(
        has_active_in(profile_config)
        for profile_config in profile_data["profiles"].values()
    )
    return assemble_profile_script_content(
        current_profile,
        context,
        host_compatibility_guard_lines(
            context.armada_device_env,
            context.armada_game_launch,
            context.host_compatibility_marker,
        ),
        wrapper_profile_configuration_lines(
            profile_data,
            profile_settings,
            metadata,
        ),
        layer_environment_lines(context),
        profile_selection_lines(
            fallback_profile,
            fallback_config,
            automatic_matching_enabled,
        ),
    )
