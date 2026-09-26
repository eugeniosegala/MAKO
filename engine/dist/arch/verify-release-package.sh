#!/usr/bin/env bash
# Build the synchronized Arch recipe against one exact local Renderer archive.
# With an output path, the exact verified package is retained for publication.
# Without one, the validation build remains disposable and is not installed.

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
package_epoch="${SOURCE_DATE_EPOCH:-}"
if [[ -n "$package_epoch" && ! "$package_epoch" =~ ^[0-9]+$ ]]; then
    echo "SOURCE_DATE_EPOCH must be a Unix timestamp, received: $package_epoch" >&2
    exit 2
fi
if [[ -n "$package_epoch" ]]; then
    export SOURCE_DATE_EPOCH="$package_epoch"
fi

usage() {
    echo "Usage: verify-release-package.sh <MAKO-Renderer-vX.Y.Z-linux.tar.xz> [output-package]" >&2
}

if (($# < 1 || $# > 2)); then
    usage
    exit 2
fi

archive_argument="$1"
if [[ ! -f "$archive_argument" ]]; then
    echo "Arch package verification archive is missing: $archive_argument" >&2
    exit 1
fi
archive_dir="$(cd "$(dirname "$archive_argument")" && pwd -P)"
archive_path="$archive_dir/$(basename "$archive_argument")"

pkgver="$(sed -n 's/^pkgver=\(.*\)$/\1/p' "$script_dir/PKGBUILD" | head -n 1)"
pkgrel="$(sed -n 's/^pkgrel=\(.*\)$/\1/p' "$script_dir/PKGBUILD" | head -n 1)"
if [[ -z "$pkgver" || -z "$pkgrel" ]]; then
    echo "Could not read pkgver and pkgrel from $script_dir/PKGBUILD" >&2
    exit 1
fi

expected_archive="MAKO-Renderer-v${pkgver}-linux.tar.xz"
if [[ "$(basename "$archive_path")" != "$expected_archive" ]]; then
    echo "Arch package verification expected $expected_archive, received $(basename "$archive_path")" >&2
    exit 1
fi

expected_package="mako-renderer-bin-${pkgver}-${pkgrel}-x86_64.pkg.tar.zst"
output_path=""
if (($# == 2)); then
    output_argument="$2"
    output_name="$(basename "$output_argument")"
    if [[ "$output_name" != "$expected_package" ]]; then
        echo "Arch package output must be named $expected_package, received $output_name" >&2
        exit 1
    fi
    output_dir="$(cd "$(dirname "$output_argument")" && pwd -P)"
    output_path="$output_dir/$output_name"
fi

if ! command -v makepkg >/dev/null 2>&1 || ! command -v fakeroot >/dev/null 2>&1; then
    container_runtime=""
    if command -v docker >/dev/null 2>&1; then
        container_runtime="docker"
    elif command -v podman >/dev/null 2>&1; then
        container_runtime="podman"
    else
        echo "Arch package verification needs makepkg, Docker, or Podman." >&2
        exit 1
    fi

    echo "Using local linux/amd64 $container_runtime Arch packaging environment..."
    container_arguments=(
        run --rm --platform linux/amd64
        -v "$script_dir:/package-source:ro"
        -v "$archive_path:/release-archive/$expected_archive:ro"
    )
    container_output_path=""
    if [[ -n "$output_path" ]]; then
        container_arguments+=(
            -v "$output_dir:/release-output"
        )
        container_output_path="/release-output/$output_name"
    fi
    if [[ -n "$package_epoch" ]]; then
        container_arguments+=(
            -e "SOURCE_DATE_EPOCH=$package_epoch"
        )
    fi
    exec "$container_runtime" "${container_arguments[@]}" \
        archlinux:base-devel \
        bash -c '
            set -euo pipefail
            if ! command -v fakeroot >/dev/null 2>&1; then
                pacman -Syu --noconfirm --needed fakeroot
            fi
            useradd --create-home mako-builder
            install -d -o mako-builder -g mako-builder /tmp/mako-arch-package-source
            cp \
                /package-source/PKGBUILD \
                /package-source/mako-renderer-bin.install \
                /package-source/verify-release-package.sh \
                "/release-archive/$1" \
                /tmp/mako-arch-package-source/
            chown -R mako-builder:mako-builder /tmp/mako-arch-package-source
            if [[ -n "${2:-}" ]]; then
                runuser -u mako-builder -- \
                    bash /tmp/mako-arch-package-source/verify-release-package.sh \
                    "/tmp/mako-arch-package-source/$1" \
                    "/tmp/mako-arch-package-source/$3"
                output_tmp="$2.tmp.$$"
                trap "rm -f -- \"$output_tmp\"" EXIT
                install -Dm644 "/tmp/mako-arch-package-source/$3" "$output_tmp"
                mv -f -- "$output_tmp" "$2"
            else
                exec runuser -u mako-builder -- \
                    bash /tmp/mako-arch-package-source/verify-release-package.sh \
                    "/tmp/mako-arch-package-source/$1"
            fi
        ' _ "$expected_archive" "$container_output_path" "$expected_package"
fi

for command in bsdtar find; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "Required command not found: $command" >&2
        exit 1
    fi
done

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/mako-arch-release-package.XXXXXX")"
cleanup() {
    rm -rf -- "$work_dir"
}
trap cleanup EXIT

cp -- "$script_dir/PKGBUILD" "$script_dir/mako-renderer-bin.install" "$work_dir/"
cp -- "$archive_path" "$work_dir/$expected_archive"
mkdir -p "$work_dir/home" "$work_dir/config"

(
    cd "$work_dir"
    HOME="$work_dir/home" \
        XDG_CONFIG_HOME="$work_dir/config" \
        SRCDEST="$work_dir" \
        PKGDEST="$work_dir" \
        makepkg --nodeps --cleanbuild --noconfirm
)

mapfile -t package_files < <(
    find "$work_dir" -maxdepth 1 -type f \
        -name "mako-renderer-bin-${pkgver}-${pkgrel}-*.pkg.tar.*" \
        ! -name '*.sig' -print
)
if ((${#package_files[@]} != 1)); then
    echo "Expected exactly one built Arch package, found ${#package_files[@]}." >&2
    exit 1
fi

package_entries="$(bsdtar -tf "${package_files[0]}")"
for required_entry in \
    .INSTALL \
    usr/bin/mako-cli \
    usr/bin/mako-diagnostics \
    usr/bin/mako-launch \
    usr/bin/mako-ui \
    usr/lib/libmako-render.so \
    usr/lib/libmako-render-scaling.so \
    usr/lib32/libmako-render.so \
    usr/lib32/libmako-render-scaling.so \
    usr/share/applications/io.github.eugeniosegala.mako.desktop; do
    if ! grep -Fqx "$required_entry" <<< "$package_entries"; then
        echo "Built Arch package is missing $required_entry" >&2
        exit 1
    fi
done

if grep -Eq '(^|/)mako-installer$|io\.github\.eugeniosegala\.mako\.uninstaller\.desktop$|(^|/)\.config/mako-render/' <<< "$package_entries"; then
    echo "Built Arch package contains a user-local installer, uninstaller, or profile path." >&2
    exit 1
fi

package_info="$(bsdtar -xOf "${package_files[0]}" .PKGINFO)"
for required_identity in \
    "pkgname = mako-renderer-bin" \
    "pkgver = ${pkgver}-${pkgrel}" \
    "arch = x86_64"; do
    if ! grep -Fqx "$required_identity" <<< "$package_info"; then
        echo "Built Arch package has the wrong internal identity; missing: $required_identity" >&2
        exit 1
    fi
done

packaged_renderer_version="$(
    bsdtar -xOf "${package_files[0]}" \
        usr/share/doc/mako-renderer-bin/MAKO-Renderer-version.txt |
        tr -d '[:space:]'
)"
if [[ "$packaged_renderer_version" != "$pkgver" ]]; then
    echo "Built Arch package contains Renderer $packaged_renderer_version instead of $pkgver." >&2
    exit 1
fi

if [[ -n "$output_path" ]]; then
    output_tmp="${output_path}.tmp.$$"
    trap 'rm -rf -- "$work_dir"; rm -f -- "$output_tmp"' EXIT
    install -Dm644 "${package_files[0]}" "$output_tmp"
    mv -f -- "$output_tmp" "$output_path"
    echo "Verified Arch release package: $output_path"
fi

echo "Verified Arch package build for MAKO Renderer $pkgver ($expected_archive)."
