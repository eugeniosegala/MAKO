#!/bin/sh
# Verify that engine/dist/arch/PKGBUILD is still pinned to the release that
# upstream validated.
#
# plugin/package.json remote_binary[0] is written by the release flow only after
# the native build and its gates pass, so it is the authoritative version, URL
# and checksum for the host archive this package repackages. This script fails
# when the PKGBUILD drifts from that pin.
#
# It is read-only, needs no network access, and runs on any POSIX shell.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
repo_root=$(CDPATH= cd -- "$script_dir/../../.." && pwd -P)
pkgbuild="${1:-$script_dir/PKGBUILD}"
install_script="$script_dir/mako-renderer-bin.install"
pin_file="${2:-$repo_root/plugin/package.json}"

fail() {
    printf 'check-release-pin: %s\n' "$1" >&2
    exit 1
}

[ -f "$pkgbuild" ] || fail "missing $pkgbuild"
[ -f "$install_script" ] || fail "missing $install_script"
[ -f "$pin_file" ] || fail "missing $pin_file"

# The pin file is Prettier-formatted JSON with one key per line, so read the
# remote_binary block line by line instead of pulling in a JSON parser.
pin_block=$(awk '/^  "remote_binary": \[/,/^  \]/' "$pin_file")
[ -n "$pin_block" ] || fail "could not locate remote_binary in $pin_file"

pin_version=$(printf '%s\n' "$pin_block" | sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' | head -n 1)
pin_url=$(printf '%s\n' "$pin_block" | sed -n 's/.*"url": *"\([^"]*\)".*/\1/p' | head -n 1)
pin_sha256=$(printf '%s\n' "$pin_block" | sed -n 's/.*"sha256hash": *"\([^"]*\)".*/\1/p' | head -n 1)

[ -n "$pin_version" ] || fail "could not read remote_binary version from $pin_file"
[ -n "$pin_url" ] || fail "could not read remote_binary url from $pin_file"
[ -n "$pin_sha256" ] || fail "could not read remote_binary sha256hash from $pin_file"

pkg_ver=$(sed -n 's/^pkgver=\(.*\)$/\1/p' "$pkgbuild" | head -n 1)
pkg_sha256=$(sed -n "s/^sha256sums=('\([^']*\)').*/\1/p" "$pkgbuild" | head -n 1)

[ -n "$pkg_ver" ] || fail "could not read pkgver from $pkgbuild"
[ -n "$pkg_sha256" ] || fail "could not read sha256sums from $pkgbuild"

# The source line uses ${pkgver}, so expand it before comparing to the pinned URL.
# A source entry may be written as "filename::url"; keep only the URL.
pkg_source=$(sed -n 's/^source=("\(.*\)")$/\1/p' "$pkgbuild" | head -n 1)
pkg_source_url=${pkg_source##*::}
pkg_source_url=$(printf '%s\n' "$pkg_source_url" | sed "s/\\\${pkgver}/$pin_version/g")

[ -n "$pkg_source_url" ] || fail "could not read source url from $pkgbuild"

[ "$pkg_ver" = "$pin_version" ] ||
    fail "PKGBUILD pkgver=$pkg_ver does not match the pinned release $pin_version"
[ "$pkg_sha256" = "$pin_sha256" ] ||
    fail "PKGBUILD sha256sums=$pkg_sha256 does not match the pinned release checksum $pin_sha256"
[ "$pkg_source_url" = "$pin_url" ] ||
    fail "PKGBUILD source url $pkg_source_url does not match the pinned archive URL $pin_url"
grep -Fq -- 'install="${pkgname}.install"' "$pkgbuild" ||
    fail 'PKGBUILD does not declare install="${pkgname}.install"'
grep -Fq -- 'provides=("mako-renderer=${pkgver}")' "$pkgbuild" ||
    fail 'PKGBUILD does not provide the versioned mako-renderer package identity'
grep -Fq -- "conflicts=('mako-renderer')" "$pkgbuild" ||
    fail 'PKGBUILD does not conflict with the alternate mako-renderer package'
bash -n "$pkgbuild" || fail "$pkgbuild is not valid bash syntax"
sh -n "$install_script" || fail "$install_script is not valid POSIX sh syntax"

printf 'check-release-pin: PKGBUILD matches the pinned release %s\n' "$pin_version"
printf '  archive: %s\n' "$pin_url"
printf '  sha256:  %s\n' "$pin_sha256"
