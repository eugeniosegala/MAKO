#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
gpu_quality_mode="${MAKO_GPU_QUALITY_TEST:-AUTO}"

for command in cmake ctest; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "Required command not found: $command" >&2
        exit 1
    fi
done

build_root="$(mktemp -d "${TMPDIR:-/tmp}/mako-adaptive-tests.XXXXXX")"
cleanup() {
    rm -rf "$build_root"
}
trap cleanup EXIT

generator="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
    generator="Ninja"
fi

cmake -S "$repo_root" -B "$build_root" -G "$generator" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DMAKO_BUILD_VK_LAYER=OFF \
    -DMAKO_BUILD_UI=OFF \
    -DMAKO_BUILD_CLI=ON \
    -DMAKO_GPU_QUALITY_TEST="$gpu_quality_mode" \
    -DBUILD_TESTING=ON
cmake --build "$build_root"
ctest --test-dir "$build_root" --output-on-failure

echo "MAKO Renderer tests passed; eligible AMD hardware also passed the GPU quality regression."
