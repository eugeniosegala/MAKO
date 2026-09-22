#!/usr/bin/env bash
# Build the synchronized Arch recipe against one exact local Renderer archive.
# This is a release-time validation: the resulting package is disposable and is
# not published or installed.

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    echo "Usage: verify-release-package.sh <MAKO-Renderer-vX.Y.Z-linux.tar.xz>" >&2
}

if (($# != 1)); then
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

if ! command -v makepkg >/dev/null 2>&1; then
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
    exec "$container_runtime" run --rm --platform linux/amd64 \
        -v "$script_dir:/package-source:ro" \
        -v "$archive_path:/release-archive/$expected_archive:ro" \
        archlinux:base-devel \
        bash -c '
            set -euo pipefail
            useradd --create-home mako-builder
            install -d -o mako-builder -g mako-builder /tmp/mako-arch-package-source
            cp \
                /package-source/PKGBUILD \
                /package-source/mako-renderer-bin.install \
                /package-source/verify-release-package.sh \
                "/release-archive/$1" \
                /tmp/mako-arch-package-source/
            chown -R mako-builder:mako-builder /tmp/mako-arch-package-source
            exec runuser -u mako-builder -- \
                bash /tmp/mako-arch-package-source/verify-release-package.sh \
                "/tmp/mako-arch-package-source/$1"
        ' _ "$expected_archive"
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

echo "Verified Arch package build for MAKO Renderer $pkgver ($expected_archive)."
