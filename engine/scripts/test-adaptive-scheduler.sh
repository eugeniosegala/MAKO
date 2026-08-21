#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sanitizer_mode="${MAKO_ENABLE_SANITIZERS:-OFF}"

case "$sanitizer_mode" in
    ON|OFF) ;;
    *)
        echo "MAKO_ENABLE_SANITIZERS must be ON or OFF" >&2
        exit 2
        ;;
esac

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
    -DMAKO_BUILD_CLI=OFF \
    -DMAKO_ENABLE_SANITIZERS="$sanitizer_mode" \
    -DBUILD_TESTING=ON
cmake --build "$build_root" --target mako-portable-policy-test-binaries
ctest --test-dir "$build_root" --output-on-failure \
    -L '^portable-policy$'

echo "MAKO Renderer portable policy tests passed."
