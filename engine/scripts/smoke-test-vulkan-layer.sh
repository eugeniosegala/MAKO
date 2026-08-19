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
mkdir -p "$prefix" "$home_dir"
tar -xJf "$archive" -C "$prefix"

manifest="$prefix/share/vulkan/implicit_layer.d/VkLayer_MAKO_render.json"
library="$prefix/lib/libmako-render.so"
for required_file in "$manifest" "$library"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Packaged Renderer smoke test is missing: $required_file" >&2
        exit 1
    fi
done

if ! env -u DISABLE_MAKO \
        HOME="$home_dir" \
        XDG_CONFIG_HOME="$home_dir/.config" \
        XDG_DATA_HOME="$prefix/share" \
        ENABLE_MAKO=1 \
        vulkaninfo --summary >"$log_file" 2>&1; then
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

echo "Packaged MAKO Renderer Vulkan layer activated successfully on real hardware."
