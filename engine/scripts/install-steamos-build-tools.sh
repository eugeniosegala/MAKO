#!/usr/bin/env bash
# Restore the SteamOS system packages used by direct MAKO development builds.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/install-steamos-build-tools.sh --check|--install

  --check    Report missing SteamOS build commands and development headers.
  --install  Install the complete direct-build toolchain, including 32-bit,
             shader, Qt UI, and Flatpak development prerequisites.

Run as your normal SteamOS user. Installation prompts for sudo and Pacman
confirmation, preserves the original read-only state, and does not build,
deploy, or install MAKO. Portable release builds use Docker/Podman separately.
The fast SteamOS builder fetches MAKO's pinned Vulkan headers into its own
repository-local cache; the system headers serve manual builds and other tools.
EOF
}

if (($# != 1)); then
    usage >&2
    exit 2
fi
case "$1" in
    --check|--install) action="$1" ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
esac

if [[ ! -r /etc/os-release ]]; then
    echo "Cannot identify the host operating system." >&2
    exit 1
fi
# shellcheck source=/etc/os-release
source /etc/os-release
if [[ "${ID:-}" != steamos ]]; then
    echo "This installer only supports SteamOS; found ${ID:-unknown}." >&2
    exit 1
fi

check_prerequisites() {
    local missing=0 command_name header
    local commands=(git curl python3 gcc g++ clang++ cmake ninja pkg-config ccache glslangValidator spirv-val flatpak flatpak-builder)
    local headers=(
        /usr/include/gnu/stubs-64.h
        /usr/include/gnu/stubs-32.h
        /usr/include/vulkan/vulkan.h
        /usr/include/X11/X.h
        /usr/include/X11/Xlib.h
        /usr/include/xcb/xcb.h
        /usr/include/wayland-client.h
        /usr/include/GL/gl.h
        /usr/include/qt6/QtCore/qglobal.h
        /usr/include/qt6/QtQuick/QQuickItem
        /usr/include/qt6/QtShaderTools/QtShaderTools
        /usr/include/qt6/QtUiTools/QUiLoader
    )
    for command_name in "${commands[@]}"; do
        if ! command -v "$command_name" >/dev/null 2>&1; then
            echo "Missing command: $command_name" >&2
            missing=1
        fi
    done
    for header in "${headers[@]}"; do
        if [[ ! -f "$header" ]]; then
            echo "Missing header: $header" >&2
            missing=1
        fi
    done
    if ((missing)); then
        return 1
    fi
    echo "SteamOS direct-build toolchain is ready."
}

if [[ "$action" == --check ]]; then
    check_prerequisites
    exit $?
fi

for command_name in sudo pacman pacman-key steamos-readonly; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Required host command not found: $command_name" >&2
        exit 1
    fi
done
for keyring in archlinux holo; do
    if [[ ! -f "/usr/share/pacman/keyrings/$keyring.gpg" ]]; then
        echo "SteamOS Pacman keyring is missing: $keyring" >&2
        exit 1
    fi
done

# A writable root is reported as "disabled" with status 1, not a query failure.
readonly_status=0
readonly_state="$(steamos-readonly status)" || readonly_status=$?
case "$readonly_status:$readonly_state" in
    0:enabled|0:disabled|1:disabled) ;;
    *) echo "Unexpected SteamOS read-only state: $readonly_state (exit $readonly_status)" >&2; exit 1 ;;
esac

sudo -v
restore_readonly=false
finish() {
    local result=$?
    trap - EXIT
    if [[ "$restore_readonly" == true ]]; then
        if ! sudo steamos-readonly enable; then
            echo "Could not restore SteamOS read-only protection; run sudo steamos-readonly enable." >&2
            result=1
        fi
    fi
    exit "$result"
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [[ "$readonly_state" == enabled ]]; then
    restore_readonly=true
    sudo steamos-readonly disable
fi

# SteamOS image changes can preserve Pacman's package database while removing
# development files and the local signing keyring. Reinstall the complete set
# rather than trusting --needed to detect those missing files. Keep signatures
# enabled and let Pacman show its transaction for user confirmation.
sudo pacman-key --init
sudo pacman-key --populate archlinux holo
sudo pacman -S \
    glibc linux-api-headers lib32-glibc \
    git curl python gcc lib32-gcc-libs clang cmake ninja pkgconf ccache \
    glslang spirv-tools vulkan-headers vulkan-icd-loader \
    libx11 libxcb xorgproto libxau libxdmcp wayland libglvnd \
    qt6-base qt6-declarative qt6-shadertools qt6-tools \
    flatpak flatpak-builder

check_prerequisites
