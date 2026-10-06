#!/usr/bin/env bash
# Prepare public, pinned optimizer sources for the experimental LSFG build.
set -euo pipefail

engine_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cache_root="${MAKO_BUILD_CACHE_ROOT:-$engine_root/build/cache}"
if [[ "$cache_root" != /* ]]; then
    cache_root="$engine_root/$cache_root"
fi
tools_commit=33e02568181e3312f49a3cf33df470bf96ef293a
headers_commit=2a611a970fdbc41ac2e3e328802aed9985352dca
tools_sha=44d1005880c583fc00a0fb41c839214c68214b000ea8dcb54d352732fee600ff
headers_sha=c2225a49c3d7efa5c4f4ce4a6b42081e6ea3daca376f3353d9d7c2722d77a28a
cache="$cache_root/lsfg-fp16-tools"
source_dir="$cache/$tools_commit"
mkdir -p "$cache"

fetch_archive() {
    local project="$1" commit="$2" expected="$3" archive="$4"
    if [[ ! -f "$archive" ]]; then
        curl --fail --location --retry 3 \
            "https://codeload.github.com/KhronosGroup/$project/tar.gz/$commit" \
            --output "$archive.part" >&2
        mv -- "$archive.part" "$archive"
    fi
    printf '%s  %s\n' "$expected" "$archive" | sha256sum --check >&2
}

fetch_archive SPIRV-Tools "$tools_commit" "$tools_sha" "$cache/tools.tar.gz"
fetch_archive SPIRV-Headers "$headers_commit" "$headers_sha" "$cache/headers.tar.gz"
if [[ ! -f "$source_dir/.mako-source-pin" ]]; then
    if [[ -e "$source_dir" ]]; then
        echo "Incomplete optimizer cache: $source_dir" >&2
        exit 1
    fi
    stage="$(mktemp -d "$cache/.stage.XXXXXX")"
    trap 'rm -rf -- "$stage"' EXIT
    mkdir -p "$stage/source"
    tar -xzf "$cache/tools.tar.gz" --strip-components=1 -C "$stage/source"
    mkdir -p "$stage/source/external/spirv-headers"
    tar -xzf "$cache/headers.tar.gz" --strip-components=1 -C "$stage/source/external/spirv-headers"
    printf 'SPIRV-Tools %s\nSPIRV-Headers %s\n' "$tools_commit" "$headers_commit" > "$stage/source/.mako-source-pin"
    mv -- "$stage/source" "$source_dir"
    rm -rf -- "$stage"
    trap - EXIT
fi
printf '%s\n' "$source_dir"
