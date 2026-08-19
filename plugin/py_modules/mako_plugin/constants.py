"""
Constants for the MAKO Decky.
"""

from pathlib import Path

MAKO_ROOT = ".local/share/mako-render"
LOCAL_LIB = f"{MAKO_ROOT}/lib"
LOCAL_LIB32 = f"{MAKO_ROOT}/lib32"
VULKAN_LAYER_DIR = f"{MAKO_ROOT}/vulkan/implicit_layer.d"
USER_VULKAN_LAYER_DIR = ".local/share/vulkan/implicit_layer.d"
CONFIG_DIR = ".config/mako-render"

SCRIPT_NAME = ".local/bin/mako-run"
DIAGNOSTICS_SCRIPT_NAME = ".local/bin/mako-diagnostics"
DIAGNOSTICS_HELPER_FILENAME = "mako-diagnostics"

# Avoid persistent Gamescope presentation stalls by giving the generated-image
# acquisition path a bounded first wait. During backoff, the engine probes
# availability before scheduling inference and periodically reuses this bound
# to avoid missing the compositor's image-release window indefinitely.
PRESENT_ACQUIRE_TIMEOUT_MS = 50
CONFIG_FILENAME = "conf.toml"
# The engine reads conf.toml directly, so Decky-only launcher settings must be
# stored separately rather than adding unknown keys to an upstream profile.
WRAPPER_PROFILE_SETTINGS_FILENAME = "profile-wrapper-settings.json"
# Friendly game identity and Steam app matching for Decky's versioned profile
# model. Renderer settings remain in conf.toml, their canonical engine format.
PROFILE_METADATA_FILENAME = "profile-metadata.json"
# Bundled MAKO Renderer payload filenames. The archive name is read from the packaged
# package.json remote_binary record so the release pin has one source of truth.
LIB_FILENAME = "libmako-render.so"
MAKO_LAYER_NAME = "VK_LAYER_MAKO_render"
MAKO_LAYER_ENABLE_ENV = "ENABLE_MAKO"
MAKO_LAYER_DISABLE_ENV = "DISABLE_MAKO"
# MAKO and the public LSFG-VK releases are separate frame-generation layers.
# Loading either public identity alongside MAKO would make both layers
# intercept the same swapchain and presentation calls. Disable only those
# known competitors in the per-game launcher instead of hiding every other
# implicit Vulkan layer from the game.
COMPETING_LSFG_DISABLE_ENVS = (
    "DISABLE_LSFG",  # LSFG-VK 1.x
    "DISABLE_LSFGVK",  # LSFG-VK 2.x
)
MAKO_LAYER_BUILD_MARKER = (
    b"mako: render layer active; identity="
    b"VK_LAYER_MAKO_render; build="
)
# Decky's generated wrapper relies on the renderer understanding this
# low-priority profile selector. Keep it as a payload compatibility marker so
# an older same-name renderer cannot be installed alongside a newer wrapper
# and silently remain inactive in Heroic or an emulator Flatpak.
MAKO_PROFILE_FALLBACK_MARKER = b"MAKO_PROFILE_FALLBACK"
JSON_FILENAME = "VkLayer_MAKO_render.json"
JSON32_FILENAME = "VkLayer_MAKO_render.x86.json"
CLI_FILENAME = "mako-cli"
CLI_DIR = f"{MAKO_ROOT}/bin"

BIN_DIR = "bin"

# Flatpak uses a dedicated MAKO Renderer extension ID and mount point.
FLATPAK_EXTENSION_NAME = "org.freedesktop.Platform.VulkanLayer.makorender"
FLATPAK_EXTENSION_PREFIX = "/usr/lib/extensions/vulkan/makorender"
FLATPAK_IMPLICIT_LAYER_DIR = f"{FLATPAK_EXTENSION_PREFIX}/share/vulkan/implicit_layer.d"
# Heroic's Flatpak ships Gamescope as a separate Vulkan runtime extension.
# Add it alongside MAKO when the per-game wrapper exposes mounted extensions
# to the child compatibility environment. This preserves Gamescope's ordinary
# SDR presentation, frame limiting, and window integration without replacing
# the loader's standard implicit-layer search paths.
FLATPAK_GAMESCOPE_IMPLICIT_LAYER_DIR = (
    "/usr/lib/extensions/vulkan/gamescope/share/vulkan/implicit_layer.d"
)
FLATPAK_23_08_FILENAME = f"{FLATPAK_EXTENSION_NAME}-23.08.flatpak"
FLATPAK_24_08_FILENAME = f"{FLATPAK_EXTENSION_NAME}-24.08.flatpak"
FLATPAK_25_08_FILENAME = f"{FLATPAK_EXTENSION_NAME}-25.08.flatpak"

# Armada runs Steam through FEX and requires its host launcher to apply the
# game-specific runtime and controller configuration.
ARMADA_DEVICE_ENV = Path("/usr/libexec/armada/device-env")
ARMADA_GAME_LAUNCH = Path("/usr/libexec/armada/armada-game-launch")

STEAM_COMMON_PATH = Path("steamapps/common/Lossless Scaling")
LOSSLESS_DLL_NAME = "Lossless.dll"

ENV_MAKO_DLL_PATH = "MAKO_DLL_PATH"
ENV_XDG_DATA_HOME = "XDG_DATA_HOME"
ENV_HOME = "HOME"
