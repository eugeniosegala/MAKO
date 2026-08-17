#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

for command in cmake; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "Required command not found: $command" >&2
        exit 1
    fi
done

build_root="$(mktemp -d "${TMPDIR:-/tmp}/mako-adaptive-benchmark.XXXXXX")"
cleanup() {
    rm -rf "$build_root"
}
trap cleanup EXIT

generator="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
    generator="Ninja"
fi

cmake -S "$repo_root" -B "$build_root" -G "$generator" \
    -DCMAKE_BUILD_TYPE=Release \
    -DMAKO_BUILD_VK_LAYER=OFF \
    -DMAKO_BUILD_UI=OFF \
    -DMAKO_BUILD_CLI=OFF \
    -DBUILD_TESTING=ON
cmake --build "$build_root" --target mako-adaptive-benchmark
"$build_root/mako-render/mako-adaptive-benchmark"
