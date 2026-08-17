#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
monorepo_root="$(cd "$repo_root/.." && pwd)"
version="$(tr -d '[:space:]' < "$repo_root/VERSION")"
default_output="$repo_root/out/mako-render-$version-linux.tar.xz"
output_path=""
build_32_bit=true

usage() {
    cat <<'EOF'
Usage: scripts/package-local.sh [--64-bit-only] [output-path]

Build and verify a local Linux engine archive. --64-bit-only omits the 32-bit
layer and manifest for faster native 64-bit Deck/Steam Machine test builds.
EOF
}

while (($#)); do
    case "$1" in
        --64-bit-only)
            build_32_bit=false
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --*)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
        *)
            if [[ -n "$output_path" ]]; then
                echo "Only one output path may be specified" >&2
                exit 2
            fi
            output_path="$1"
            ;;
    esac
    shift
done
output_path="${output_path:-$default_output}"

if [[ "$output_path" != /* ]]; then
    output_path="$PWD/$output_path"
fi

if [[ -z "$version" ]]; then
    echo "VERSION must contain a release version." >&2
    exit 1
fi

if [[ "$(uname -s)" != "Linux" ]]; then
    if ! command -v docker >/dev/null 2>&1; then
        echo "Packaging needs Linux. Install Docker Desktop or run this script on Linux." >&2
        exit 1
    fi

    case "$output_path" in
        "$repo_root"/*)
            output_relative="${output_path#"$repo_root"/}"
            ;;
        *)
            echo "On non-Linux hosts, the output path must be inside this repository." >&2
            exit 1
            ;;
    esac

    echo "Using local linux/amd64 Docker packaging environment..."
    docker_64_only=0
    if [[ "$build_32_bit" == false ]]; then
        docker_64_only=1
    fi
    exec docker run --rm --platform linux/amd64 \
        -e MAKO_PACKAGE_64_ONLY="$docker_64_only" \
        -v "$monorepo_root:/workspace" \
        -w /workspace/engine \
        ubuntu:22.04 \
        bash -lc '
            set -euo pipefail
            export DEBIAN_FRONTEND=noninteractive
            sed -i "s|http://|https://|g" /etc/apt/sources.list
            # Minimal Ubuntu images do not contain a CA bundle. APT still
            # verifies signed Ubuntu repository metadata during this one-time
            # TLS bootstrap; subsequent downloads use normal certificate checks.
            if [[ ! -s /etc/ssl/certs/ca-certificates.crt ]]; then
                apt-get -o Acquire::https::Verify-Peer=false update -qq
                apt-get -o Acquire::https::Verify-Peer=false install -y -qq ca-certificates
            fi
            apt-get update -qq
            apt-get install -y -qq \
                git curl llvm clang cmake ninja-build pkg-config g++-multilib \
                libvulkan-dev mesa-common-dev \
                qt6-base-dev qt6-base-dev-tools \
                qt6-tools-dev qt6-tools-dev-tools \
                qt6-declarative-dev qt6-declarative-dev-tools
            git clone --depth=1 -b vulkan-sdk-1.4.328 \
                https://github.com/KhronosGroup/Vulkan-Headers /tmp/vkh
            rm -rf /usr/include/vulkan /usr/include/vk_video
            cp -a /tmp/vkh/include/vulkan /tmp/vkh/include/vk_video /usr/include/
            package_args=()
            if [[ "${MAKO_PACKAGE_64_ONLY:-0}" == "1" ]]; then
                package_args+=(--64-bit-only)
            fi
            scripts/package-local.sh "${package_args[@]}" "/workspace/engine/'"$output_relative"'"
        '
fi

for command in cmake ninja clang++ strings tar sha256sum; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "Required command not found: $command" >&2
        exit 1
    fi
done

has_usable_qt_prefix() {
    local prefix="$1"
    [[ -f "$prefix/lib/cmake/Qt6/Qt6Config.cmake" &&
       -f "$prefix/include/qt6/QtCore/QtCore" &&
       -f "$prefix/include/GL/gl.h" &&
       -e "$prefix/lib/libOpenGL.so" ]]
}

bootstrap_native_qt_sdk() {
    local prefix_list="/usr"
    local candidate_prefix
    local configured_prefixes
    local -a prefix_candidates=()

    if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
        configured_prefixes="${CMAKE_PREFIX_PATH//;/:}"
        IFS=':' read -r -a prefix_candidates <<< "$configured_prefixes"
        for candidate_prefix in "${prefix_candidates[@]}"; do
            if [[ -n "$candidate_prefix" ]] && has_usable_qt_prefix "${candidate_prefix%/}"; then
                return
            fi
        done
    fi
    if has_usable_qt_prefix "$prefix_list"; then
        return
    fi

    for command in pacman curl bsdtar; do
        if ! command -v "$command" >/dev/null 2>&1; then
            echo "MAKO's release UI needs Qt 6 development files, but $command is unavailable." >&2
            echo "Install the Qt development packages, or run the package script on a Pacman-based SteamOS host." >&2
            exit 1
        fi
    done

    local sdk_cache_dir="${MAKO_NATIVE_SDK_DIR:-$repo_root/build/native-sdk}"
    if [[ "$sdk_cache_dir" != /* ]]; then
        sdk_cache_dir="$repo_root/$sdk_cache_dir"
    fi
    local package_cache_dir="$sdk_cache_dir/packages"
    local package_urls
    if ! package_urls="$(pacman -Sp qt6-base qt6-declarative libglvnd)"; then
        echo "Could not resolve the SteamOS packages needed for MAKO's native Qt SDK." >&2
        exit 1
    fi

    local -a package_names=(qt6-base qt6-declarative libglvnd)
    local -a package_files=()
    local package_name
    local package_url
    local package_file
    local -A package_sources=()
    while IFS= read -r package_url; do
        package_file="${package_url##*/}"
        for package_name in "${package_names[@]}"; do
            if [[ "$package_file" == "$package_name"-*.pkg.tar.* ]]; then
                package_sources["$package_name"]="$package_url"
            fi
        done
    done <<< "$package_urls"

    for package_name in "${package_names[@]}"; do
        package_url="${package_sources[$package_name]:-}"
        if [[ -z "$package_url" ]]; then
            echo "Pacman did not provide a download URL for $package_name." >&2
            exit 1
        fi
        package_file="${package_url##*/}"
        package_files+=("$package_file")
        mkdir -p "$package_cache_dir"
        if [[ ! -s "$package_cache_dir/$package_file" ]]; then
            echo "Caching $package_name for MAKO's native release builds..."
            curl --fail --location --retry 3 \
                --output "$package_cache_dir/$package_file.part" \
                "$package_url"
            mv "$package_cache_dir/$package_file.part" "$package_cache_dir/$package_file"
        fi
    done

    local sdk_key
    sdk_key="$(printf '%s\n' "${package_files[@]}" | sha256sum | awk '{print substr($1, 1, 16)}')"
    local sdk_root="$sdk_cache_dir/$sdk_key"
    if [[ ! -f "$sdk_root/.ready" ]]; then
        echo "Preparing isolated native Qt SDK at $sdk_root..."
        mkdir -p "$sdk_root"
        for package_file in "${package_files[@]}"; do
            bsdtar -xf "$package_cache_dir/$package_file" -C "$sdk_root"
        done
        touch "$sdk_root/.ready"
    fi

    local sdk_prefix="$sdk_root/usr"
    if ! has_usable_qt_prefix "$sdk_prefix"; then
        echo "The cached MAKO native Qt SDK is incomplete: $sdk_root" >&2
        echo "Remove that SDK directory and run the package command again." >&2
        exit 1
    fi

    export CMAKE_PREFIX_PATH="$sdk_prefix${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
    echo "Using cached isolated native Qt SDK: $sdk_prefix"
}

bootstrap_native_qt_sdk

build_root="$(mktemp -d "${TMPDIR:-/tmp}/mako-package.XXXXXX")"
cleanup() {
    rm -rf "$build_root"
}
trap cleanup EXIT

build64_dir="$build_root/build64"
build32_dir="$build_root/build32"
install_dir="$build_root/target"
mkdir -p "$(dirname "$output_path")"

cmake -S "$repo_root" -B "$build64_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$install_dir" \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DMAKO_BUILD_VK_LAYER=ON \
    -DMAKO_BUILD_UI=ON \
    -DMAKO_BUILD_CLI=ON \
    -DMAKO_INSTALL_XDG_FILES=ON \
    -DMAKO_LAYER_LIBRARY_PATH="../../../lib/libmako-render.so"

cmake --build "$build64_dir" --target \
    mako-config-tests mako-profile-update-tests \
    mako-runtime-transition-tests \
    mako-presentation-policy-tests \
    mako-adaptive-tests mako-adaptive-matrix \
    mako-pnext-chain-tests mako-color-tests \
    mako-hdr-color-math-tests
ctest --test-dir "$build64_dir" --output-on-failure
cmake --build "$build64_dir"
cmake --install "$build64_dir"

if [[ "$build_32_bit" == true ]]; then
    # A Vulkan layer is loaded into the application's process. Release archives
    # retain the second copy for genuine 32-bit games; local 64-bit-only builds
    # skip it to shorten the edit/test cycle.
    cmake -S "$repo_root" -B "$build32_dir" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$install_dir" \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_CXX_FLAGS=-m32 \
        -DCMAKE_SHARED_LINKER_FLAGS=-m32 \
        -DCMAKE_INSTALL_LIBDIR=lib32 \
        -DBUILD_TESTING=OFF \
        -DMAKO_BUILD_VK_LAYER=ON \
        -DMAKO_BUILD_UI=OFF \
        -DMAKO_BUILD_CLI=OFF \
        -DMAKO_INSTALL_XDG_FILES=OFF \
        -DMAKO_LAYER_MANIFEST_SUFFIX=.x86 \
        -DMAKO_LAYER_LIBRARY_PATH="../../../lib32/libmako-render.so"

    cmake --build "$build32_dir" --target mako-render
    cmake --install "$build32_dir"
fi

required_paths=(
    "bin/mako-cli" \
    "bin/mako-ui" \
    "lib/libmako-render.so" \
    "share/doc/mako-render/LICENSE.md" \
    "share/vulkan/implicit_layer.d/VkLayer_MAKO_render.json"
)
if [[ "$build_32_bit" == true ]]; then
    required_paths+=(
        "lib32/libmako-render.so"
        "share/vulkan/implicit_layer.d/VkLayer_MAKO_render.x86.json"
    )
fi
for required_path in "${required_paths[@]}"; do
    if [[ ! -e "$install_dir/$required_path" ]]; then
        echo "Packaging failed: missing $required_path" >&2
        exit 1
    fi
done

verify_elf_class() {
    local path="$1"
    local expected="$2"
    local actual
    actual="$(od -An -t u1 -j 4 -N 1 "$path" | tr -d '[:space:]')"
    if [[ "$actual" != "$expected" ]]; then
        echo "Packaging failed: $path has unexpected ELF class byte $actual" >&2
        exit 1
    fi
}

verify_elf_class "$install_dir/lib/libmako-render.so" 2
if [[ "$build_32_bit" == true ]]; then
    verify_elf_class "$install_dir/lib32/libmako-render.so" 1
fi

manifest64="$install_dir/share/vulkan/implicit_layer.d/VkLayer_MAKO_render.json"
if ! grep -Fq '"library_arch": "64"' "$manifest64" ||
        ! grep -Fq '../../../lib/libmako-render.so' "$manifest64" ||
        ! grep -Fq '"name": "VK_LAYER_MAKO_render"' "$manifest64" ||
        ! grep -Fq '"ENABLE_MAKO": "1"' "$manifest64" ||
        ! grep -Fq '"DISABLE_MAKO": "1"' "$manifest64"; then
    echo "Packaging failed: 64-bit Vulkan manifest is incorrect" >&2
    exit 1
fi
if [[ "$build_32_bit" == true ]]; then
    manifest32="$install_dir/share/vulkan/implicit_layer.d/VkLayer_MAKO_render.x86.json"
    if ! grep -Fq '"library_arch": "32"' "$manifest32" ||
            ! grep -Fq '../../../lib32/libmako-render.so' "$manifest32" ||
            ! grep -Fq '"name": "VK_LAYER_MAKO_render"' "$manifest32" ||
            ! grep -Fq '"ENABLE_MAKO": "1"' "$manifest32" ||
            ! grep -Fq '"DISABLE_MAKO": "1"' "$manifest32"; then
        echo "Packaging failed: 32-bit Vulkan manifest is incorrect" >&2
        exit 1
    fi
fi

layer_binaries=("$install_dir/lib/libmako-render.so")
if [[ "$build_32_bit" == true ]]; then
    layer_binaries+=("$install_dir/lib32/libmako-render.so")
fi
for layer_binary in "${layer_binaries[@]}"; do
    if ! strings "$layer_binary" |
            grep -F "mako: render layer active; identity=VK_LAYER_MAKO_render; build=$version" >/dev/null; then
        echo "Packaging failed: layer build identity diagnostic is missing from $layer_binary" >&2
        exit 1
    fi
done

tar -C "$install_dir" -cJf "$output_path" .

echo "Created and verified: $output_path"
echo "Version: $version"
echo "Architectures: $([[ "$build_32_bit" == true ]] && printf '64,32' || printf '64')"
