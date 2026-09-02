"""
Constants for the MAKO Decky.
"""

from pathlib import Path
from typing import NamedTuple

from shared_config import (
    EXTERNAL_VULKAN_LAYER_MANGOHUD,
    EXTERNAL_VULKAN_LAYER_VKBASALT,
    MAKO_WRAPPER_RELATIVE_PATH,
    PER_GAME_WRAPPER_FLATPAK_APP_IDS,
    SUPPORTED_FLATPAK_RUNTIME_VERSIONS,
)

from .package_paths import PLUGIN_ROOT


PER_GAME_WRAPPER_FLATPAK_APPS = frozenset(PER_GAME_WRAPPER_FLATPAK_APP_IDS)

MAKO_ROOT = ".local/share/mako-render"
LOCAL_LIB = f"{MAKO_ROOT}/lib"
LOCAL_LIB32 = f"{MAKO_ROOT}/lib32"
VULKAN_LAYER_DIR = f"{MAKO_ROOT}/vulkan/implicit_layer.d"
SPATIAL_SCALING_LAYER_DIR = f"{MAKO_ROOT}/vulkan/spatial_scaling.d"
GAMESCOPE_WSI_COMPATIBILITY_LAYER_DIR = (
    f"{MAKO_ROOT}/vulkan/gamescope_wsi_compatibility.d"
)
MANGOHUD_LAYER_DIR = f"{MAKO_ROOT}/vulkan/mangohud.d"
VKBASALT_LAYER_DIR = f"{MAKO_ROOT}/vulkan/vkbasalt.d"
USER_VULKAN_LAYER_DIR = ".local/share/vulkan/implicit_layer.d"
CONFIG_DIR = ".config/mako-render"
RUNTIME_STATE_DIRNAME = "runtime-state"

SCRIPT_NAME = MAKO_WRAPPER_RELATIVE_PATH
DIAGNOSTICS_SCRIPT_NAME = ".local/bin/mako-diagnostics"
DIAGNOSTICS_HELPER_FILENAME = Path(DIAGNOSTICS_SCRIPT_NAME).name
ACTIVE_RENDERER_STATE_FILENAME = "active-renderer.json"
ACTIVE_RENDERER_STATE_SCHEMA_VERSION = 1
ACTIVE_RENDERER_OWNER_DECKY = "decky"
ACTIVE_RENDERER_OWNER_STANDALONE = "standalone"
STANDALONE_INSTALLER_STATE_RELATIVE_PATH = (
    f"{MAKO_ROOT}/installer/installed-files.sha256"
)

# Avoid persistent Gamescope presentation stalls by giving the generated-image
# acquisition path a bounded first wait. During backoff, the engine probes
# availability before scheduling inference and periodically reuses this bound
# to avoid missing the compositor's image-release window indefinitely.
PRESENT_ACQUIRE_TIMEOUT_MS = 50
PRESENT_ACQUIRE_TIMEOUT_ENV = "MAKO_PRESENT_ACQUIRE_TIMEOUT_MS"
PRESENT_DIAGNOSTICS_ENV = "MAKO_PRESENT_DIAGNOSTICS"
PRESENT_DIAGNOSTICS_LOG_ENV = "MAKO_PRESENT_DIAGNOSTICS_LOG"
PRESENT_DIAGNOSTICS_LOG_FILENAME = "present-diagnostics.log"
PRESENT_DIAGNOSTICS_RETAINED_SESSION_COUNT = 5
MAKO_PROFILE_ENV = "MAKO_PROFILE"
MAKO_PROFILE_FALLBACK_ENV = "MAKO_PROFILE_FALLBACK"
STEAM_APP_ID_ENV_KEYS = (
    "SteamAppId",
    "SteamGameId",
    "STEAM_COMPAT_APP_ID",
)
STEAM_DECK_MODE_ENV = "SteamDeck"
ZINK_GLX_VENDOR_ENV = "__GLX_VENDOR_LIBRARY_NAME"
ZINK_MESA_LOADER_ENV = "MESA_LOADER_DRIVER_OVERRIDE"
ZINK_GALLIUM_DRIVER_ENV = "GALLIUM_DRIVER"
ZINK_GLX_VENDOR_VALUE = "mesa"
ZINK_DRIVER_VALUE = "zink"
SDL_AUDIO_DRIVER_ENV = "SDL_AUDIODRIVER"
SDL_AUDIO_DRIVER_ALSA_VALUE = "alsa"
WINE_DLL_OVERRIDES_ENV = "WINEDLLOVERRIDES"
CONFIG_FILENAME = "conf.toml"
# The engine reads conf.toml directly, so Decky-only launcher settings must be
# stored separately rather than adding unknown keys to an upstream profile.
WRAPPER_PROFILE_SETTINGS_FILENAME = "profile-wrapper-settings.json"
# Friendly game identity and Steam app matching for Decky's versioned profile
# model. Renderer settings remain in conf.toml, their canonical engine format.
PROFILE_METADATA_FILENAME = "profile-metadata.json"
# Bundled MAKO Renderer payload filenames. Published packages read the archive
# identity from remote_binary; self-contained local packages use
# bundled_renderer so Decky Loader does not attempt an impossible download.
LIB_FILENAME = "libmako-render.so"
SPATIAL_SCALING_LIB_FILENAME = "libmako-render-scaling.so"
MAKO_LAYER_NAME = "VK_LAYER_MAKO_render"
MAKO_LAYER_ENABLE_ENV = "ENABLE_MAKO"
MAKO_LAYER_DISABLE_ENV = "DISABLE_MAKO"
SPATIAL_SCALING_LAYER_NAME = "VK_LAYER_MAKO_spatial_scaling"
SPATIAL_SCALING_LAYER_ENABLE_ENV = "ENABLE_MAKO_SPATIAL_SCALING"
SPATIAL_SCALING_LAYER_DISABLE_ENV = "DISABLE_MAKO_SPATIAL_SCALING"
# Internal ownership boundary for Decky's split application layer chain. The
# standalone Renderer keeps its established combined-library behavior unless
# its launcher deliberately opts into the same topology.
MAKO_SPLIT_LAYER_CHAIN_ENV = "MAKO_SPLIT_LAYER_CHAIN"
MAKO_SPLIT_LAYER_CHAIN_COMBINED_PIPELINE = "2"
# Process-start presentation policy. Keep native wrappers and direct Flatpak
# overrides on the same supported SDR/WSI-isolated boundary.
GAMESCOPE_WSI_DISABLE_ENV = "DISABLE_GAMESCOPE_WSI"
GAMESCOPE_WSI_ENABLE_ENV = "ENABLE_GAMESCOPE_WSI"
GAMESCOPE_WSI_LAYER_NAME_64 = "VK_LAYER_FROG_gamescope_wsi_x86_64"
GAMESCOPE_WAYLAND_DISPLAY_ENV = "GAMESCOPE_WAYLAND_DISPLAY"
WAYLAND_DISPLAY_ENV = "WAYLAND_DISPLAY"
HDR_EXPOSURE_DISABLE_ENV = "MAKO_DISABLE_HDR_EXPOSURE"
DXVK_HDR_ENV = "DXVK_HDR"
# MAKO and the public LSFG-VK releases are separate frame-generation layers.
# Loading either public identity alongside MAKO would make both layers
# intercept the same swapchain and presentation calls. Disable only those
# known competitors in the per-game launcher as defence in depth. They remain
# excluded when an explicitly selected external tool temporarily admits the
# guarded system manifest directory alongside MAKO's private directory.
COMPETING_LSFG_DISABLE_ENVS = (
    "DISABLE_LSFG",  # LSFG-VK 1.x
    "DISABLE_LSFGVK",  # LSFG-VK 2.x
)
HOST_SYSTEM_IMPLICIT_LAYER_DIR = Path("/usr/share/vulkan/implicit_layer.d")
GAMESCOPE_WSI_MANIFEST_FILENAME_64 = "VkLayer_FROG_gamescope_wsi.x86_64.json"
GAMESCOPE_WSI_LIBRARY_FILENAME_64 = (
    "libVkLayer_FROG_gamescope_wsi_x86_64.so"
)
MANGOHUD_MANIFEST_FILENAME_64 = "MangoHud.x86_64.json"
MANGOHUD_MANIFEST_FILENAME_32 = "MangoHud.x86.json"
MANGOHUD_LAYER_NAME_64 = "VK_LAYER_MANGOHUD_overlay_x86_64"
MANGOHUD_LAYER_NAME_32 = "VK_LAYER_MANGOHUD_overlay_x86"
VKBASALT_MANIFEST_FILENAMES_64 = ("vkBasalt.json", "vkBasalt.x86_64.json")
VKBASALT_MANIFEST_FILENAME_64 = VKBASALT_MANIFEST_FILENAMES_64[0]
VKBASALT_MANIFEST_FILENAMES_32 = ("vkBasalt.x86.json",)
VKBASALT_MANIFEST_FILENAME_32 = VKBASALT_MANIFEST_FILENAMES_32[0]
VKBASALT_LAYER_NAME_64 = "VK_LAYER_VKBASALT_post_processing"
VKBASALT_LAYER_NAME_32 = VKBASALT_LAYER_NAME_64
EXTERNAL_VULKAN_LAYER_ENV = "MAKO_EXTERNAL_VULKAN_LAYER"
MAKO_LAYER_BUILD_MARKER = (
    f"MAKO Renderer: render layer active; identity={MAKO_LAYER_NAME}; build="
).encode("ascii")
SPATIAL_SCALING_LAYER_BUILD_MARKER = (
    "MAKO Renderer: render layer active; "
    f"identity={SPATIAL_SCALING_LAYER_NAME}; build="
).encode("ascii")
# Decky's generated wrapper relies on the renderer understanding this
# low-priority profile selector. Keep it as a payload compatibility marker so
# an older same-name renderer cannot be installed alongside a newer wrapper
# and silently remain inactive in Heroic or an emulator Flatpak.
MAKO_PROFILE_FALLBACK_MARKER = MAKO_PROFILE_FALLBACK_ENV.encode("ascii")
JSON_FILENAME = "VkLayer_MAKO_render.json"
JSON32_FILENAME = "VkLayer_MAKO_render.x86.json"
SPATIAL_SCALING_JSON_FILENAME = "VkLayer_MAKO_spatial_scaling.json"
SPATIAL_SCALING_JSON32_FILENAME = (
    "VkLayer_MAKO_spatial_scaling.x86.json"
)
CLI_FILENAME = "mako-cli"
CLI_DIR = f"{MAKO_ROOT}/bin"

# The standalone uninstaller mirrors this fixed list so removing MAKO Renderer
# also removes a Decky-supplied native payload. A cross-component contract test
# keeps the independently packaged Python and shell implementations aligned.
DECKY_NATIVE_RENDERER_RELATIVE_PATHS = (
    f"{LOCAL_LIB}/{LIB_FILENAME}",
    f"{LOCAL_LIB32}/{LIB_FILENAME}",
    f"{LOCAL_LIB}/{SPATIAL_SCALING_LIB_FILENAME}",
    f"{LOCAL_LIB32}/{SPATIAL_SCALING_LIB_FILENAME}",
    f"{VULKAN_LAYER_DIR}/{JSON_FILENAME}",
    f"{VULKAN_LAYER_DIR}/{JSON32_FILENAME}",
    f"{SPATIAL_SCALING_LAYER_DIR}/{SPATIAL_SCALING_JSON_FILENAME}",
    f"{SPATIAL_SCALING_LAYER_DIR}/{SPATIAL_SCALING_JSON32_FILENAME}",
    f"{GAMESCOPE_WSI_COMPATIBILITY_LAYER_DIR}/{GAMESCOPE_WSI_MANIFEST_FILENAME_64}",
    f"{GAMESCOPE_WSI_COMPATIBILITY_LAYER_DIR}/{GAMESCOPE_WSI_LIBRARY_FILENAME_64}",
    f"{MANGOHUD_LAYER_DIR}/{MANGOHUD_MANIFEST_FILENAME_64}",
    f"{MANGOHUD_LAYER_DIR}/{MANGOHUD_MANIFEST_FILENAME_32}",
    f"{VKBASALT_LAYER_DIR}/{VKBASALT_MANIFEST_FILENAME_64}",
    f"{VKBASALT_LAYER_DIR}/{VKBASALT_MANIFEST_FILENAME_32}",
    f"{CLI_DIR}/{CLI_FILENAME}",
    f"{MAKO_ROOT}/installed-engine.json",
    f"{MAKO_ROOT}/{ACTIVE_RENDERER_STATE_FILENAME}",
    MAKO_WRAPPER_RELATIVE_PATH,
    DIAGNOSTICS_SCRIPT_NAME,
    f"{USER_VULKAN_LAYER_DIR}/{JSON_FILENAME}",
    f"{USER_VULKAN_LAYER_DIR}/{JSON32_FILENAME}",
)

BIN_DIR = "bin"

# Flatpak uses a dedicated MAKO Renderer extension ID and mount point.
FLATPAK_EXTENSION_NAME = "org.freedesktop.Platform.VulkanLayer.makorender"
FLATPAK_HOST_ARCHITECTURE = "x86_64"
FLATPAK_EXTENSION_PREFIX = "/usr/lib/extensions/vulkan/makorender"
FLATPAK_IMPLICIT_LAYER_DIR = f"{FLATPAK_EXTENSION_PREFIX}/share/vulkan/implicit_layer.d"


class FlatpakRuntimeBundle(NamedTuple):
    """Identity of one bundled Freedesktop Vulkan-layer runtime."""

    filename: str
    extension_id: str


FLATPAK_RUNTIME_BUNDLES = {
    version: FlatpakRuntimeBundle(
        filename=f"{FLATPAK_EXTENSION_NAME}-{version}.flatpak",
        extension_id=(
            f"{FLATPAK_EXTENSION_NAME}/{FLATPAK_HOST_ARCHITECTURE}/{version}"
        ),
    )
    for version in SUPPORTED_FLATPAK_RUNTIME_VERSIONS
}

# Retain established imports while deriving every filename from the ordered
# runtime descriptor above.
FLATPAK_23_08_FILENAME = FLATPAK_RUNTIME_BUNDLES["23.08"].filename
FLATPAK_24_08_FILENAME = FLATPAK_RUNTIME_BUNDLES["24.08"].filename
FLATPAK_25_08_FILENAME = FLATPAK_RUNTIME_BUNDLES["25.08"].filename

MAKO_CONFIG_ENV = "MAKO_CONFIG"
VK_INSTANCE_LAYERS_ENV = "VK_INSTANCE_LAYERS"
VK_IMPLICIT_LAYER_PATH_ENV = "VK_IMPLICIT_LAYER_PATH"
VK_ADD_IMPLICIT_LAYER_PATH_ENV = "VK_ADD_IMPLICIT_LAYER_PATH"

# Armada runs Steam through FEX and requires its host launcher to apply the
# game-specific runtime and controller configuration.
ARMADA_DEVICE_ENV = Path("/usr/libexec/armada/device-env")
ARMADA_GAME_LAUNCH = Path("/usr/libexec/armada/armada-game-launch")

LOSSLESS_SCALING_DIRECTORY = Path("Lossless Scaling")
LOSSLESS_DLL_NAME = "Lossless.dll"
STEAM_COMMON_PATH = Path("steamapps/common") / LOSSLESS_SCALING_DIRECTORY

ENV_MAKO_DLL_PATH = "MAKO_DLL_PATH"
ENV_XDG_DATA_HOME = "XDG_DATA_HOME"
ENV_HOME = "HOME"
