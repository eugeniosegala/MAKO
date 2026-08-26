#!/usr/bin/env bash
# Incrementally build the host Vulkan layer for SteamOS development.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_cache_root="${MAKO_BUILD_CACHE_ROOT:-$repo_root/build/cache}"
if [[ "$build_cache_root" != /* ]]; then
    build_cache_root="$repo_root/$build_cache_root"
fi
build_dir="${MAKO_BUILD_DIR:-$repo_root/build/steamos-dev}"
build_32_dir="${MAKO_BUILD_32_DIR:-}"
compiler="${CXX:-clang++}"
jobs="${MAKO_BUILD_JOBS:-}"
build_64_bit=true
build_32_bit=false

usage() {
    cat <<'EOF'
Usage: scripts/build-steamos-dev.sh [options]

Incrementally builds host Vulkan layers needed for native Steam-game testing.
The default builds the 64-bit layer and CLI. The build directories are retained
between runs; this does not build the Qt UI, Flatpak extensions, general test
suite, archives, or a Decky ZIP. Real-hardware licensed-model validation is
owned by the sibling MAKO-Gym repository. Set CXX to choose a compiler. ccache
is used automatically when present.

Options:
  --with-32-bit          Build both the 64-bit and 32-bit host layers.
  --32-bit-only          Build only the 32-bit host layer.
  --build-dir PATH       64-bit persistent CMake build directory.
  --build-32-dir PATH    32-bit persistent CMake build directory.
  --jobs COUNT           Parallel compile jobs.
Environment:
  MAKO_BUILD_DIR     Persistent CMake build directory (default: build/steamos-dev)
  MAKO_BUILD_32_DIR  Persistent 32-bit build directory (default: build/steamos-dev-32)
  MAKO_BUILD_JOBS    Parallel compile jobs (default: available CPUs)
  CXX                C++ compiler (default: clang++)
EOF
}

while (($#)); do
    case "$1" in
        --build-dir)
            if (($# < 2)); then
                echo "--build-dir requires a path" >&2
                exit 2
            fi
            build_dir="$2"
            shift 2
            continue
            ;;
        --jobs)
            if (($# < 2)); then
                echo "--jobs requires a positive integer" >&2
                exit 2
            fi
            jobs="$2"
            shift 2
            continue
            ;;
        --build-32-dir)
            if (($# < 2)); then
                echo "--build-32-dir requires a path" >&2
                exit 2
            fi
            build_32_dir="$2"
            shift 2
            continue
            ;;
        --with-32-bit)
            build_32_bit=true
            ;;
        --32-bit-only)
            build_64_bit=false
            build_32_bit=true
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

if [[ "$build_dir" != /* ]]; then
    build_dir="$repo_root/$build_dir"
fi
if [[ -z "$build_32_dir" ]]; then
    build_32_dir="${build_dir}-32"
elif [[ "$build_32_dir" != /* ]]; then
    build_32_dir="$repo_root/$build_32_dir"
fi

for command in cmake ninja "$compiler"; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "Required command not found: $command" >&2
        echo "Install the SteamOS build prerequisites in docs/BUILDING-FROM-SOURCE.md." >&2
        exit 1
    fi
done

if [[ -z "$jobs" ]]; then
    jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')"
fi
if ! [[ "$jobs" =~ ^[1-9][0-9]*$ ]]; then
    echo "Build job count must be a positive integer: $jobs" >&2
    exit 2
fi

if [[ "$build_32_bit" == true && ! -f /usr/include/gnu/stubs-32.h ]]; then
    echo "32-bit glibc development headers are missing: /usr/include/gnu/stubs-32.h" >&2
    echo "On SteamOS, reinstall lib32-glibc (do not use --needed):" >&2
    echo "  sudo steamos-readonly disable" >&2
    echo "  sudo pacman -S lib32-glibc" >&2
    echo "  sudo steamos-readonly enable" >&2
    exit 1
fi

compiler_launcher=""
if command -v ccache >/dev/null 2>&1; then
    export CCACHE_DIR="${CCACHE_DIR:-$build_cache_root/ccache}"
    mkdir -p "$CCACHE_DIR"
    compiler_launcher="ccache"
    echo "Using repo-local ccache: $CCACHE_DIR"
else
    echo "ccache is not installed; continuing with Ninja's incremental build cache."
fi

build_layer() {
    local architecture="$1"
    local target_build_dir="$2"
    shift 2
    local build_cli=OFF
    local build_targets=(mako-render)

    if [[ "$architecture" == "64-bit" ]]; then
        build_cli=ON
        build_targets+=(mako-cli)
    fi

    # Reconfiguring a persistent Ninja tree is cheap and picks up CMake/source
    # changes without throwing away already compiled objects.
    cmake -S "$repo_root" -B "$target_build_dir" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_COMPILER="$compiler" \
        -DCMAKE_CXX_COMPILER_LAUNCHER="$compiler_launcher" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DBUILD_TESTING=OFF \
        -DMAKO_BUILD_VK_LAYER=ON \
        -DMAKO_BUILD_UI=OFF \
        -DMAKO_BUILD_CLI="$build_cli" \
        -DMAKO_INSTALL_XDG_FILES=OFF \
        "$@"

    cmake --build "$target_build_dir" --parallel "$jobs" --target "${build_targets[@]}"

    local layer_path="$target_build_dir/mako-render/libmako-render.so"
    if [[ ! -f "$layer_path" ]]; then
        echo "Expected $architecture layer output is missing: $layer_path" >&2
        exit 1
    fi
    echo "Incremental $architecture layer build ready: $layer_path"

}

if [[ "$build_64_bit" == true ]]; then
    build_layer "64-bit" "$build_dir"
fi
if [[ "$build_32_bit" == true ]]; then
    build_layer "32-bit" "$build_32_dir" \
        -DCMAKE_CXX_FLAGS=-m32 \
        -DCMAKE_SHARED_LINKER_FLAGS=-m32 \
        -DCMAKE_INSTALL_LIBDIR=lib32 \
        -DMAKO_LAYER_MANIFEST_SUFFIX=.x86
fi
