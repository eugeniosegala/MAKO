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
    GAMESCOPE_WSI_LAYER_NAME_64,
    GAMESCOPE_WAYLAND_DISPLAY_ENV,
    HDR_EXPOSURE_DISABLE_ENV,
    MAKO_CONFIG_ENV,
    MAKO_LAYER_DISABLE_ENV,
    MAKO_LAYER_ENABLE_ENV,
    MAKO_LAYER_NAME,
    MAKO_PROFILE_ENV,
    MAKO_PROFILE_FALLBACK_ENV,
    MAKO_SPLIT_LAYER_CHAIN_COMBINED_PIPELINE,
    MAKO_SPLIT_LAYER_CHAIN_ENV,
    MANGOHUD_LAYER_NAME_64,
    PRESENT_ACQUIRE_TIMEOUT_ENV,
    PRESENT_ACQUIRE_TIMEOUT_MS,
    PRESENT_DIAGNOSTICS_ENV,
    PRESENT_DIAGNOSTICS_LOG_ENV,
    PRESENT_DIAGNOSTICS_LOG_FILENAME,
    PRESENT_DIAGNOSTICS_RETAINED_SESSION_COUNT,
    SPATIAL_SCALING_LAYER_DISABLE_ENV,
    SPATIAL_SCALING_LAYER_ENABLE_ENV,
    SPATIAL_SCALING_LAYER_NAME,
    STEAM_APP_ID_ENV_KEYS,
    VK_ADD_IMPLICIT_LAYER_PATH_ENV,
    VK_IMPLICIT_LAYER_PATH_ENV,
    VK_INSTANCE_LAYERS_ENV,
    VKBASALT_CONFIG_FILE_ENV,
    VKBASALT_CONFIG_RELOAD_ENV,
    VKBASALT_LAYER_DISABLE_ENV,
    VKBASALT_LAYER_ENABLE_ENV,
    VKBASALT_LAYER_NAME_64,
    WAYLAND_DISPLAY_ENV,
)
from .profile_storage import (
    ProfileMetadata,
    WrapperProfileSettings,
    config_for_profile,
    metadata_steam_app_id,
    processes_for_config,
    vkbasalt_config_path,
)


WRAPPER_FORMAT_VERSION = 66
WRAPPER_FORMAT_MARKER = f"# mako-wrapper-format: {WRAPPER_FORMAT_VERSION}"
HOST_COMPATIBILITY_MARKER = "# mako-host-compatibility: aarch64-passthrough-v1"
DIAGNOSTICS_DEFAULT_MARKER = (
    "# development presentation diagnostics default: disabled"
)
REQUIRED_WRAPPER_EXPORTS = (
    f"export {PRESENT_ACQUIRE_TIMEOUT_ENV}=",
    f"export {PRESENT_DIAGNOSTICS_ENV}=",
    f"export {MAKO_LAYER_ENABLE_ENV}=1",
    "mako_renderer_required=",
    "mako_renderer_enabled=",
    f"unset {MAKO_SPLIT_LAYER_CHAIN_ENV}",
    f"export {MAKO_SPLIT_LAYER_CHAIN_ENV}={MAKO_SPLIT_LAYER_CHAIN_COMBINED_PIPELINE}",
    *(f"export {variable}=1" for variable in COMPETING_LSFG_DISABLE_ENVS),
    f"export {GAMESCOPE_WSI_DISABLE_ENV}=1",
    f"unset {GAMESCOPE_WSI_ENABLE_ENV}",
    "mako_gamescope_wsi_required=",
    "mako_gamescope_wsi_session=",
    "mako_gamescope_wsi_skip_log=",
    f"export {SPATIAL_SCALING_LAYER_DISABLE_ENV}=1",
    f"unset {SPATIAL_SCALING_LAYER_ENABLE_ENV}",
    f"export {VKBASALT_LAYER_DISABLE_ENV}=1",
    f"unset {VKBASALT_LAYER_ENABLE_ENV}",
    f"unset {VKBASALT_CONFIG_RELOAD_ENV}",
    "mako_spatial_scaling_required=",
    f"export {EXTERNAL_VULKAN_LAYER_ENV}=",
    "mako_vkbasalt_config=",
    "mako_vkbasalt_enabled=",
    "mako_managed_instance_layers=",
    f"export {VK_INSTANCE_LAYERS_ENV}=",
    f"export {VK_IMPLICIT_LAYER_PATH_ENV}=",
    f"unset {VK_ADD_IMPLICIT_LAYER_PATH_ENV}",
    f"export {MAKO_PROFILE_FALLBACK_ENV}=",
    "mako_diagnostics_default=",
)
def is_current_wrapper(
        content: str,
        wrapper_format_marker: str = WRAPPER_FORMAT_MARKER,
        host_compatibility_marker: str = HOST_COMPATIBILITY_MARKER,
        diagnostics_default_marker: str = DIAGNOSTICS_DEFAULT_MARKER,
        required_exports: tuple[str, ...] = REQUIRED_WRAPPER_EXPORTS,
) -> bool:
    """Return whether generated cache satisfies every current safety marker."""
    return (
        wrapper_format_marker in content
        and host_compatibility_marker in content
        and diagnostics_default_marker in content
        and all(export in content for export in required_exports)
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
    spatial_scaling_layer_dir: Path
    gamescope_wsi_compatibility_dir: Path
    mangohud_layer_dir: Path
    vkbasalt_layer_dir: Path
    vkbasalt_global_config_path: Path
    vkbasalt_profile_config_dir: Path
    flatpak_implicit_layer_dir: str
    gamescope_wsi_manifest_filename_64: str
    spatial_scaling_manifest_filename_64: str
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
    # WSI is an independent, restart-only compatibility choice. Without it,
    # the combined Renderer owns scaling and Frame Generation. Only an
    # explicitly selected WSI path needs the lower spatial role for scaling.
    lines.append(
        "mako_gamescope_wsi_required="
        f"{1 if config.get('gamescope_wsi_compatibility', False) else 0}"
    )
    lines.append(
        "mako_renderer_required="
        f"{1 if (config.get('frame_generation_provisioned', True) or config.get('scaling_enabled', False)) else 0}"
    )
    lines.append(
        "mako_spatial_scaling_required="
        f"{1 if (config.get('scaling_enabled', False) and config.get('gamescope_wsi_compatibility', False)) else 0}"
    )
    for line in hdr_lines(config):
        if line not in lines:
            lines.append(line)
    return lines


def vkbasalt_profile_environment_lines(
        profile_name: str,
        config: ConfigurationData,
        global_config_path: Path,
        profile_config_dir: Path,
        steam_app_id: Optional[str] = None,
) -> list[str]:
    """Select the automatic global or saved-profile vkBasalt config."""
    config_path = ""
    if config.get("external_vulkan_layer") == EXTERNAL_VULKAN_LAYER_VKBASALT:
        config_path = str(
            vkbasalt_config_path(
                profile_name,
                global_config_path,
                profile_config_dir,
                steam_app_id,
            )
        )
    return [f"mako_vkbasalt_config={shlex.quote(config_path)}"]


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
    if PRESENT_DIAGNOSTICS_RETAINED_SESSION_COUNT < 1:
        raise ValueError("managed diagnostics must retain at least one session")
    diagnostics_log_path = context.config_dir / PRESENT_DIAGNOSTICS_LOG_FILENAME
    diagnostics_history_paths = " ".join(
        '"$mako_diagnostics_log' + (f".{index}" if index else "") + '"'
        for index in range(PRESENT_DIAGNOSTICS_RETAINED_SESSION_COUNT)
    )
    diagnostics_rotation_lines: list[str] = []
    for retained_index in range(
            PRESENT_DIAGNOSTICS_RETAINED_SESSION_COUNT - 1,
            0,
            -1,
    ):
        source_suffix = "" if retained_index == 1 else f".{retained_index - 1}"
        target_suffix = f".{retained_index}"
        diagnostics_rotation_lines.extend([
            (
                '        if [ "$mako_diagnostics_rotation_ready" = 1 ] && '
                f'[ -f "$mako_diagnostics_log{source_suffix}" ] && ! mv -fT -- '
                f'"$mako_diagnostics_log{source_suffix}" '
                f'"$mako_diagnostics_log{target_suffix}" 2>/dev/null; then'
            ),
            "            mako_diagnostics_rotation_ready=0",
            "        fi",
        ])
    gamescope_wsi_manifest = shlex.quote(str(
        context.gamescope_wsi_compatibility_dir /
        context.gamescope_wsi_manifest_filename_64
    ))
    gamescope_wsi_layer_dir = shlex.quote(str(
        context.gamescope_wsi_compatibility_dir
    ))
    spatial_scaling_manifest = shlex.quote(str(
        context.spatial_scaling_layer_dir /
        context.spatial_scaling_manifest_filename_64
    ))
    spatial_scaling_layer_dir = shlex.quote(str(
        context.spatial_scaling_layer_dir
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
    inherited_managed_layer_removal_lines: list[str] = []
    for layer_name in (
        MAKO_LAYER_NAME,
        SPATIAL_SCALING_LAYER_NAME,
        GAMESCOPE_WSI_LAYER_NAME_64,
        VKBASALT_LAYER_NAME_64,
    ):
        inherited_managed_layer_removal_lines.extend((
            (
                'while [[ "$mako_existing_instance_layers" == *":'
                f'{layer_name}:"* ]]; do'
            ),
            (
                '    mako_existing_instance_layers="'
                '${mako_existing_instance_layers/:'
                f'{layer_name}:/:}}"'
            ),
            "done",
        ))
    return [
        f'export {PRESENT_ACQUIRE_TIMEOUT_ENV}="${{{PRESENT_ACQUIRE_TIMEOUT_ENV}:-{PRESENT_ACQUIRE_TIMEOUT_MS}}}"',
        # Presentation logging is intentionally opt-in for every build.
        # Slow-path records are synchronous and can distort the timing problem
        # being measured when a compositor is already congested.
        f'export {PRESENT_DIAGNOSTICS_ENV}="${{{PRESENT_DIAGNOSTICS_ENV}:-0}}"',
        "mako_renderer_enabled=0",
        'if [ "${mako_renderer_required:-0}" = 1 ] && '
        f'[ "${{{MAKO_LAYER_DISABLE_ENV}:-0}}" != 1 ]; then',
        f"    unset {MAKO_LAYER_DISABLE_ENV}",
        f"    export {MAKO_LAYER_ENABLE_ENV}=1",
        "    mako_renderer_enabled=1",
        "else",
        f"    unset {MAKO_LAYER_ENABLE_ENV}",
        f"    export {MAKO_LAYER_DISABLE_ENV}=1",
        "fi",
        f"unset {MAKO_SPLIT_LAYER_CHAIN_ENV}",
        *(f"export {variable}=1" for variable in COMPETING_LSFG_DISABLE_ENVS),
        f"export {GAMESCOPE_WSI_DISABLE_ENV}=1",
        f"unset {GAMESCOPE_WSI_ENABLE_ENV}",
        f"export {SPATIAL_SCALING_LAYER_DISABLE_ENV}=1",
        f"unset {SPATIAL_SCALING_LAYER_ENABLE_ENV}",
        f'mako_existing_instance_layers=":${{{VK_INSTANCE_LAYERS_ENV}:-}}:"',
        *inherited_managed_layer_removal_lines,
        'mako_existing_instance_layers="${mako_existing_instance_layers#:}"',
        'mako_existing_instance_layers="${mako_existing_instance_layers%:}"',
        'if [ -n "$mako_existing_instance_layers" ]; then',
        f'    export {VK_INSTANCE_LAYERS_ENV}="$mako_existing_instance_layers"',
        "else",
        f"    unset {VK_INSTANCE_LAYERS_ENV}",
        "fi",
        "mako_managed_instance_layers=",
        "mako_managed_external_layer=",
        "mako_gamescope_wsi_session=0",
        "mako_gamescope_wsi_skip_log=",
        f'if [ -n "${{{GAMESCOPE_WAYLAND_DISPLAY_ENV}:-}}" ] && '
        f'{{ [ -z "${{{WAYLAND_DISPLAY_ENV}:-}}" ] || '
        f'[ "${{{WAYLAND_DISPLAY_ENV}}}" = "${{{GAMESCOPE_WAYLAND_DISPLAY_ENV}}}" ]; }}; then',
        "    mako_gamescope_wsi_session=1",
        "fi",
        f'mako_external_vulkan_layer="${{{EXTERNAL_VULKAN_LAYER_ENV}:-}}"',
        f"unset {EXTERNAL_VULKAN_LAYER_ENV}",
        f"mako_gamescope_wsi_layer_dir={gamescope_wsi_layer_dir}",
        f"mako_spatial_scaling_layer_dir={spatial_scaling_layer_dir}",
        f"mako_mangohud_layer_dir={mangohud_layer_dir}",
        f"mako_vkbasalt_layer_dir={vkbasalt_layer_dir}",
        "mako_vkbasalt_enabled=0",
        "mako_flatpak_runtime=0",
        "mako_flatpak_launch=0",
        'if [ "${1##*/}" = flatpak ] && [ "${2:-}" = run ]; then',
        "    mako_flatpak_launch=1",
        "fi",
        "unset MANGOHUD",
        f"unset {VKBASALT_CONFIG_FILE_ENV}",
        f"unset {VKBASALT_CONFIG_RELOAD_ENV}",
        f"export {VKBASALT_LAYER_DISABLE_ENV}=1",
        f"unset {VKBASALT_LAYER_ENABLE_ENV}",
        f"if [ -d {shlex.quote(context.flatpak_implicit_layer_dir)} ] || "
        '[ "$mako_flatpak_launch" = 1 ]; then',
        f"    mako_implicit_layer_path={shlex.quote(context.flatpak_implicit_layer_dir)}",
        "    mako_flatpak_runtime=1",
        "    mako_spatial_scaling_manifest=\"$mako_implicit_layer_path/"
        f"{context.spatial_scaling_manifest_filename_64}\"",
        "    mako_vkbasalt_manifest=\"$mako_implicit_layer_path/"
        f"{context.vkbasalt_manifest_filename_64}\"",
        "    mako_vkbasalt_manifest32=\"$mako_implicit_layer_path/"
        f"{context.vkbasalt_manifest_filename_32}\"",
        '    mako_vkbasalt_layer_dir="$mako_implicit_layer_path"',
        "else",
        f"    mako_implicit_layer_path={shlex.quote(str(context.local_share_dir))}",
        f"    mako_spatial_scaling_manifest={spatial_scaling_manifest}",
        f"    mako_vkbasalt_manifest={vkbasalt_manifest}",
        f"    mako_vkbasalt_manifest32={vkbasalt_manifest32}",
        "fi",
        # Preserve the established Renderer -> Gamescope WSI -> spatial order.
        # Flatpak preparation stages the host's own WSI binary beside its
        # guarded manifest, so Heroic and EmuDeck use this same chain without
        # exposing or searching the host's global Vulkan layer directory.
        'if [ "$mako_renderer_enabled" = 1 ] && '
        '[ "${mako_gamescope_wsi_required:-0}" = 1 ] && '
        '[ "$mako_gamescope_wsi_session" = 1 ] && [ -r '
        f"{gamescope_wsi_manifest} ] && "
        '( [ "${mako_spatial_scaling_required:-0}" != 1 ] || '
        '[ -r "$mako_spatial_scaling_manifest" ] || '
        '[ "$mako_flatpak_launch" = 1 ] ) && '
        f'[ "${{{MAKO_LAYER_DISABLE_ENV}:-0}}" != 1 ]; then',
        '    if [ "${mako_spatial_scaling_required:-0}" = 1 ]; then',
        f"        export {MAKO_SPLIT_LAYER_CHAIN_ENV}={MAKO_SPLIT_LAYER_CHAIN_COMBINED_PIPELINE}",
        "    fi",
        f"    unset {GAMESCOPE_WSI_DISABLE_ENV}",
        f"    export {GAMESCOPE_WSI_ENABLE_ENV}=1",
        "    export NODEVICE_SELECT=1",
        "    export DISABLE_LAYER_MESA_ANTI_LAG=1",
        f"    mako_managed_instance_layers={MAKO_LAYER_NAME}:{GAMESCOPE_WSI_LAYER_NAME_64}",
        '    mako_implicit_layer_path="$mako_implicit_layer_path:$mako_gamescope_wsi_layer_dir"',
        '    if [ "${mako_spatial_scaling_required:-0}" = 1 ]; then',
        f"        unset {SPATIAL_SCALING_LAYER_DISABLE_ENV}",
        f"        export {SPATIAL_SCALING_LAYER_ENABLE_ENV}=1",
        f'        mako_managed_instance_layers="$mako_managed_instance_layers:{SPATIAL_SCALING_LAYER_NAME}"',
        '        if [ "$mako_flatpak_runtime" != 1 ]; then',
        '            mako_implicit_layer_path="$mako_implicit_layer_path:$mako_spatial_scaling_layer_dir"',
        "        fi",
        "    fi",
        'elif [ "$mako_renderer_enabled" = 1 ] && '
        '[ "${mako_gamescope_wsi_required:-0}" = 1 ] && '
        '[ "$mako_gamescope_wsi_session" != 1 ]; then',
        '    mako_gamescope_wsi_skip_log="MAKO Decky: Gamescope WSI skipped: no active Gamescope session; continuing with the managed WSI and spatial chain disabled."',
        "fi",
        'case "$mako_external_vulkan_layer" in',
        f"        {EXTERNAL_VULKAN_LAYER_MANGOHUD})",
        '            if [ "$mako_flatpak_runtime" != 1 ] && '
        f"{{ [ -r {mangohud_manifest} ] || [ -r {mangohud_manifest32} ]; }}; then",
        "                unset DISABLE_MANGOHUD",
        "                export MANGOHUD=1",
        "                export NODEVICE_SELECT=1",
        "                export DISABLE_LAYER_MESA_ANTI_LAG=1",
        '                mako_implicit_layer_path="$mako_implicit_layer_path:$mako_mangohud_layer_dir"',
        f"                if [ -r {mangohud_manifest} ]; then",
        f"                    mako_managed_external_layer={MANGOHUD_LAYER_NAME_64}",
        "                fi",
        "            fi",
        "            ;;",
        f"        {EXTERNAL_VULKAN_LAYER_VKBASALT})",
        "            if { [ -z \"$mako_vkbasalt_config\" ] || "
        "[ -r \"$mako_vkbasalt_config\" ]; } && "
        "{ [ -r \"$mako_vkbasalt_manifest\" ] || "
        "[ -r \"$mako_vkbasalt_manifest32\" ] || "
        "[ \"$mako_flatpak_launch\" = 1 ]; }; then",
        '                if [ -n "$mako_vkbasalt_config" ]; then',
        f'                    export {VKBASALT_CONFIG_FILE_ENV}="$mako_vkbasalt_config"',
        f"                    export {VKBASALT_CONFIG_RELOAD_ENV}=1",
        "                fi",
        f"                unset {VKBASALT_LAYER_DISABLE_ENV}",
        f"                export {VKBASALT_LAYER_ENABLE_ENV}=1",
        "                mako_vkbasalt_enabled=1",
        "                export NODEVICE_SELECT=1",
        "                export DISABLE_LAYER_MESA_ANTI_LAG=1",
        '                if [ "$mako_flatpak_runtime" != 1 ]; then',
        '                    mako_implicit_layer_path="$mako_implicit_layer_path:$mako_vkbasalt_layer_dir"',
        "                fi",
        '                if [ -r "$mako_vkbasalt_manifest" ] || '
        '[ "$mako_flatpak_launch" = 1 ]; then',
        f"                    mako_managed_external_layer={VKBASALT_LAYER_NAME_64}",
        "                fi",
        "            fi",
        "            ;;",
        "esac",
        'if [ -n "$mako_managed_instance_layers" ]; then',
        '    if [ -n "$mako_managed_external_layer" ]; then',
        '        mako_managed_instance_layers="$mako_managed_instance_layers:$mako_managed_external_layer"',
        "    fi",
        '    if [ -n "$mako_existing_instance_layers" ]; then',
        f'        export {VK_INSTANCE_LAYERS_ENV}="$mako_managed_instance_layers:$mako_existing_instance_layers"',
        "    else",
        f'        export {VK_INSTANCE_LAYERS_ENV}="$mako_managed_instance_layers"',
        "    fi",
        # Prevent the same manifests from joining first through their implicit
        # activation gates. The explicit list above is the only owner of the
        # managed WSI/scaling order on this supported 64-bit path.
        f"    unset {MAKO_LAYER_ENABLE_ENV}",
        f"    unset {GAMESCOPE_WSI_ENABLE_ENV}",
        f"    unset {SPATIAL_SCALING_LAYER_ENABLE_ENV}",
        '    if [ "$mako_managed_external_layer" = "'
        f'{MANGOHUD_LAYER_NAME_64}" ]; then',
        "        unset MANGOHUD",
        '    elif [ "$mako_managed_external_layer" = "'
        f'{VKBASALT_LAYER_NAME_64}" ]; then',
        f"        unset {VKBASALT_LAYER_ENABLE_ENV}",
        "    fi",
        "fi",
        "unset mako_gamescope_wsi_required",
        "unset mako_gamescope_wsi_session",
        "unset mako_spatial_scaling_required",
        "unset mako_external_vulkan_layer",
        "unset mako_gamescope_wsi_layer_dir",
        "unset mako_spatial_scaling_layer_dir",
        "unset mako_mangohud_layer_dir",
        "unset mako_vkbasalt_layer_dir",
        "unset mako_existing_instance_layers",
        "unset mako_managed_instance_layers",
        "unset mako_managed_external_layer",
        "unset mako_spatial_scaling_manifest",
        "unset mako_vkbasalt_manifest",
        "unset mako_vkbasalt_manifest32",
        f'export {VK_IMPLICIT_LAYER_PATH_ENV}="$mako_implicit_layer_path"',
        f"unset {VK_ADD_IMPLICIT_LAYER_PATH_ENV}",
        f"export {MAKO_CONFIG_ENV}={shlex.quote(str(context.config_file_path))}",
        # A direct EmuDeck/Flatpak shortcut executes this wrapper on the host.
        # Per-launch Flatpak options must override its persisted app-wide
        # preparation so the explicit managed chain and the selected bundled
        # vkBasalt profile survive into the sandbox.
        'if [ "${1##*/}" = flatpak ] && [ "${2:-}" = run ]; then',
        '    mako_flatpak_command="$1"',
        "    shift",
        '    mako_flatpak_subcommand="$1"',
        "    shift",
        '    set -- "$mako_flatpak_command" "$mako_flatpak_subcommand" '
        f'--env={VK_IMPLICIT_LAYER_PATH_ENV}="${{{VK_IMPLICIT_LAYER_PATH_ENV}}}" '
        f'--env={MAKO_CONFIG_ENV}="${{{MAKO_CONFIG_ENV}}}" '
        f'--unset-env={VK_ADD_IMPLICIT_LAYER_PATH_ENV} '
        '"$@"',
        '    if [ "$mako_renderer_enabled" = 1 ] && '
        f'[ -z "${{{VK_INSTANCE_LAYERS_ENV}:-}}" ]; then',
        '        set -- "$mako_flatpak_command" "$mako_flatpak_subcommand" '
        f'--env={MAKO_LAYER_ENABLE_ENV}=1 '
        f'--unset-env={MAKO_LAYER_DISABLE_ENV} '
        '"${@:3}"',
        '    elif [ "$mako_renderer_enabled" != 1 ]; then',
        '        set -- "$mako_flatpak_command" "$mako_flatpak_subcommand" '
        f'--unset-env={MAKO_LAYER_ENABLE_ENV} '
        f'--env={MAKO_LAYER_DISABLE_ENV}=1 '
        '"${@:3}"',
        "    fi",
        f'    if [ -n "${{{VK_INSTANCE_LAYERS_ENV}:-}}" ]; then',
        '        set -- "$mako_flatpak_command" "$mako_flatpak_subcommand" '
        f'--env={VK_INSTANCE_LAYERS_ENV}="${{{VK_INSTANCE_LAYERS_ENV}}}" '
        f'--unset-env={MAKO_LAYER_ENABLE_ENV} '
        f'--unset-env={MAKO_LAYER_DISABLE_ENV} '
        f'--unset-env={GAMESCOPE_WSI_ENABLE_ENV} '
        f'--unset-env={GAMESCOPE_WSI_DISABLE_ENV} '
        f'--unset-env={SPATIAL_SCALING_LAYER_ENABLE_ENV} '
        f'--unset-env={SPATIAL_SCALING_LAYER_DISABLE_ENV} '
        '"${@:3}"',
        "    fi",
        f'    if [ -n "${{{MAKO_SPLIT_LAYER_CHAIN_ENV}:-}}" ]; then',
        '        set -- "$mako_flatpak_command" "$mako_flatpak_subcommand" '
        f'--env={MAKO_SPLIT_LAYER_CHAIN_ENV}="${{{MAKO_SPLIT_LAYER_CHAIN_ENV}}}" '
        '"${@:3}"',
        "    fi",
        '    if [ "$mako_vkbasalt_enabled" = 1 ]; then',
        '        set -- "$mako_flatpak_command" "$mako_flatpak_subcommand" '
        f'--unset-env={VKBASALT_LAYER_DISABLE_ENV} '
        '"${@:3}"',
        f'        if [ "${{{VKBASALT_LAYER_ENABLE_ENV}:-0}}" = 1 ]; then',
        '            set -- "$mako_flatpak_command" "$mako_flatpak_subcommand" '
        f'--env={VKBASALT_LAYER_ENABLE_ENV}=1 "${{@:3}}"',
        "        fi",
        f'        if [ -n "${{{VKBASALT_CONFIG_FILE_ENV}:-}}" ]; then',
        '            set -- "$mako_flatpak_command" "$mako_flatpak_subcommand" '
        f'--env={VKBASALT_CONFIG_FILE_ENV}="${{{VKBASALT_CONFIG_FILE_ENV}}}" '
        f'--env={VKBASALT_CONFIG_RELOAD_ENV}=1 '
        '"${@:3}"',
        "        fi",
        "    fi",
        "    unset mako_flatpak_command",
        "    unset mako_flatpak_subcommand",
        "fi",
        "unset mako_vkbasalt_config",
        "unset mako_vkbasalt_enabled",
        "unset mako_renderer_required",
        "unset mako_renderer_enabled",
        "unset mako_flatpak_runtime",
        "unset mako_flatpak_launch",
        "# Heroic can discard a game's stderr. Capture opt-in engine diagnostics here instead.",
        f"mako_diagnostics_default={shlex.quote(str(diagnostics_log_path))}",
        f'if [ "${{{PRESENT_DIAGNOSTICS_ENV}:-0}}" != "0" ]; then',
        f'    mako_diagnostics_log="${{{PRESENT_DIAGNOSTICS_LOG_ENV}:-$mako_diagnostics_default}}"',
        "    mako_diagnostics_rotation_ready=1",
        f"    for mako_diagnostics_entry in {diagnostics_history_paths}; do",
        '        if [ -L "$mako_diagnostics_entry" ] || { [ -e "$mako_diagnostics_entry" ] && { [ ! -f "$mako_diagnostics_entry" ] || [ ! -O "$mako_diagnostics_entry" ]; }; }; then',
        "            mako_diagnostics_rotation_ready=0",
        "            break",
        "        fi",
        "    done",
        '    if [ "$mako_diagnostics_rotation_ready" = 1 ] && [ -f "$mako_diagnostics_log" ]; then',
        *diagnostics_rotation_lines,
        "    fi",
        (
            '    if [ "$mako_diagnostics_rotation_ready" = 1 ] && '
            '(set -C; : > "$mako_diagnostics_log") 2>/dev/null; then'
        ),
        '        exec 2>> "$mako_diagnostics_log"',
        "    fi",
        "fi",
        "unset mako_diagnostics_entry",
        'if [ -n "${mako_gamescope_wsi_skip_log:-}" ]; then',
        '    printf "%s\\n" "$mako_gamescope_wsi_skip_log" >&2',
        "fi",
        "unset mako_gamescope_wsi_skip_log",
    ]


def wrapper_profile_configuration_lines(
        profile_data: ProfileData,
        profile_settings: WrapperProfileSettings,
        metadata: ProfileMetadata,
        vkbasalt_global_config_path: Optional[Path] = None,
        vkbasalt_profile_config_dir: Optional[Path] = None,
        profile_config: Callable[
            [ProfileData, str, WrapperProfileSettings], ConfigurationData
        ] = config_for_profile,
        config_lines: Callable[
            [ConfigurationData], list[str]
        ] = script_configuration_lines,
) -> list[str]:
    """Select launcher-only settings by explicit profile or Steam app ID."""
    global_config_path = (
        vkbasalt_global_config_path
        if vkbasalt_global_config_path is not None
        else Path("/nonexistent/vkBasalt.conf")
    )
    profile_config_dir = (
        vkbasalt_profile_config_dir
        if vkbasalt_profile_config_dir is not None
        else Path("/nonexistent/mako-vkbasalt")
    )
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
        lines.extend(
            f"        {line}" for line in vkbasalt_profile_environment_lines(
                profile_name,
                config,
                global_config_path,
                profile_config_dir,
                metadata_steam_app_id(metadata, profile_name),
            )
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
    lines.extend(
        f"        {line}" for line in vkbasalt_profile_environment_lines(
            current_profile,
            fallback_config,
            global_config_path,
            profile_config_dir,
            metadata_steam_app_id(metadata, current_profile),
        )
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
        [
            *script_configuration_lines(config),
            *vkbasalt_profile_environment_lines(
                DEFAULT_PROFILE_NAME,
                config,
                context.vkbasalt_global_config_path,
                context.vkbasalt_profile_config_dir,
            ),
        ],
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
            context.vkbasalt_global_config_path,
            context.vkbasalt_profile_config_dir,
        ),
        layer_environment_lines(context),
        profile_selection_lines(
            fallback_profile,
            fallback_config,
            automatic_matching_enabled,
        ),
    )
