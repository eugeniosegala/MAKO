#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="$(tr -d '[:space:]' < "$repo_root/VERSION")"
archive="${1:-$repo_root/out/MAKO-Renderer-v$version-linux.tar.xz}"

if [[ "$(uname -s)" != Linux ]]; then
    echo "The Vulkan-layer smoke test requires Linux." >&2
    exit 1
fi
for command in tar vulkaninfo; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "Required command not found: $command" >&2
        exit 1
    fi
done
if [[ ! -f "$archive" ]]; then
    echo "Renderer archive not found: $archive" >&2
    exit 1
fi

smoke_root="$(mktemp -d "${TMPDIR:-/tmp}/mako-vulkan-smoke.XXXXXX")"
cleanup() {
    rm -rf -- "$smoke_root"
}
trap cleanup EXIT

prefix="$smoke_root/prefix"
home_dir="$smoke_root/home"
log_file="$smoke_root/vulkaninfo.log"
swapchain_log="$smoke_root/vkcube.log"
mkdir -p "$prefix" "$home_dir"
tar -xJf "$archive" -C "$prefix"

manifest="$prefix/share/vulkan/implicit_layer.d/VkLayer_MAKO_render.json"
library="$prefix/lib/libmako-render.so"
launcher="$prefix/bin/mako-launch"
for required_file in "$manifest" "$library" "$launcher"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Packaged Renderer smoke test is missing: $required_file" >&2
        exit 1
    fi
done

if ! env -u DISABLE_MAKO \
        HOME="$home_dir" \
        XDG_CONFIG_HOME="$home_dir/.config" \
        XDG_DATA_HOME="$prefix/share" \
        VK_LOADER_DEBUG=layer \
        "$launcher" vulkaninfo --summary >"$log_file" 2>&1; then
    cat "$log_file" >&2
    echo "vulkaninfo failed while the packaged MAKO Renderer layer was enabled." >&2
    exit 1
fi

activation_marker="MAKO Renderer: render layer active; identity=VK_LAYER_MAKO_render; build=$version"
if ! grep -Fq "$activation_marker" "$log_file"; then
    cat "$log_file" >&2
    echo "The Vulkan loader completed without MAKO Renderer's activation marker." >&2
    exit 1
fi

if ! grep -Fq 'Insert instance layer "VK_LAYER_MAKO_render"' "$log_file"; then
    cat "$log_file" >&2
    echo "The Vulkan loader discovered MAKO Renderer but did not insert it into the instance chain." >&2
    exit 1
fi

if grep -Eq "Failed to find 'vkGet(Instance|Device)ProcAddr'.*mako-render|Skipping layer.*mako-render|requested layer VK_LAYER_MAKO_render was loaded but was not found" "$log_file"; then
    cat "$log_file" >&2
    echo "The Vulkan loader reported a MAKO Renderer entrypoint or activation failure." >&2
    exit 1
fi

# When the host exposes a graphical compositor, cover the dormant no-profile
# path through real swapchain creation and presentation as well as instance and
# device creation. Headless CI still retains the vulkaninfo hardware gate.
if command -v vkcube >/dev/null 2>&1 &&
        [[ -n "${WAYLAND_DISPLAY:-}${DISPLAY:-}" ]]; then
    if ! env -u DISABLE_MAKO \
            HOME="$home_dir" \
            XDG_CONFIG_HOME="$home_dir/.config" \
            XDG_DATA_HOME="$prefix/share" \
            VK_LOADER_DEBUG=layer \
            "$launcher" vkcube --c 2 --suppress_popups >"$swapchain_log" 2>&1; then
        cat "$swapchain_log" >&2
        echo "vkcube failed while exercising the packaged MAKO Renderer swapchain path." >&2
        exit 1
    fi
    if ! grep -Fq 'Insert instance layer "VK_LAYER_MAKO_render"' "$swapchain_log"; then
        cat "$swapchain_log" >&2
        echo "The swapchain smoke test did not insert MAKO Renderer." >&2
        exit 1
    fi
fi

echo "Packaged MAKO Renderer Vulkan layer activated successfully on real hardware."
