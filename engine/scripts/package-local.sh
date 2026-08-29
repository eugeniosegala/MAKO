#!/usr/bin/env bash
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
default_output="$repo_root/out/MAKO-Renderer-v$version-linux.tar.xz"
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

portable_container="${MAKO_PORTABLE_PACKAGE:-0}"
containerized_build="${MAKO_PACKAGE_CONTAINERIZED:-0}"
if [[ "$containerized_build" != "1" && ( "$(uname -s)" != "Linux" || "$portable_container" == "1" ) ]]; then
    container_runtime=""
    if command -v docker >/dev/null 2>&1; then
        container_runtime="docker"
    elif command -v podman >/dev/null 2>&1; then
        container_runtime="podman"
    else
        echo "Portable packaging needs Docker or Podman. Install one and try again." >&2
        exit 1
    fi

    case "$output_path" in
        "$repo_root"/*)
            output_relative="${output_path#"$repo_root"/}"
            ;;
        *)
            echo "For a containerized build, the output path must be inside this repository." >&2
            exit 1
            ;;
    esac

    echo "Using local linux/amd64 $container_runtime packaging environment..."
    docker_64_only=0
    if [[ "$build_32_bit" == false ]]; then
        docker_64_only=1
    fi
    exec "$container_runtime" run --rm --platform linux/amd64 \
        -e MAKO_PACKAGE_64_ONLY="$docker_64_only" \
        -e MAKO_PACKAGE_CONTAINERIZED=1 \
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

for command in cmake ninja clang++ nm readelf strings tar sha256sum; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "Required command not found: $command" >&2
        exit 1
    fi
done

has_usable_qt_prefix() {
    local prefix="$1"
    local qt_config
    local qt_header
    local gl_header
    local opengl_library
    qt_config="$(find "$prefix/lib" -path '*/cmake/Qt6/Qt6Config.cmake' -print -quit 2>/dev/null)"
    qt_header="$(find "$prefix/include" -path '*/qt6/QtCore/QtCore' -print -quit 2>/dev/null)"
    gl_header="$(find "$prefix/include" -path '*/GL/gl.h' -print -quit 2>/dev/null)"
    opengl_library="$(find "$prefix/lib" -name 'libOpenGL.so' -print -quit 2>/dev/null)"
    [[ -n "$qt_config" && -n "$qt_header" && -n "$gl_header" && -n "$opengl_library" ]]
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

    local sdk_cache_dir="${MAKO_NATIVE_SDK_DIR:-$build_cache_root/native-sdk}"
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

mkdir -p "$build_work_root"
build_root="$(mktemp -d "$build_work_root/mako-package.XXXXXX")"
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
    -DMAKO_LAYER_LIBRARY_PATH="../../../lib/libmako-render.so" \
    -DMAKO_SCALING_LAYER_LIBRARY_PATH="../../../lib/libmako-render-scaling.so"

# Build the complete configured tree before CTest so CMake remains the single
# authority for every registered test executable and production dependency.
cmake --build "$build64_dir"
ctest --test-dir "$build64_dir" --output-on-failure
# Release archives do not need local symbol tables. CMake's install-time strip
# preserves the dynamic entrypoints required by the Vulkan loader while
# keeping both the installed payload and compressed archive smaller.
cmake --install "$build64_dir" --strip

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
        -DMAKO_LAYER_LIBRARY_PATH="../../../lib32/libmako-render.so" \
        -DMAKO_SCALING_LAYER_LIBRARY_PATH="../../../lib32/libmako-render-scaling.so"

    cmake --build "$build32_dir" --target mako-render mako-render-scaling
    cmake --install "$build32_dir" --strip
fi

required_paths=(
    "bin/mako-cli" \
    "bin/mako-diagnostics" \
    "bin/mako-installer" \
    "bin/mako-launch" \
    "bin/mako-ui" \
    "lib/libmako-render.so" \
    "lib/libmako-render-scaling.so" \
    "share/doc/mako-render/LICENSE.md" \
    "share/mako-render/vulkan/implicit_layer.d/VkLayer_MAKO_render.json" \
    "share/mako-render/vulkan/spatial_scaling.d/VkLayer_MAKO_spatial_scaling.json" \
    "share/vulkan/implicit_layer.d/VkLayer_MAKO_spatial_scaling.json" \
    "share/vulkan/implicit_layer.d/VkLayer_MAKO_render.json"
)
if [[ "$build_32_bit" == true ]]; then
    required_paths+=(
        "lib32/libmako-render.so"
        "lib32/libmako-render-scaling.so"
        "share/mako-render/vulkan/implicit_layer.d/VkLayer_MAKO_render.x86.json"
        "share/vulkan/implicit_layer.d/VkLayer_MAKO_render.x86.json"
        "share/mako-render/vulkan/spatial_scaling.d/VkLayer_MAKO_spatial_scaling.x86.json"
        "share/vulkan/implicit_layer.d/VkLayer_MAKO_spatial_scaling.x86.json"
    )
fi
for required_path in "${required_paths[@]}"; do
    if [[ ! -e "$install_dir/$required_path" ]]; then
        echo "Packaging failed: missing $required_path" >&2
        exit 1
    fi
done

# The archive-root launcher is intentionally separate from the installed
# `mako-installer` command. Users extract the archive, double-click this file,
# and the installer copies only the verified payload into their user-local
# prefix. The generated manifest gives upgrades and uninstalls an exact,
# checksummed ownership record without treating the entire ~/.local tree as
# disposable.
cp "$repo_root/scripts/mako-installer" "$install_dir/Install MAKO Renderer"
chmod 0755 "$install_dir/Install MAKO Renderer"
printf '%s\n' "$version" > "$install_dir/MAKO-Renderer-version.txt"
manifest_roots=(bin lib share)
if [[ -d "$install_dir/lib32" ]]; then
    manifest_roots+=(lib32)
fi
(
    cd "$install_dir"
    find "${manifest_roots[@]}" -type f -print | LC_ALL=C sort | xargs sha256sum
) > "$install_dir/MAKO-Renderer-install-manifest.txt"
if [[ ! -s "$install_dir/MAKO-Renderer-install-manifest.txt" ]]; then
    echo "Packaging failed: standalone installer manifest is empty" >&2
    exit 1
fi
if ! (cd "$install_dir" && sha256sum --check --status MAKO-Renderer-install-manifest.txt); then
    echo "Packaging failed: standalone installer manifest does not verify" >&2
    exit 1
fi

if [[ ! -x "$install_dir/bin/mako-launch" ]]; then
    echo "Packaging failed: bin/mako-launch is not executable" >&2
    exit 1
fi
if [[ ! -x "$install_dir/bin/mako-installer" ]]; then
    echo "Packaging failed: bin/mako-installer is not executable" >&2
    exit 1
fi
bash -n "$install_dir/bin/mako-launch"
bash "$repo_root/scripts/test-mako-launch.sh" "$install_dir/bin/mako-launch"

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
verify_elf_class "$install_dir/lib/libmako-render-scaling.so" 2
if [[ "$build_32_bit" == true ]]; then
    verify_elf_class "$install_dir/lib32/libmako-render.so" 1
    verify_elf_class "$install_dir/lib32/libmako-render-scaling.so" 1
fi

scaling_manifest64="$install_dir/share/vulkan/implicit_layer.d/VkLayer_MAKO_spatial_scaling.json"
private_scaling_manifest64="$install_dir/share/mako-render/vulkan/spatial_scaling.d/VkLayer_MAKO_spatial_scaling.json"
if ! grep -Fq '"library_arch": "64"' "$scaling_manifest64" ||
        ! grep -Fq '../../../lib/libmako-render-scaling.so' "$scaling_manifest64" ||
        ! grep -Fq '"name": "VK_LAYER_MAKO_spatial_scaling"' "$scaling_manifest64" ||
        ! grep -Fq '"ENABLE_MAKO_SPATIAL_SCALING": "1"' "$scaling_manifest64" ||
        ! grep -Fq '"DISABLE_MAKO_SPATIAL_SCALING": "1"' "$scaling_manifest64"; then
    echo "Packaging failed: 64-bit spatial Vulkan manifest is incorrect" >&2
    exit 1
fi
if ! grep -Fq '"library_arch": "64"' "$private_scaling_manifest64" ||
        ! grep -Fq '../../../../lib/libmako-render-scaling.so' "$private_scaling_manifest64" ||
        ! grep -Fq '"name": "VK_LAYER_MAKO_spatial_scaling"' "$private_scaling_manifest64"; then
    echo "Packaging failed: private 64-bit spatial Vulkan manifest is incorrect" >&2
    exit 1
fi

# Qt promises backwards binary compatibility, not forwards compatibility.
# Keep release archives usable on the Qt 6.4 baseline provided by Ubuntu
# 24.04 even when packaging is initiated on a rolling distribution. Portable
# packaging intentionally builds against Qt 6.2 and therefore produces an
# archive with a broader runtime range.
ui_strings="$build_root/mako-ui.strings"
strings "$install_dir/bin/mako-ui" > "$ui_strings"
if grep -Eq 'Qt_6\.([5-9]|[1-9][0-9])' "$ui_strings"; then
    echo "Packaging failed: mako-ui requires a Qt ABI newer than 6.4." >&2
    echo "Re-run with MAKO_PORTABLE_PACKAGE=1 to use the reproducible Qt 6.2 container." >&2
    exit 1
fi
if grep -Fq 'libQt6QmlMeta.so.6' "$ui_strings"; then
    echo "Packaging failed: mako-ui unexpectedly depends on Qt 6.8's QmlMeta library." >&2
    echo "Re-run with MAKO_PORTABLE_PACKAGE=1 to use the reproducible Qt 6.2 container." >&2
    exit 1
fi

manifest64="$install_dir/share/vulkan/implicit_layer.d/VkLayer_MAKO_render.json"
private_manifest64="$install_dir/share/mako-render/vulkan/implicit_layer.d/VkLayer_MAKO_render.json"
if ! grep -Fq '"library_arch": "64"' "$manifest64" ||
        ! grep -Fq '../../../lib/libmako-render.so' "$manifest64" ||
        ! grep -Fq '"name": "VK_LAYER_MAKO_render"' "$manifest64" ||
        ! grep -Fq '"ENABLE_MAKO": "1"' "$manifest64" ||
        ! grep -Fq '"DISABLE_MAKO": "1"' "$manifest64"; then
    echo "Packaging failed: 64-bit Vulkan manifest is incorrect" >&2
    exit 1
fi
if ! grep -Fq '"library_arch": "64"' "$private_manifest64" ||
        ! grep -Fq '../../../../lib/libmako-render.so' "$private_manifest64" ||
        ! grep -Fq '"name": "VK_LAYER_MAKO_render"' "$private_manifest64"; then
    echo "Packaging failed: private 64-bit Vulkan manifest is incorrect" >&2
    exit 1
fi
if [[ "$build_32_bit" == true ]]; then
    manifest32="$install_dir/share/vulkan/implicit_layer.d/VkLayer_MAKO_render.x86.json"
    private_manifest32="$install_dir/share/mako-render/vulkan/implicit_layer.d/VkLayer_MAKO_render.x86.json"
    scaling_manifest32="$install_dir/share/vulkan/implicit_layer.d/VkLayer_MAKO_spatial_scaling.x86.json"
    private_scaling_manifest32="$install_dir/share/mako-render/vulkan/spatial_scaling.d/VkLayer_MAKO_spatial_scaling.x86.json"
    if ! grep -Fq '"library_arch": "32"' "$manifest32" ||
            ! grep -Fq '../../../lib32/libmako-render.so' "$manifest32" ||
            ! grep -Fq '"name": "VK_LAYER_MAKO_render"' "$manifest32" ||
            ! grep -Fq '"ENABLE_MAKO": "1"' "$manifest32" ||
            ! grep -Fq '"DISABLE_MAKO": "1"' "$manifest32"; then
        echo "Packaging failed: 32-bit Vulkan manifest is incorrect" >&2
        exit 1
    fi
    if ! grep -Fq '"library_arch": "32"' "$private_manifest32" ||
            ! grep -Fq '../../../../lib32/libmako-render.so' "$private_manifest32" ||
            ! grep -Fq '"name": "VK_LAYER_MAKO_render"' "$private_manifest32"; then
        echo "Packaging failed: private 32-bit Vulkan manifest is incorrect" >&2
        exit 1
    fi
    if ! grep -Fq '"library_arch": "32"' "$scaling_manifest32" ||
            ! grep -Fq '../../../lib32/libmako-render-scaling.so' "$scaling_manifest32" ||
            ! grep -Fq '"name": "VK_LAYER_MAKO_spatial_scaling"' "$scaling_manifest32" ||
            ! grep -Fq '"ENABLE_MAKO_SPATIAL_SCALING": "1"' "$scaling_manifest32" ||
            ! grep -Fq '"DISABLE_MAKO_SPATIAL_SCALING": "1"' "$scaling_manifest32"; then
        echo "Packaging failed: 32-bit spatial Vulkan manifest is incorrect" >&2
        exit 1
    fi
    if ! grep -Fq '"library_arch": "32"' "$private_scaling_manifest32" ||
            ! grep -Fq '../../../../lib32/libmako-render-scaling.so' "$private_scaling_manifest32" ||
            ! grep -Fq '"name": "VK_LAYER_MAKO_spatial_scaling"' "$private_scaling_manifest32"; then
        echo "Packaging failed: private 32-bit spatial Vulkan manifest is incorrect" >&2
        exit 1
    fi
fi

layer_binaries=("$install_dir/lib/libmako-render.so")
layer_binaries+=("$install_dir/lib/libmako-render-scaling.so")
if [[ "$build_32_bit" == true ]]; then
    layer_binaries+=("$install_dir/lib32/libmako-render.so")
    layer_binaries+=("$install_dir/lib32/libmako-render-scaling.so")
fi
relay_contract_symbol="makoSpatialScalingLookupFixedContract"
for layer_binary in "${layer_binaries[@]}"; do
    dynamic_dependencies="$(readelf -d "$layer_binary")"
    if ! grep -Fq 'Shared library: [libstdc++.so.6]' <<< "$dynamic_dependencies"; then
        echo "Packaging failed: $layer_binary must use SteamOS's dynamic C++ runtime" >&2
        echo "Static libstdc++ inside a Vulkan layer is not supported." >&2
        exit 1
    fi

    exported_symbols="$(nm -D --defined-only "$layer_binary" | awk '{print $3}')"
    for required_entrypoint in \
            vkNegotiateLoaderLayerInterfaceVersion \
            vkGetInstanceProcAddr \
            vkGetDeviceProcAddr; do
        if ! grep -Fxq "$required_entrypoint" <<< "$exported_symbols"; then
            echo "Packaging failed: Vulkan entrypoint $required_entrypoint is not exported by $layer_binary" >&2
            exit 1
        fi
    done
    expected_identity="VK_LAYER_MAKO_render"
    if [[ "$layer_binary" == *libmako-render-scaling.so ]]; then
        expected_identity="VK_LAYER_MAKO_spatial_scaling"
        if ! grep -Fxq "$relay_contract_symbol" <<< "$exported_symbols"; then
            echo "Packaging failed: spatial capability relay entrypoint is missing from $layer_binary" >&2
            exit 1
        fi
    elif grep -Fxq "$relay_contract_symbol" <<< "$exported_symbols"; then
        echo "Packaging failed: spatial capability relay entrypoint leaked into $layer_binary" >&2
        exit 1
    fi
    if ! strings "$layer_binary" |
            grep -F "MAKO Renderer: render layer active; identity=$expected_identity; build=$version" >/dev/null; then
        echo "Packaging failed: layer build identity diagnostic is missing from $layer_binary" >&2
        exit 1
    fi
    if ! strings "$layer_binary" | grep -F "MAKO_PROFILE_FALLBACK" >/dev/null; then
        echo "Packaging failed: profile-fallback wrapper protocol is missing from $layer_binary" >&2
        exit 1
    fi
done

tar -C "$install_dir" -cJf "$output_path" .

echo "Created and verified: $output_path"
echo "Version: $version"
echo "Architectures: $([[ "$build_32_bit" == true ]] && printf '64,32' || printf '64')"
