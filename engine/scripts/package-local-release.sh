#!/usr/bin/env bash
# Build the complete release-shaped MAKO Renderer artifact set without publishing.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
monorepo_root="$(cd "$repo_root/.." && pwd)"
version="$(tr -d '[:space:]' < "$repo_root/VERSION")"
output_argument="${1:-$repo_root/out}"

usage() {
    cat <<'EOF'
Usage: scripts/package-local-release.sh [output-directory]

Builds the release-shaped MAKO Renderer host, Flatpak, and Arch Linux artifacts
from the current checkout without changing release pins, tagging, or publishing.
The output directory must be inside engine/ so portable container builds can
write the artifacts.
EOF
}

if (($# > 1)); then
    usage >&2
    exit 2
fi
if [[ "$output_argument" == "-h" || "$output_argument" == "--help" ]]; then
    usage
    exit 0
fi
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "engine/VERSION must use X.Y.Z format: $version" >&2
    exit 1
fi

if [[ "$output_argument" != /* ]]; then
    output_argument="$PWD/$output_argument"
fi
mkdir -p "$output_argument"
output_dir="$(cd "$output_argument" && pwd -P)"
case "$output_dir/" in
    "$repo_root"/*) ;;
    *)
        echo "Local release output must be inside $repo_root for portable builds." >&2
        exit 1
        ;;
esac

for command in git python3; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "Required command not found: $command" >&2
        exit 1
    fi
done
if command -v sha256sum >/dev/null 2>&1; then
    checksum_command=(sha256sum)
elif command -v shasum >/dev/null 2>&1; then
    checksum_command=(shasum -a 256)
else
    echo "Required command not found: sha256sum or shasum" >&2
    exit 1
fi

host_archive="$output_dir/MAKO-Renderer-v$version-linux.tar.xz"
flatpak_archive="$output_dir/MAKO-Renderer-v$version-flatpaks.tar.xz"

echo "Building portable MAKO Renderer host archive..."
MAKO_PORTABLE_PACKAGE=1 "$repo_root/scripts/package-local.sh" "$host_archive"

echo "Building portable MAKO Renderer Flatpak archive..."
MAKO_PORTABLE_PACKAGE=1 "$repo_root/scripts/package-flatpaks.sh" "$flatpak_archive"

host_checksum="$("${checksum_command[@]}" "$host_archive" | awk '{print $1}')"
tracked_pkgver="$(sed -n 's/^pkgver=\(.*\)$/\1/p' "$repo_root/dist/arch/PKGBUILD" | head -n 1)"
tracked_pkgrel="$(sed -n 's/^pkgrel=\(.*\)$/\1/p' "$repo_root/dist/arch/PKGBUILD" | head -n 1)"
if [[ -z "$tracked_pkgver" || -z "$tracked_pkgrel" ]]; then
    echo "Could not read pkgver and pkgrel from dist/arch/PKGBUILD." >&2
    exit 1
fi
arch_pkgrel=1
if [[ "$tracked_pkgver" == "$version" ]]; then
    arch_pkgrel="$tracked_pkgrel"
fi
arch_package="$output_dir/mako-renderer-bin-${version}-${arch_pkgrel}-x86_64.pkg.tar.zst"

# The tracked recipe remains pinned to the last public host archive. Build the
# local package from a disposable copy synchronized to this checkout's newly
# built archive, matching the publication flow without mutating release state.
work_root="${MAKO_BUILD_WORK_ROOT:-$repo_root/build/work}"
if [[ "$work_root" != /* ]]; then
    work_root="$repo_root/$work_root"
fi
mkdir -p "$work_root"
recipe_dir="$(mktemp -d "$work_root/mako-local-release-arch.XXXXXX")"
cleanup() {
    rm -rf -- "$recipe_dir"
}
trap cleanup EXIT
cp -- \
    "$repo_root/dist/arch/PKGBUILD" \
    "$repo_root/dist/arch/mako-renderer-bin.install" \
    "$repo_root/dist/arch/verify-release-package.sh" \
    "$recipe_dir/"
python3 - "$recipe_dir/PKGBUILD" "$version" "$arch_pkgrel" "$host_checksum" <<'PY'
from __future__ import annotations

import re
import sys
from pathlib import Path

path = Path(sys.argv[1])
version, release, checksum = sys.argv[2:]
contents = path.read_text(encoding="utf-8")
replacements = (
    (r"^pkgver=.*$", f"pkgver={version}", "pkgver"),
    (r"^pkgrel=.*$", f"pkgrel={release}", "pkgrel"),
    (r"^sha256sums=\('[^']*'\)$", f"sha256sums=('{checksum}')", "sha256sums"),
)
for pattern, replacement, field in replacements:
    contents, count = re.subn(pattern, replacement, contents, flags=re.MULTILINE)
    if count != 1:
        raise SystemExit(f"Expected exactly one {field} assignment in {path}")
path.write_text(contents, encoding="utf-8")
PY

echo "Building verified Arch Linux package from the local host archive..."
package_epoch="${SOURCE_DATE_EPOCH:-}"
if [[ -z "$package_epoch" ]] && git -C "$monorepo_root" rev-parse --verify HEAD >/dev/null 2>&1; then
    package_epoch="$(git -C "$monorepo_root" show -s --format=%ct HEAD)"
fi
if [[ -n "$package_epoch" ]]; then
    SOURCE_DATE_EPOCH="$package_epoch" \
        "$recipe_dir/verify-release-package.sh" "$host_archive" "$arch_package"
else
    "$recipe_dir/verify-release-package.sh" "$host_archive" "$arch_package"
fi

checksums_path="$output_dir/SHA256SUMS"
checksums_tmp="$checksums_path.tmp.$$"
trap 'rm -rf -- "$recipe_dir"; rm -f -- "$checksums_tmp"' EXIT
(
    cd "$output_dir"
    "${checksum_command[@]}" \
        "$(basename "$host_archive")" \
        "$(basename "$flatpak_archive")" \
        "$(basename "$arch_package")"
) > "$checksums_tmp"
mv -f -- "$checksums_tmp" "$checksums_path"

echo "Complete local MAKO Renderer artifact set:"
echo "  $host_archive"
echo "  $flatpak_archive"
echo "  $arch_package"
echo "  $checksums_path"
