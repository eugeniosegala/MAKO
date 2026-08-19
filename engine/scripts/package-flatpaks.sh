#!/usr/bin/env bash
# Build self-contained Flatpak bundles for MAKO Renderer.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
monorepo_root="$(cd "$repo_root/.." && pwd)"
build_cache_root="${MAKO_BUILD_CACHE_ROOT:-$repo_root/build/cache}"
build_work_root="${MAKO_BUILD_WORK_ROOT:-$repo_root/build/work}"
if [[ "$build_cache_root" != /* ]]; then
    build_cache_root="$repo_root/$build_cache_root"
fi
if [[ "$build_work_root" != /* ]]; then
    build_work_root="$repo_root/$build_work_root"
fi
version="$(tr -d '[:space:]' < "$repo_root/VERSION")"
default_output="$repo_root/out/MAKO-Renderer-v$version-flatpaks.tar.xz"
output_path="${1:-$default_output}"

if [[ "$output_path" != /* ]]; then
    output_path="$PWD/$output_path"
fi

if [[ -z "$version" ]]; then
    echo "VERSION must contain a release version." >&2
    exit 1
fi

if [[ "$(uname -s)" != "Linux" ]]; then
    if ! command -v docker >/dev/null 2>&1; then
        echo "Flatpak packaging needs Linux. Install Docker Desktop or run this script on Linux." >&2
        exit 1
    fi

    case "$output_path" in
        "$repo_root"/*) output_relative="${output_path#"$repo_root"/}" ;;
        *)
            echo "On non-Linux hosts, the output path must be inside this repository." >&2
            exit 1
            ;;
    esac

    echo "Using local linux/amd64 Docker Flatpak packaging environment..."
    # Flatpak-builder starts a nested Bubblewrap sandbox. Docker's default seccomp
    # profile blocks the filter setup needed by that nested sandbox on Docker Desktop.
    exec docker run --rm --privileged --security-opt seccomp=unconfined --platform linux/amd64 \
        -v "mako-flatpak-cache:/cache" \
        -e MAKO_DISABLE_BWRAP_SECCOMP=1 \
        -e MAKO_FLATPAK_CACHE_ROOT=/cache \
        -e MAKO_FLATPAK_WORK_ROOT=/cache \
        -v "$monorepo_root:/workspace" \
        -w /workspace/engine \
        ubuntu:24.04 \
        bash -lc '
            set -euo pipefail
            export DEBIAN_FRONTEND=noninteractive
            sed -i "s|http://|https://|g" /etc/apt/sources.list.d/ubuntu.sources
            # Minimal Ubuntu images do not contain a CA bundle. APT still
            # verifies signed Ubuntu repository metadata during this one-time
            # TLS bootstrap; subsequent downloads use normal certificate checks.
            if [[ ! -s /etc/ssl/certs/ca-certificates.crt ]]; then
                apt-get -o Acquire::https::Verify-Peer=false update -qq
                apt-get -o Acquire::https::Verify-Peer=false install -y -qq ca-certificates
            fi
            apt-get update -qq
            apt-get install -y -qq ca-certificates flatpak flatpak-builder xz-utils
            scripts/package-flatpaks.sh "/workspace/engine/'"$output_relative"'"
        '
fi

for command in flatpak flatpak-builder strings tar; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "Required command not found: $command" >&2
        exit 1
    fi
done

verify_elf_class() {
    local path="$1"
    local expected="$2"
    local actual
    actual="$(od -An -t u1 -j 4 -N 1 "$path" | tr -d '[:space:]')"
    if [[ "$actual" != "$expected" ]]; then
        echo "Flatpak packaging failed: $path has unexpected ELF class byte $actual" >&2
        exit 1
    fi
}

# Docker Desktop's Linux VM may not expose CONFIG_SECCOMP_FILTER to nested
# Bubblewrap instances. This is only set by the privileged local Docker build
# above; normal Linux builds retain Flatpak's default sandboxing.
if [[ "${MAKO_DISABLE_BWRAP_SECCOMP:-0}" == "1" ]]; then
    export FLATPAK_BWRAP="$repo_root/scripts/bwrap-no-seccomp.sh"
fi

work_root="${MAKO_FLATPAK_WORK_ROOT:-${MAKO_FLATPAK_TMP_ROOT:-$build_work_root/flatpak}}"
if [[ "$work_root" != /* ]]; then
    work_root="$repo_root/$work_root"
fi
mkdir -p "$work_root"
build_root="$(mktemp -d "$work_root/mako-flatpak-package.XXXXXX")"
cleanup() {
    rm -rf "$build_root"
}
trap cleanup EXIT

# Keep downloaded SDKs outside the disposable staging tree.  This directory is
# ignored by Git and makes repeated native Linux release builds incremental;
# Docker builds continue to use the explicitly configured /cache volume.
cache_root="${MAKO_FLATPAK_CACHE_ROOT:-$build_cache_root/flatpak}"
if [[ "$cache_root" != /* ]]; then
    cache_root="$repo_root/$cache_root"
fi
export HOME="$cache_root/home"
export XDG_CACHE_HOME="$cache_root/cache"
export XDG_CONFIG_HOME="$cache_root/config"
export XDG_DATA_HOME="$cache_root/data"

mkdir -p "$HOME" "$XDG_CACHE_HOME" "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$(dirname "$output_path")"
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo

bundle_dir="$build_root/bundles"
repo_dir="$build_root/repo"
mkdir -p "$bundle_dir" "$repo_dir"

extension_id="org.freedesktop.Platform.VulkanLayer.makorender"
for runtime_version in 23.08 24.08 25.08; do
    manifest="$repo_root/dist/flatpak/mako-render/$extension_id"_"$runtime_version.yml"
    build_dir="$build_root/build-$runtime_version"
    bundle="$bundle_dir/$extension_id-$runtime_version.flatpak"

    if [[ ! -f "$manifest" ]]; then
        echo "Missing Flatpak manifest: $manifest" >&2
        exit 1
    fi

    echo "Building MAKO Renderer Flatpak runtime extension $runtime_version..."
    flatpak-builder --force-clean --user --install-deps-from=flathub \
        --state-dir="$build_root/state-$runtime_version" \
        --repo="$repo_dir" "$build_dir" "$manifest"

    # Verify both layer architectures before bundling. Flatpak applications may
    # launch either a 64-bit or genuine 32-bit Vulkan process, and each process
    # must find a manifest and library with matching bitness.
    for required_path in \
        "files/share/vulkan/implicit_layer.d/VkLayer_MAKO_render.json" \
        "files/share/vulkan/implicit_layer.d/VkLayer_MAKO_render.x86.json" \
        "files/share/doc/mako-render/LICENSE.md" \
        "files/lib64/libmako-render.so" \
        "files/lib/i386-linux-gnu/libmako-render.so"; do
        if [[ ! -f "$build_dir/$required_path" ]]; then
            echo "Flatpak packaging failed: missing $required_path for $runtime_version" >&2
            exit 1
        fi
    done

    verify_elf_class "$build_dir/files/lib64/libmako-render.so" 2
    verify_elf_class "$build_dir/files/lib/i386-linux-gnu/libmako-render.so" 1

    for layer_binary in \
            "$build_dir/files/lib64/libmako-render.so" \
            "$build_dir/files/lib/i386-linux-gnu/libmako-render.so"; do
        if ! strings "$layer_binary" |
                grep -F "MAKO Renderer: render layer active; identity=VK_LAYER_MAKO_render; build=$version" >/dev/null; then
            echo "Flatpak packaging failed: layer build identity is missing for $runtime_version" >&2
            exit 1
        fi
        if ! strings "$layer_binary" | grep -F "MAKO_PROFILE_FALLBACK" >/dev/null; then
            echo "Flatpak packaging failed: profile-fallback wrapper protocol is missing for $runtime_version" >&2
            exit 1
        fi
    done

    manifest64="$build_dir/files/share/vulkan/implicit_layer.d/VkLayer_MAKO_render.json"
    manifest32="$build_dir/files/share/vulkan/implicit_layer.d/VkLayer_MAKO_render.x86.json"

    if ! grep -Fq "/usr/lib/extensions/vulkan/makorender/lib64/libmako-render.so" \
        "$manifest64"; then
        echo "Flatpak packaging failed: 64-bit manifest path is incorrect for $runtime_version" >&2
        exit 1
    fi
    if ! grep -Fq '"library_arch": "64"' \
        "$manifest64"; then
        echo "Flatpak packaging failed: 64-bit manifest architecture is incorrect for $runtime_version" >&2
        exit 1
    fi
    if ! grep -Fq "/usr/lib/extensions/vulkan/makorender/lib/i386-linux-gnu/libmako-render.so" \
        "$manifest32"; then
        echo "Flatpak packaging failed: 32-bit manifest path is incorrect for $runtime_version" >&2
        exit 1
    fi
    if ! grep -Fq '"library_arch": "32"' \
        "$manifest32"; then
        echo "Flatpak packaging failed: 32-bit manifest architecture is incorrect for $runtime_version" >&2
        exit 1
    fi
    for manifest in "$manifest64" "$manifest32"; do
        if ! grep -Fq '"name": "VK_LAYER_MAKO_render"' "$manifest" ||
                ! grep -Fq '"ENABLE_MAKO": "1"' "$manifest" ||
                ! grep -Fq '"DISABLE_MAKO": "1"' "$manifest"; then
            echo "Flatpak packaging failed: MAKO Renderer layer gating is incorrect for $runtime_version" >&2
            exit 1
        fi
    done

    flatpak build-bundle "$repo_dir" "$bundle" "$extension_id" "$runtime_version" --runtime

    if [[ ! -s "$bundle" ]]; then
        echo "Flatpak packaging failed: missing $bundle" >&2
        exit 1
    fi

    # A runtime extension is normalized from `lib` to `lib64` when Flatpak
    # deploys it. Verify the *installed bundle*, not only the build staging
    # directory, so the manifest cannot point at a path that is absent at run
    # time.
    flatpak install --user --noninteractive "$bundle" >/dev/null
    deployed_dir="$(flatpak info --user --show-location "$extension_id//$runtime_version")"
    deployed_manifest64="$deployed_dir/files/share/vulkan/implicit_layer.d/VkLayer_MAKO_render.json"
    deployed_manifest32="$deployed_dir/files/share/vulkan/implicit_layer.d/VkLayer_MAKO_render.x86.json"

    if [[ ! -f "$deployed_dir/files/lib64/libmako-render.so" ]]; then
        echo "Flatpak packaging failed: deployed 64-bit library is missing for $runtime_version" >&2
        exit 1
    fi
    if [[ ! -f "$deployed_dir/files/lib/i386-linux-gnu/libmako-render.so" ]]; then
        echo "Flatpak packaging failed: deployed 32-bit library is missing for $runtime_version" >&2
        exit 1
    fi
    if [[ ! -f "$deployed_dir/files/share/doc/mako-render/LICENSE.md" ]]; then
        echo "Flatpak packaging failed: deployed license is missing for $runtime_version" >&2
        exit 1
    fi

    verify_elf_class "$deployed_dir/files/lib64/libmako-render.so" 2
    verify_elf_class "$deployed_dir/files/lib/i386-linux-gnu/libmako-render.so" 1

    for layer_binary in \
            "$deployed_dir/files/lib64/libmako-render.so" \
            "$deployed_dir/files/lib/i386-linux-gnu/libmako-render.so"; do
        if ! strings "$layer_binary" |
                grep -F "MAKO Renderer: render layer active; identity=VK_LAYER_MAKO_render; build=$version" >/dev/null; then
            echo "Flatpak packaging failed: deployed layer build identity is missing for $runtime_version" >&2
            exit 1
        fi
        if ! strings "$layer_binary" | grep -F "MAKO_PROFILE_FALLBACK" >/dev/null; then
            echo "Flatpak packaging failed: deployed profile-fallback wrapper protocol is missing for $runtime_version" >&2
            exit 1
        fi
    done

    if ! grep -Fq "/usr/lib/extensions/vulkan/makorender/lib64/libmako-render.so" \
        "$deployed_manifest64"; then
        echo "Flatpak packaging failed: deployed 64-bit manifest path is incorrect for $runtime_version" >&2
        exit 1
    fi
    if ! grep -Fq '"library_arch": "64"' "$deployed_manifest64"; then
        echo "Flatpak packaging failed: deployed 64-bit manifest architecture is incorrect for $runtime_version" >&2
        exit 1
    fi
    if ! grep -Fq "/usr/lib/extensions/vulkan/makorender/lib/i386-linux-gnu/libmako-render.so" \
        "$deployed_manifest32"; then
        echo "Flatpak packaging failed: deployed 32-bit manifest path is incorrect for $runtime_version" >&2
        exit 1
    fi
    if ! grep -Fq '"library_arch": "32"' "$deployed_manifest32"; then
        echo "Flatpak packaging failed: deployed 32-bit manifest architecture is incorrect for $runtime_version" >&2
        exit 1
    fi
done

# The source directory is mounted into Flatpak-builder as a local source. Some
# builder versions remove ignored output directories while cleaning the source
# staging area, so recreate the destination immediately before writing it.
mkdir -p "$(dirname "$output_path")"
tar -C "$bundle_dir" -cJf "$output_path" .
echo "Created and verified: $output_path"
echo "Version: $version"
