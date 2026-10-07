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
experimental_lsfg_fp16=OFF

usage() {
    cat <<'EOF'
Usage: scripts/build-steamos-dev.sh [options]

Incrementally builds host Vulkan layers needed for native Steam-game testing.
The default builds the 64-bit layer and CLI. The build directories are retained
between runs; this does not build the Qt UI, Flatpak extensions, general test
suite, archives, or a Decky ZIP. Real-hardware licensed-model validation is
owned by the sibling MAKO Gym repository. Set CXX to choose a compiler. ccache
is used automatically when present. The shared Vulkan-Headers revision is
cached under build/cache and checked against the package presentation baseline.

Options:
  --with-32-bit          Build both the 64-bit and 32-bit host layers.
  --32-bit-only          Build only the 32-bit host layer.
  --build-dir PATH       64-bit persistent CMake build directory.
  --build-32-dir PATH    32-bit persistent CMake build directory.
  --jobs COUNT           Parallel compile jobs.
  --experimental-lsfg-fp16  Build the unqualified FP32-to-FP16 LSFG fallback.
                            Uses the existing FP16 setting; adds no user toggle.
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
        --experimental-lsfg-fp16)
            experimental_lsfg_fp16=ON
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
        echo "Run scripts/install-steamos-build-tools.sh --install on the SteamOS host." >&2
        exit 1
    fi
done

for header in \
    /usr/include/gnu/stubs-64.h \
    /usr/include/X11/X.h \
    /usr/include/X11/Xlib.h \
    /usr/include/xcb/xcb.h; do
    if [[ ! -f "$header" ]]; then
        echo "Required SteamOS development header not found: $header" >&2
        echo "Run scripts/install-steamos-build-tools.sh --install on the SteamOS host." >&2
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
    echo "Run scripts/install-steamos-build-tools.sh --install on the SteamOS host." >&2
    exit 1
fi

vulkan_headers_revision="$(tr -d '[:space:]' < "$repo_root/vulkan-headers-revision.txt")"
if [[ ! "$vulkan_headers_revision" =~ ^(vulkan-sdk-|v)[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Invalid shared Vulkan-Headers revision: $vulkan_headers_revision" >&2
    exit 1
fi
vulkan_headers_cache="$build_cache_root/vulkan-headers"
vulkan_headers_source="$vulkan_headers_cache/$vulkan_headers_revision"
# SDK branches can advance. Resolve them afresh and cache by commit; release
# tags reuse their existing checkout after the first fetch.
if [[ "$vulkan_headers_revision" == vulkan-sdk-* ||
      ! -f "$vulkan_headers_source/include/vulkan/vulkan_core.h" ]]; then
    if ! command -v git >/dev/null 2>&1; then
        echo "Git is required to fetch the shared Vulkan-Headers revision." >&2
        exit 1
    fi
    mkdir -p "$vulkan_headers_cache"
    vulkan_headers_stage="$(mktemp -d "$vulkan_headers_cache/.stage.XXXXXX")"
    trap 'rm -rf -- "$vulkan_headers_stage"' EXIT
    git clone --depth=1 --branch "$vulkan_headers_revision" \
        https://github.com/KhronosGroup/Vulkan-Headers.git \
        "$vulkan_headers_stage/source"
    resolved_vulkan_headers_commit="$(git -C "$vulkan_headers_stage/source" rev-parse HEAD)"
    if [[ "$vulkan_headers_revision" == vulkan-sdk-* ]]; then
        vulkan_headers_source="$vulkan_headers_cache/$vulkan_headers_revision.$resolved_vulkan_headers_commit"
    fi
    if [[ ! -e "$vulkan_headers_source" ]]; then
        mv -- "$vulkan_headers_stage/source" "$vulkan_headers_source"
    fi
    rm -rf -- "$vulkan_headers_stage"
    trap - EXIT
fi
if [[ ! -f "$vulkan_headers_source/include/vulkan/vulkan_core.h" ]]; then
    echo "Cached Vulkan-Headers source is incomplete: $vulkan_headers_source" >&2
    exit 1
fi
resolved_vulkan_headers_commit="$(git -C "$vulkan_headers_source" rev-parse HEAD)"
vulkan_headers_include_dir="$vulkan_headers_source/include"
echo "Using shared Vulkan-Headers $vulkan_headers_revision ($resolved_vulkan_headers_commit)."

compiler_launcher=""
lsfg_fp16_tools_source=""
if [[ "$experimental_lsfg_fp16" == ON ]]; then
    lsfg_fp16_tools_source="$("$repo_root/scripts/prepare-lsfg-fp16-tools.sh")"
    # Source archives lack .git; prevent the optimizer's version generator
    # from reporting this enclosing MAKO repository's commit as its own.
    read -r optimizer_project optimizer_commit < "$lsfg_fp16_tools_source/.mako-source-pin"
    export FORCED_BUILD_VERSION_DESCRIPTION="$optimizer_commit"
    echo "Experimental LSFG FP16 conversion enabled; image quality is unqualified."
fi
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
    local install_libdir=lib
    local build_targets=(mako-render mako-render-scaling)

    if [[ "$architecture" == "64-bit" ]]; then
        build_cli=ON
        build_targets+=(mako-cli mako-remote-play-sdr)
    else
        install_libdir=lib32
    fi

    # Reconfiguring a persistent Ninja tree is cheap and picks up CMake/source
    # changes without throwing away already compiled objects.
    cmake -S "$repo_root" -B "$target_build_dir" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_COMPILER="$compiler" \
        -DCMAKE_CXX_COMPILER_LAUNCHER="$compiler_launcher" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DBUILD_TESTING=OFF \
        -DMAKO_REQUIRE_NATIVE_PACKAGE_HEADERS=ON \
        -DMAKO_VULKAN_HEADERS_INCLUDE_DIR="$vulkan_headers_include_dir" \
        -DMAKO_BUILD_VK_LAYER=ON \
        -DMAKO_BUILD_UI=OFF \
        -DMAKO_BUILD_CLI="$build_cli" \
        -DMAKO_EXPERIMENTAL_LSFG_FP16="$experimental_lsfg_fp16" \
        -DMAKO_LSFG_FP16_TOOLS_SOURCE="$lsfg_fp16_tools_source" \
        -DMAKO_INSTALL_XDG_FILES=OFF \
        -DMAKO_LAYER_LIBRARY_PATH="../$install_libdir/libmako-render.so" \
        -DMAKO_SCALING_LAYER_LIBRARY_PATH="../$install_libdir/libmako-render-scaling.so" \
        "$@"

    if [[ "$experimental_lsfg_fp16" == ON ]]; then
        # Refresh with the canonical upstream generator even when an existing
        # Ninja cache retained a version generated before the pin was supplied.
        python3 "$lsfg_fp16_tools_source/utils/update_build_version.py" \
            "$lsfg_fp16_tools_source/CHANGES" \
            "$target_build_dir/mako-backend/spirv-tools/build-version.inc"
    fi

    cmake --build "$target_build_dir" --parallel "$jobs" --target "${build_targets[@]}"

    local layer_path="$target_build_dir/mako-render/libmako-render.so"
    local scaling_layer_path="$target_build_dir/mako-render/libmako-render-scaling.so"
    if [[ ! -f "$layer_path" ]]; then
        echo "Expected $architecture layer output is missing: $layer_path" >&2
        exit 1
    fi
    if [[ ! -f "$scaling_layer_path" ]]; then
        echo "Expected $architecture spatial layer output is missing: $scaling_layer_path" >&2
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
        -DCMAKE_C_FLAGS=-m32 \
        -DCMAKE_SHARED_LINKER_FLAGS=-m32 \
        -DCMAKE_INSTALL_LIBDIR=lib32 \
        -DMAKO_LAYER_MANIFEST_SUFFIX=.x86
fi
